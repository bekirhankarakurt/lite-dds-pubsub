#include "ldds/wire.hpp"

#include <array>
#include <stdexcept>

namespace ldds {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{0x4C, 0x44, 0x44, 0x53};  // "LDDS"
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kFlagReliable = 0x01;
constexpr std::size_t kMinEndpointBytes = 16;

std::uint16_t checked_u16(std::size_t n) {
    if (n > 0xFFFFU) throw std::length_error("too many elements for u16 count");
    return static_cast<std::uint16_t>(n);
}

void write_body(ByteWriter& w, const Announce& a) {
    w.str(a.name);
    w.u32(a.lease_ms);
    w.u16(checked_u16(a.endpoints.size()));
    for (const EndpointInfo& e : a.endpoints) {
        w.u8(static_cast<std::uint8_t>(e.kind));
        w.u32(e.id);
        w.str(e.topic);
        w.str(e.type_name);
        w.u8(static_cast<std::uint8_t>(e.qos.reliability));
        w.u16(e.qos.history_depth);
        w.u32(static_cast<std::uint32_t>(e.qos.deadline.count()));
    }
}

void write_body(ByteWriter&, const Bye&) {}

void write_body(ByteWriter& w, const Data& d) {
    w.u32(d.writer_id);
    w.u64(d.seq);
    w.i64(d.timestamp_ns);
    w.str(d.topic);
    w.str(d.type_name);
    w.u32(static_cast<std::uint32_t>(d.payload.size()));
    w.raw(d.payload);
}

void write_body(ByteWriter& w, const Heartbeat& h) {
    w.u32(h.writer_id);
    w.u64(h.first_seq);
    w.u64(h.last_seq);
    w.str(h.topic);
    w.str(h.type_name);
}

void write_body(ByteWriter& w, const Nack& n) {
    w.u64(n.target);
    w.u32(n.writer_id);
    w.u16(checked_u16(n.seqs.size()));
    for (const std::uint64_t s : n.seqs) w.u64(s);
}

Reliability read_reliability(ByteReader& r) {
    const std::uint8_t v = r.u8();
    if (v > 1) throw DecodeError("invalid reliability");
    return static_cast<Reliability>(v);
}

Announce read_announce(ByteReader& r) {
    Announce a;
    a.name = r.str();
    a.lease_ms = r.u32();
    const std::size_t count = r.u16();
    if (count * kMinEndpointBytes > r.remaining()) throw DecodeError("endpoint count exceeds payload");
    a.endpoints.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        EndpointInfo e;
        const std::uint8_t kind = r.u8();
        if (kind > 1) throw DecodeError("invalid endpoint kind");
        e.kind = static_cast<EndpointKind>(kind);
        e.id = r.u32();
        e.topic = r.str();
        e.type_name = r.str();
        e.qos.reliability = read_reliability(r);
        e.qos.history_depth = r.u16();
        e.qos.deadline = std::chrono::milliseconds{r.u32()};
        a.endpoints.push_back(std::move(e));
    }
    return a;
}

Data read_data(ByteReader& r, std::uint8_t flags) {
    Data d;
    d.reliable = (flags & kFlagReliable) != 0;
    d.writer_id = r.u32();
    d.seq = r.u64();
    d.timestamp_ns = r.i64();
    d.topic = r.str();
    d.type_name = r.str();
    d.payload = r.raw(r.u32());
    return d;
}

Heartbeat read_heartbeat(ByteReader& r) {
    Heartbeat h;
    h.writer_id = r.u32();
    h.first_seq = r.u64();
    h.last_seq = r.u64();
    h.topic = r.str();
    h.type_name = r.str();
    return h;
}

Nack read_nack(ByteReader& r) {
    Nack n;
    n.target = r.u64();
    n.writer_id = r.u32();
    const std::size_t count = r.u16();
    if (count * 8 > r.remaining()) throw DecodeError("nack count exceeds payload");
    n.seqs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) n.seqs.push_back(r.u64());
    return n;
}

}  // namespace

Bytes encode(const Packet& packet) {
    ByteWriter w;
    for (const std::uint8_t m : kMagic) w.u8(m);
    w.u8(kVersion);
    w.u8(static_cast<std::uint8_t>(packet.body.index() + 1));
    const auto* data = std::get_if<Data>(&packet.body);
    w.u8(data != nullptr && data->reliable ? kFlagReliable : std::uint8_t{0});
    w.u8(0);  // reserved
    w.u64(packet.participant_id);
    std::visit([&w](const auto& body) { write_body(w, body); }, packet.body);
    if (w.data().size() > kMaxDatagram) throw std::length_error("datagram exceeds kMaxDatagram");
    return w.take();
}

std::optional<Packet> decode(const Bytes& datagram) {
    try {
        ByteReader r(datagram);
        for (const std::uint8_t m : kMagic) {
            if (r.u8() != m) return std::nullopt;
        }
        if (r.u8() != kVersion) return std::nullopt;
        const std::uint8_t type = r.u8();
        const std::uint8_t flags = r.u8();
        r.skip(1);
        Packet p;
        p.participant_id = r.u64();
        switch (type) {
            case 1: p.body = read_announce(r); break;
            case 2: p.body = Bye{}; break;
            case 3: p.body = read_data(r, flags); break;
            case 4: p.body = read_heartbeat(r); break;
            case 5: p.body = read_nack(r); break;
            default: return std::nullopt;
        }
        return p;
    } catch (const DecodeError&) {
        return std::nullopt;
    }
}

const char* packet_name(const Body& body) noexcept {
    switch (body.index()) {
        case 0: return "ANNOUNCE";
        case 1: return "BYE";
        case 2: return "DATA";
        case 3: return "HEARTBEAT";
        default: return "NACK";
    }
}

}  // namespace ldds
