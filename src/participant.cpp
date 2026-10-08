#include "ldds/participant.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ldds {
namespace {

using Payload = std::shared_ptr<const Bytes>;
using Deferred = std::vector<std::function<void()>>;
using WriterKey = std::pair<std::uint64_t, std::uint32_t>;  // remote participant, writer id

constexpr std::size_t kMaxPending = 4096;
constexpr std::size_t kMaxNackSeqs = 64;
constexpr std::size_t kMaxBatch = 256;
constexpr std::size_t kMaxNameLength = 255;

struct StoredSample {
    std::uint64_t seq;
    std::int64_t timestamp_ns;
    Payload payload;
};

struct Pending {
    std::int64_t timestamp_ns;
    Payload payload;
};

struct LocalWriter {
    std::uint32_t id{0};
    std::string topic;
    std::string type;
    Qos qos;
    std::uint64_t next_seq{1};
    std::deque<StoredSample> history;
    TimePoint next_heartbeat{};
    WriterStats stats;
};

/// Per (reader, remote writer) reception state.
struct RemoteWriterState {
    std::uint64_t next_seq{0};
    std::uint64_t known_last{0};
    std::map<std::uint64_t, Pending> pending;
    TimePoint last_rx{};
    TimePoint next_nack{};
    bool deadline_flagged{false};
};

struct LocalReader {
    std::uint32_t id{0};
    std::string topic;
    std::string type;
    Qos qos;
    std::shared_ptr<const Participant::RawHandler> handler;
    std::map<WriterKey, RemoteWriterState> remotes;
    ReaderStats stats;
};

struct RemoteEntry {
    RemoteParticipant info;
    TimePoint last_seen{};
};

bool compatible(const Qos& writer, const Qos& reader) {
    return !(reader.reliability == Reliability::Reliable && writer.reliability == Reliability::BestEffort);
}

std::uint64_t random_id() {
    std::random_device rd;
    const std::uint64_t id = (static_cast<std::uint64_t>(rd()) << 32) | rd();
    return id != 0 ? id : 1;
}

void validate_endpoint(const std::string& topic, const std::string& type_name, const Qos& qos) {
    if (topic.empty() || topic.size() > kMaxNameLength) throw std::invalid_argument("topic must be 1-255 characters");
    if (type_name.empty() || type_name.size() > kMaxNameLength) {
        throw std::invalid_argument("type name must be 1-255 characters");
    }
    if (qos.history_depth == 0) throw std::invalid_argument("history depth must be at least 1");
    if (qos.deadline.count() < 0) throw std::invalid_argument("deadline must not be negative");
}

}  // namespace

const char* to_string(EventKind kind) noexcept {
    switch (kind) {
        case EventKind::ParticipantDiscovered: return "participant-discovered";
        case EventKind::ParticipantLost: return "participant-lost";
        case EventKind::DeadlineMissed: return "deadline-missed";
        case EventKind::SamplesLost: return "samples-lost";
    }
    return "unknown";
}

struct Participant::Impl {
    Impl(std::unique_ptr<Transport> t, Config c)
        : transport(std::move(t)),
          config(std::move(c)),
          clock(config.clock ? config.clock : std::make_shared<SteadyClock>()),
          id(config.id != 0 ? config.id : random_id()) {
        if (!transport) throw std::invalid_argument("transport must not be null");
    }

    std::unique_ptr<Transport> transport;
    const Config config;
    const std::shared_ptr<Clock> clock;
    const std::uint64_t id;

    mutable std::mutex mutex;
    std::map<std::uint32_t, LocalWriter> writers;
    std::map<std::uint32_t, LocalReader> readers;
    std::map<std::uint64_t, RemoteEntry> remotes;
    std::shared_ptr<const EventHandler> event_handler;
    std::uint32_t next_endpoint_id{1};
    TimePoint next_announce{};
    ParticipantStats stats;

    std::thread thread;
    std::atomic<bool> running{false};

    // ---- helpers (mutex held) -------------------------------------------------

