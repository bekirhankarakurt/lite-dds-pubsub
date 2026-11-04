#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ldds/bytes.hpp"
#include "ldds/qos.hpp"

namespace ldds {

/// Largest datagram the library will emit or accept.
inline constexpr std::size_t kMaxDatagram = 65000;
/// Largest serialized sample that is guaranteed to fit a DATA datagram.
inline constexpr std::size_t kMaxPayload = kMaxDatagram - 600;

enum class EndpointKind : std::uint8_t { Writer = 0, Reader = 1 };

struct EndpointInfo {
    EndpointKind kind{EndpointKind::Writer};
    std::uint32_t id{0};
    std::string topic;
    std::string type_name;
    Qos qos;
};

/// Discovery: periodic participant advertisement (also renews the lease).
struct Announce {
    std::string name;
    std::uint32_t lease_ms{0};
    std::vector<EndpointInfo> endpoints;
};

/// Graceful departure.
struct Bye {};

struct Data {
    std::uint32_t writer_id{0};
    std::uint64_t seq{0};
    std::int64_t timestamp_ns{0};
    bool reliable{false};
    std::string topic;
    std::string type_name;
    Bytes payload;
};

/// Reliable writers advertise the range of samples they can still retransmit.
struct Heartbeat {
    std::uint32_t writer_id{0};
    std::uint64_t first_seq{0};
    std::uint64_t last_seq{0};
    std::string topic;
    std::string type_name;
};

/// Reader request for missing samples of one remote writer.
struct Nack {
    std::uint64_t target{0};
    std::uint32_t writer_id{0};
    std::vector<std::uint64_t> seqs;
};

using Body = std::variant<Announce, Bye, Data, Heartbeat, Nack>;

struct Packet {
    std::uint64_t participant_id{0};
    Body body;
};

/// Serialize a packet. Throws std::length_error when it exceeds kMaxDatagram.
[[nodiscard]] Bytes encode(const Packet& packet);

/// Parse a datagram; returns nullopt for anything that is not a valid packet.
[[nodiscard]] std::optional<Packet> decode(const Bytes& datagram);

[[nodiscard]] const char* packet_name(const Body& body) noexcept;

}  // namespace ldds
