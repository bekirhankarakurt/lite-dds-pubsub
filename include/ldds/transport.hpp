#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ldds/bytes.hpp"

namespace ldds {

/// Datagram channel shared by all participants of a domain.
/// receive() must only be called from one thread at a time; send() is thread-safe.
class Transport {
public:
    virtual ~Transport() = default;

    /// Returns false when the datagram could not be sent.
    [[nodiscard]] virtual bool send(const Bytes& datagram) = 0;

    /// Waits up to `timeout` for the next datagram.
    [[nodiscard]] virtual std::optional<Bytes> receive(std::chrono::milliseconds timeout) = 0;
};

struct UdpConfig {
    std::string group{"239.255.42.99"};
    std::uint16_t port{7447};
    /// Local interface address used for multicast. Loopback keeps traffic on this host.
    std::string interface_addr{"127.0.0.1"};
    int ttl{1};
};

/// UDP multicast transport (POSIX). Throws std::runtime_error when the socket cannot be set up.
[[nodiscard]] std::unique_ptr<Transport> make_udp_transport(const UdpConfig& config);

/// In-process "network": every datagram is delivered to every connected endpoint
/// (including the sender, like multicast loopback). Used for simulation and tests.
class MemoryBus : public std::enable_shared_from_this<MemoryBus> {
public:
    /// Return true to drop the datagram. Invoked under the bus lock; must not call into the bus.
    using DropFilter = std::function<bool(const Bytes&)>;

    [[nodiscard]] static std::shared_ptr<MemoryBus> create();

    [[nodiscard]] std::unique_ptr<Transport> connect();
    void set_drop_filter(DropFilter filter);

    [[nodiscard]] std::uint64_t datagrams_sent() const;
    [[nodiscard]] std::uint64_t datagrams_dropped() const;

private:
    struct Endpoint;
    class Port;

    MemoryBus() = default;
    void deliver(const Bytes& datagram);
    void detach(const std::shared_ptr<Endpoint>& endpoint);

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Endpoint>> endpoints_;
    DropFilter filter_;
    std::uint64_t sent_{0};
    std::uint64_t dropped_{0};
};

}  // namespace ldds
