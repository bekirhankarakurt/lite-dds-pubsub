#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "ldds/transport.hpp"
#include "ldds/wire.hpp"

namespace ldds {
namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("udp transport: " + what + ": " + std::strerror(errno));
}

class Fd {
public:
    explicit Fd(int fd) noexcept : fd_(fd) {}
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() {
        if (fd_ >= 0) ::close(fd_);
    }
    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_;
};

in_addr parse_addr(const std::string& text, const char* what) {
    in_addr addr{};
    if (::inet_pton(AF_INET, text.c_str(), &addr) != 1) {
        throw std::runtime_error(std::string("udp transport: invalid ") + what + " address '" + text + "'");
    }
    return addr;
}

template <class T>
void set_option(int fd, int level, int name, const T& value, const char* what) {
    if (::setsockopt(fd, level, name, &value, sizeof value) != 0) fail(what);
}

class UdpTransport final : public Transport {
public:
    explicit UdpTransport(const UdpConfig& cfg) : fd_(::socket(AF_INET, SOCK_DGRAM, 0)), buffer_(65536) {
        if (fd_.get() < 0) fail("socket");
        const in_addr group = parse_addr(cfg.group, "group");
        const in_addr iface = parse_addr(cfg.interface_addr, "interface");

        const int one = 1;
        set_option(fd_.get(), SOL_SOCKET, SO_REUSEADDR, one, "SO_REUSEADDR");
#ifdef SO_REUSEPORT
        set_option(fd_.get(), SOL_SOCKET, SO_REUSEPORT, one, "SO_REUSEPORT");
#endif
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(cfg.port);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(fd_.get(), reinterpret_cast<const sockaddr*>(&local), sizeof local) != 0) fail("bind");

        ip_mreq membership{};
        membership.imr_multiaddr = group;
        membership.imr_interface = iface;
        set_option(fd_.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, membership, "IP_ADD_MEMBERSHIP");
        set_option(fd_.get(), IPPROTO_IP, IP_MULTICAST_IF, iface, "IP_MULTICAST_IF");
        const unsigned char ttl = static_cast<unsigned char>(cfg.ttl);
        set_option(fd_.get(), IPPROTO_IP, IP_MULTICAST_TTL, ttl, "IP_MULTICAST_TTL");
        const unsigned char loop = 1;
        set_option(fd_.get(), IPPROTO_IP, IP_MULTICAST_LOOP, loop, "IP_MULTICAST_LOOP");

        dest_.sin_family = AF_INET;
        dest_.sin_port = htons(cfg.port);
        dest_.sin_addr = group;
    }

    bool send(const Bytes& datagram) override {
        const ssize_t n = ::sendto(fd_.get(), datagram.data(), datagram.size(), 0,
                                   reinterpret_cast<const sockaddr*>(&dest_), sizeof dest_);
        return n == static_cast<ssize_t>(datagram.size());
    }

    std::optional<Bytes> receive(std::chrono::milliseconds timeout) override {
        pollfd pfd{};
        pfd.fd = fd_.get();
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (ready <= 0) return std::nullopt;
        const ssize_t n = ::recvfrom(fd_.get(), buffer_.data(), buffer_.size(), 0, nullptr, nullptr);
        if (n < 0) return std::nullopt;
        return Bytes(buffer_.begin(), buffer_.begin() + n);
    }

private:
    Fd fd_;
    sockaddr_in dest_{};
    Bytes buffer_;
};

}  // namespace

std::unique_ptr<Transport> make_udp_transport(const UdpConfig& config) {
    return std::make_unique<UdpTransport>(config);
}

}  // namespace ldds