    std::int64_t timestamp_ns(TimePoint now) const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    }

    void send_packet(const Packet& packet) {
        if (transport->send(encode(packet))) {
            ++stats.packets_out;
        } else {
            ++stats.send_failures;
        }
    }

    void send_announce(TimePoint now) {
        Announce a;
        a.name = config.name;
        a.lease_ms = static_cast<std::uint32_t>(config.lease.count());
        for (const auto& entry : writers) {
            const LocalWriter& w = entry.second;
            a.endpoints.push_back(EndpointInfo{EndpointKind::Writer, w.id, w.topic, w.type, w.qos});
        }
        for (const auto& entry : readers) {
            const LocalReader& r = entry.second;
            a.endpoints.push_back(EndpointInfo{EndpointKind::Reader, r.id, r.topic, r.type, r.qos});
        }
        send_packet(Packet{id, std::move(a)});
        next_announce = now + config.announce_period;
    }

    void emit(Event event, Deferred& out) {
        if (!event_handler) return;
        out.push_back([h = event_handler, e = std::move(event)] { (*h)(e); });
    }

    std::string remote_name(std::uint64_t pid) const {
        const auto it = remotes.find(pid);
        return it == remotes.end() ? std::string{} : it->second.info.name;
    }

    void deliver(LocalReader& r, const SampleInfo& info, const Payload& payload, Deferred& out) {
        ++r.stats.received;
        out.push_back([h = r.handler, payload, info] { (*h)(*payload, info); });
    }

    void note_lost(LocalReader& r, std::uint64_t pid, std::uint64_t count, Deferred& out) {
        if (count == 0) return;
        r.stats.lost += count;
        emit(Event{EventKind::SamplesLost, pid, remote_name(pid), r.topic, count}, out);
    }

    bool has_remote_reader(const LocalWriter& w) const {
        for (const auto& entry : remotes) {
            for (const EndpointInfo& e : entry.second.info.endpoints) {
                if (e.kind == EndpointKind::Reader && e.topic == w.topic && e.type_name == w.type &&
                    compatible(w.qos, e.qos)) {
                    return true;
                }
            }
        }
        return false;
    }

    // ---- reception ------------------------------------------------------------

    void drain_pending(LocalReader& r, const WriterKey& key, RemoteWriterState& st, Deferred& out) {
        while (!st.pending.empty() && st.pending.begin()->first == st.next_seq) {
            auto node = st.pending.extract(st.pending.begin());
            deliver(r, SampleInfo{key.first, key.second, node.key(), node.mapped().timestamp_ns},
                    node.mapped().payload, out);
            ++st.next_seq;
        }
    }

    void maybe_nack(LocalReader& r, const WriterKey& key, RemoteWriterState& st, TimePoint now) {
        if (st.known_last < st.next_seq || now < st.next_nack) return;
        Nack nack{key.first, key.second, {}};
        for (std::uint64_t s = st.next_seq; s <= st.known_last && nack.seqs.size() < kMaxNackSeqs; ++s) {
            if (st.pending.find(s) == st.pending.end()) nack.seqs.push_back(s);
        }
        if (nack.seqs.empty()) return;
        send_packet(Packet{id, std::move(nack)});
        st.next_nack = now + config.nack_interval;
        ++r.stats.nacks_sent;
    }

    void handle_data(std::uint64_t pid, Data& d, TimePoint now, Deferred& out) {
        const Payload payload = std::make_shared<const Bytes>(std::move(d.payload));
        for (auto& entry : readers) {
            LocalReader& r = entry.second;
            if (r.topic != d.topic || r.type != d.type_name) continue;
            if (r.qos.reliability == Reliability::Reliable && !d.reliable) continue;

            const WriterKey key{pid, d.writer_id};
            auto [it, inserted] = r.remotes.try_emplace(key);
            RemoteWriterState& st = it->second;
            if (inserted) st.next_seq = d.seq;
            st.last_rx = now;
            st.deadline_flagged = false;
            const SampleInfo info{pid, d.writer_id, d.seq, d.timestamp_ns};

            if (r.qos.reliability == Reliability::BestEffort) {
                if (d.seq < st.next_seq) {
                    ++r.stats.duplicates;
                    continue;
                }
                note_lost(r, pid, d.seq - st.next_seq, out);
                st.next_seq = d.seq + 1;
                deliver(r, info, payload, out);
                continue;
            }

            st.known_last = std::max(st.known_last, d.seq);
            if (d.seq < st.next_seq || st.pending.count(d.seq) != 0) {
                ++r.stats.duplicates;
            } else if (d.seq == st.next_seq) {
                deliver(r, info, payload, out);
                ++st.next_seq;
                drain_pending(r, key, st, out);
            } else if (st.pending.size() < kMaxPending) {
                st.pending.emplace(d.seq, Pending{d.timestamp_ns, payload});
            }
            maybe_nack(r, key, st, now);
        }
    }

    void handle_heartbeat(std::uint64_t pid, const Heartbeat& h, TimePoint now, Deferred& out) {
        for (auto& entry : readers) {
            LocalReader& r = entry.second;
            if (r.qos.reliability != Reliability::Reliable) continue;
            if (r.topic != h.topic || r.type != h.type_name) continue;

            const WriterKey key{pid, h.writer_id};
            auto [it, inserted] = r.remotes.try_emplace(key);
            RemoteWriterState& st = it->second;
            if (inserted) {
                st.next_seq = h.last_seq + 1;
                st.last_rx = now;
            }
            if (h.first_seq > st.next_seq) {
                note_lost(r, pid, h.first_seq - st.next_seq, out);
                st.next_seq = h.first_seq;
                st.pending.erase(st.pending.begin(), st.pending.lower_bound(h.first_seq));
                drain_pending(r, key, st, out);
            }
            st.known_last = std::max(st.known_last, h.last_seq);
            maybe_nack(r, key, st, now);
        }
    }

    void handle_nack(const Nack& n) {
        const auto it = writers.find(n.writer_id);
        if (it == writers.end()) return;
        LocalWriter& w = it->second;
        for (const std::uint64_t seq : n.seqs) {
            if (w.history.empty() || seq < w.history.front().seq || seq > w.history.back().seq) continue;
            const StoredSample& s = w.history[static_cast<std::size_t>(seq - w.history.front().seq)];
            send_packet(Packet{id, Data{w.id, s.seq, s.timestamp_ns, true, w.topic, w.type, *s.payload}});
            ++w.stats.retransmitted;
        }
    }

    void drop_remote(std::uint64_t pid, Deferred& out) {
        const auto it = remotes.find(pid);
        if (it == remotes.end()) return;
        emit(Event{EventKind::ParticipantLost, pid, it->second.info.name, {}, 0}, out);
        remotes.erase(it);
        for (auto& entry : readers) {
            auto& states = entry.second.remotes;
            for (auto s = states.begin(); s != states.end();) {
                s = s->first.first == pid ? states.erase(s) : std::next(s);
            }
        }
    }

    void handle_announce(std::uint64_t pid, Announce& a, TimePoint now, Deferred& out) {
        const auto it = remotes.find(pid);
        const bool is_new = it == remotes.end();
        RemoteEntry& entry = is_new ? remotes[pid] : it->second;
        entry.info.id = pid;
        entry.info.name = a.name;
        entry.info.lease = std::chrono::milliseconds{a.lease_ms};
        entry.info.endpoints = std::move(a.endpoints);
        entry.last_seen = now;
        if (is_new) {
            emit(Event{EventKind::ParticipantDiscovered, pid, entry.info.name, {}, 0}, out);
            next_announce = now;  // answer newcomers promptly
        }
    }

    void handle_packet(Packet& p, TimePoint now, Deferred& out) {
        if (p.participant_id == id) return;  // own multicast echo
        const auto known = remotes.find(p.participant_id);
        if (known != remotes.end()) known->second.last_seen = now;

        if (auto* a = std::get_if<Announce>(&p.body)) {
            handle_announce(p.participant_id, *a, now, out);
        } else if (std::holds_alternative<Bye>(p.body)) {
            drop_remote(p.participant_id, out);
        } else if (auto* d = std::get_if<Data>(&p.body)) {
            handle_data(p.participant_id, *d, now, out);
        } else if (const auto* h = std::get_if<Heartbeat>(&p.body)) {
            handle_heartbeat(p.participant_id, *h, now, out);
        } else if (const auto* n = std::get_if<Nack>(&p.body)) {
            if (n->target == id) handle_nack(*n);
        }
    }

    // ---- periodic work --------------------------------------------------------

    void run_timers(TimePoint now, Deferred& out) {
        if (now >= next_announce) send_announce(now);

        for (auto& entry : writers) {
            LocalWriter& w = entry.second;
            if (w.qos.reliability != Reliability::Reliable || w.history.empty() || now < w.next_heartbeat) continue;
            w.next_heartbeat = now + config.heartbeat_period;
            if (has_remote_reader(w)) {
                send_packet(Packet{id, Heartbeat{w.id, w.history.front().seq, w.history.back().seq, w.topic, w.type}});
            }
        }

        std::vector<std::uint64_t> expired;
        for (const auto& entry : remotes) {
            if (now - entry.second.last_seen > entry.second.info.lease) expired.push_back(entry.first);
        }
        for (const std::uint64_t pid : expired) drop_remote(pid, out);

        for (auto& entry : readers) {
            LocalReader& r = entry.second;
            for (auto& state : r.remotes) {
                RemoteWriterState& st = state.second;
                if (r.qos.reliability == Reliability::Reliable) maybe_nack(r, state.first, st, now);
                if (r.qos.deadline.count() > 0 && !st.deadline_flagged && now - st.last_rx > r.qos.deadline) {
                    st.deadline_flagged = true;
                    ++r.stats.deadline_missed;
                    emit(Event{EventKind::DeadlineMissed, state.first.first, remote_name(state.first.first), r.topic, 1},
                         out);
                }
            }
        }
    }

    // ---- public operations ----------------------------------------------------

    std::size_t spin_once(std::chrono::milliseconds timeout) {
        std::optional<Bytes> datagram = transport->receive(timeout);
        std::size_t processed = 0;
        Deferred out;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const TimePoint now = clock->now();
            while (datagram) {
                ++processed;
                if (auto packet = decode(*datagram)) {
                    ++stats.packets_in;
                    handle_packet(*packet, now, out);
                } else {
                    ++stats.malformed;
                }
                if (processed >= kMaxBatch) break;
                datagram = transport->receive(std::chrono::milliseconds{0});
            }
            run_timers(now, out);
        }
        for (auto& f : out) f();
        return processed;
    }

    std::uint64_t write(std::uint32_t writer_id, Bytes data) {
        if (data.size() > kMaxPayload) throw std::length_error("sample exceeds kMaxPayload");
        Deferred out;
        std::uint64_t seq = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = writers.find(writer_id);
            if (it == writers.end()) throw std::invalid_argument("unknown writer");
            LocalWriter& w = it->second;
            const std::int64_t ts = timestamp_ns(clock->now());
            seq = w.next_seq++;
            ++w.stats.published;
            const Payload payload = std::make_shared<const Bytes>(std::move(data));
            const bool reliable = w.qos.reliability == Reliability::Reliable;
            if (reliable) {
                w.history.push_back(StoredSample{seq, ts, payload});
                while (w.history.size() > w.qos.history_depth) w.history.pop_front();
            }
            for (auto& entry : readers) {
                LocalReader& r = entry.second;
                if (r.topic == w.topic && r.type == w.type && compatible(w.qos, r.qos)) {
                    deliver(r, SampleInfo{id, w.id, seq, ts}, payload, out);
                }
            }
            if (has_remote_reader(w)) {
                send_packet(Packet{id, Data{w.id, seq, ts, reliable, w.topic, w.type, *payload}});
            }
        }
        for (auto& f : out) f();
        return seq;
    }

    std::size_t matched_readers(std::uint32_t writer_id) const {
        std::lock_guard<std::mutex> lock(mutex);
        const LocalWriter& w = find_writer(writer_id);
        std::size_t n = 0;
        for (const auto& entry : readers) {
            const LocalReader& r = entry.second;
            if (r.topic == w.topic && r.type == w.type && compatible(w.qos, r.qos)) ++n;
        }
        for (const auto& entry : remotes) {
            for (const EndpointInfo& e : entry.second.info.endpoints) {
                if (e.kind == EndpointKind::Reader && e.topic == w.topic && e.type_name == w.type &&
                    compatible(w.qos, e.qos)) {
                    ++n;
                }
            }
        }
        return n;
    }

    std::size_t matched_writers(std::uint32_t reader_id) const {
        std::lock_guard<std::mutex> lock(mutex);
        const LocalReader& r = find_reader(reader_id);
        std::size_t n = 0;
        for (const auto& entry : writers) {
            const LocalWriter& w = entry.second;
            if (r.topic == w.topic && r.type == w.type && compatible(w.qos, r.qos)) ++n;
        }
        for (const auto& entry : remotes) {
            for (const EndpointInfo& e : entry.second.info.endpoints) {
                if (e.kind == EndpointKind::Writer && e.topic == r.topic && e.type_name == r.type &&
                    compatible(e.qos, r.qos)) {
                    ++n;
                }
            }
        }
        return n;
    }

    const LocalWriter& find_writer(std::uint32_t writer_id) const {
        const auto it = writers.find(writer_id);
        if (it == writers.end()) throw std::invalid_argument("unknown writer");
        return it->second;
    }

    const LocalReader& find_reader(std::uint32_t reader_id) const {
        const auto it = readers.find(reader_id);
        if (it == readers.end()) throw std::invalid_argument("unknown reader");
        return it->second;
    }
};

