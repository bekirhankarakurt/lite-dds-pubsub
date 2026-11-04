#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ldds/bytes.hpp"
#include "ldds/clock.hpp"
#include "ldds/qos.hpp"
#include "ldds/transport.hpp"
#include "ldds/wire.hpp"

namespace ldds {

template <class T>
class Publisher;
template <class T>
class Subscriber;

struct Config {
    std::string name{"participant"};
    std::uint64_t id{0};  ///< 0 picks a random id
    std::chrono::milliseconds announce_period{1000};
    std::chrono::milliseconds lease{3500};  ///< silence after which a peer is declared lost
    std::chrono::milliseconds heartbeat_period{100};
    std::chrono::milliseconds nack_interval{20};
    std::shared_ptr<Clock> clock;  ///< null selects SteadyClock
};

/// Metadata delivered with every sample.
struct SampleInfo {
    std::uint64_t participant_id{0};
    std::uint32_t writer_id{0};
    std::uint64_t seq{0};
    std::int64_t timestamp_ns{0};  ///< writer clock; comparable only on the same host
};

enum class EventKind : std::uint8_t { ParticipantDiscovered, ParticipantLost, DeadlineMissed, SamplesLost };

struct Event {
    EventKind kind{EventKind::ParticipantDiscovered};
    std::uint64_t remote_id{0};
    std::string remote_name;
    std::string topic;       ///< empty for participant events
    std::uint64_t count{0};  ///< samples lost, for SamplesLost
};

[[nodiscard]] const char* to_string(EventKind kind) noexcept;

struct RemoteParticipant {
    std::uint64_t id{0};
    std::string name;
    std::chrono::milliseconds lease{0};
    std::chrono::milliseconds age{0};  ///< time since the last packet
    std::vector<EndpointInfo> endpoints;
};

struct WriterStats {
    std::uint64_t published{0};
    std::uint64_t retransmitted{0};
};

struct ReaderStats {
    std::uint64_t received{0};
    std::uint64_t lost{0};
    std::uint64_t duplicates{0};
    std::uint64_t deadline_missed{0};
    std::uint64_t nacks_sent{0};
};

struct ParticipantStats {
    std::uint64_t packets_in{0};
    std::uint64_t packets_out{0};
    std::uint64_t malformed{0};
    std::uint64_t send_failures{0};
};

/// A node in the domain: owns the transport, performs discovery, and routes
/// samples between local and remote endpoints.
///
/// Threading: call spin_once() yourself, or start() a background I/O thread
/// (do not mix the two). Handlers run on whichever thread spins and are
/// invoked without internal locks held, so they may publish.
/// The participant must outlive every Publisher/Subscriber created from it.
class Participant {
public:
    using EventHandler = std::function<void(const Event&)>;
    using RawHandler = std::function<void(const Bytes&, const SampleInfo&)>;

    explicit Participant(std::unique_ptr<Transport> transport, Config config = {});
    ~Participant();
    Participant(const Participant&) = delete;
    Participant& operator=(const Participant&) = delete;

    void set_event_handler(EventHandler handler);

    void start();
    void stop();

    /// Receives and processes pending datagrams (waiting up to `timeout` for the first),
    /// then runs periodic work. Returns the number of datagrams processed.
    std::size_t spin_once(std::chrono::milliseconds timeout);

    [[nodiscard]] std::uint64_t id() const noexcept;
    [[nodiscard]] std::vector<RemoteParticipant> remote_participants() const;
    [[nodiscard]] ParticipantStats stats() const;

    template <class T>
    [[nodiscard]] Publisher<T> create_publisher(const std::string& topic, Qos qos = {});

    template <class T>
    [[nodiscard]] Subscriber<T> create_subscriber(const std::string& topic, Qos qos = {},
                                                  std::function<void(const T&, const SampleInfo&)> handler = {});

    // Type-erased endpoint API used by Publisher/Subscriber.
    [[nodiscard]] std::uint32_t add_writer(const std::string& topic, const std::string& type_name, Qos qos);
    void remove_writer(std::uint32_t writer_id) noexcept;
    std::uint64_t write(std::uint32_t writer_id, Bytes payload);
    [[nodiscard]] std::uint32_t add_reader(const std::string& topic, const std::string& type_name, Qos qos,
                                           RawHandler handler);
    void remove_reader(std::uint32_t reader_id) noexcept;

    [[nodiscard]] std::size_t matched_readers(std::uint32_t writer_id) const;
    [[nodiscard]] std::size_t matched_writers(std::uint32_t reader_id) const;
    [[nodiscard]] WriterStats writer_stats(std::uint32_t writer_id) const;
    [[nodiscard]] ReaderStats reader_stats(std::uint32_t reader_id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ldds