Participant::Participant(std::unique_ptr<Transport> transport, Config config)
    : impl_(std::make_unique<Impl>(std::move(transport), std::move(config))) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->send_announce(impl_->clock->now());
}

Participant::~Participant() {
    stop();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->send_packet(Packet{impl_->id, Bye{}});
}

void Participant::set_event_handler(EventHandler handler) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->event_handler = handler ? std::make_shared<const EventHandler>(std::move(handler)) : nullptr;
}

void Participant::start() {
    if (impl_->thread.joinable()) return;
    impl_->running = true;
    impl_->thread = std::thread([impl = impl_.get()] {
        while (impl->running) impl->spin_once(std::chrono::milliseconds{5});
    });
}

void Participant::stop() {
    impl_->running = false;
    if (impl_->thread.joinable()) impl_->thread.join();
}

std::size_t Participant::spin_once(std::chrono::milliseconds timeout) { return impl_->spin_once(timeout); }

std::uint64_t Participant::id() const noexcept { return impl_->id; }

std::vector<RemoteParticipant> Participant::remote_participants() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const TimePoint now = impl_->clock->now();
    std::vector<RemoteParticipant> out;
    for (const auto& entry : impl_->remotes) {
        RemoteParticipant p = entry.second.info;
        p.age = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.second.last_seen);
        out.push_back(std::move(p));
    }
    return out;
}

ParticipantStats Participant::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

std::uint32_t Participant::add_writer(const std::string& topic, const std::string& type_name, Qos qos) {
    validate_endpoint(topic, type_name, qos);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    LocalWriter w;
    w.id = impl_->next_endpoint_id++;
    w.topic = topic;
    w.type = type_name;
    w.qos = qos;
    const std::uint32_t id = w.id;
    impl_->writers.emplace(id, std::move(w));
    impl_->send_announce(impl_->clock->now());
    return id;
}

void Participant::remove_writer(std::uint32_t writer_id) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->writers.erase(writer_id) != 0) impl_->send_announce(impl_->clock->now());
}

std::uint64_t Participant::write(std::uint32_t writer_id, Bytes payload) {
    return impl_->write(writer_id, std::move(payload));
}

std::uint32_t Participant::add_reader(const std::string& topic, const std::string& type_name, Qos qos,
                                      RawHandler handler) {
    validate_endpoint(topic, type_name, qos);
    if (!handler) throw std::invalid_argument("reader handler must not be empty");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    LocalReader r;
    r.id = impl_->next_endpoint_id++;
    r.topic = topic;
    r.type = type_name;
    r.qos = qos;
    r.handler = std::make_shared<const RawHandler>(std::move(handler));
    const std::uint32_t id = r.id;
    impl_->readers.emplace(id, std::move(r));
    impl_->send_announce(impl_->clock->now());
    return id;
}

void Participant::remove_reader(std::uint32_t reader_id) noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->readers.erase(reader_id) != 0) impl_->send_announce(impl_->clock->now());
}

std::size_t Participant::matched_readers(std::uint32_t writer_id) const { return impl_->matched_readers(writer_id); }

std::size_t Participant::matched_writers(std::uint32_t reader_id) const { return impl_->matched_writers(reader_id); }

WriterStats Participant::writer_stats(std::uint32_t writer_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->find_writer(writer_id).stats;
}

ReaderStats Participant::reader_stats(std::uint32_t reader_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->find_reader(reader_id).stats;
}

}  // namespace ldds
