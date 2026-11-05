#include <condition_variable>
#include <deque>

#include "ldds/transport.hpp"

namespace ldds {

struct MemoryBus::Endpoint {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Bytes> queue;
};

class MemoryBus::Port final : public Transport {
public:
    Port(std::shared_ptr<MemoryBus> bus, std::shared_ptr<Endpoint> endpoint)
        : bus_(std::move(bus)), endpoint_(std::move(endpoint)) {}

    ~Port() override { bus_->detach(endpoint_); }

    bool send(const Bytes& datagram) override {
        bus_->deliver(datagram);
        return true;
    }

    std::optional<Bytes> receive(std::chrono::milliseconds timeout) override {
        std::unique_lock<std::mutex> lock(endpoint_->mutex);
        if (endpoint_->queue.empty() && timeout.count() > 0) {
            endpoint_->cv.wait_for(lock, timeout, [this] { return !endpoint_->queue.empty(); });
        }
        if (endpoint_->queue.empty()) return std::nullopt;
        Bytes datagram = std::move(endpoint_->queue.front());
        endpoint_->queue.pop_front();
        return datagram;
    }

private:
    std::shared_ptr<MemoryBus> bus_;
    std::shared_ptr<Endpoint> endpoint_;
};

std::shared_ptr<MemoryBus> MemoryBus::create() { return std::shared_ptr<MemoryBus>(new MemoryBus()); }

std::unique_ptr<Transport> MemoryBus::connect() {
    auto endpoint = std::make_shared<Endpoint>();
    {
        std::scoped_lock lock(mutex_);
        endpoints_.push_back(endpoint);
    }
    return std::make_unique<Port>(shared_from_this(), std::move(endpoint));
}

void MemoryBus::set_drop_filter(DropFilter filter) {
    std::scoped_lock lock(mutex_);
    filter_ = std::move(filter);
}

std::uint64_t MemoryBus::datagrams_sent() const {
    std::scoped_lock lock(mutex_);
    return sent_;
}

std::uint64_t MemoryBus::datagrams_dropped() const {
    std::scoped_lock lock(mutex_);
    return dropped_;
}

void MemoryBus::deliver(const Bytes& datagram) {
    std::scoped_lock lock(mutex_);
    ++sent_;
    if (filter_ && filter_(datagram)) {
        ++dropped_;
        return;
    }
    for (const auto& endpoint : endpoints_) {
        {
            std::scoped_lock el(endpoint->mutex);
            endpoint->queue.push_back(datagram);
        }
        endpoint->cv.notify_one();
    }
}

void MemoryBus::detach(const std::shared_ptr<Endpoint>& endpoint) {
    std::scoped_lock lock(mutex_);
    for (auto it = endpoints_.begin(); it != endpoints_.end(); ++it) {
        if (*it == endpoint) {
            endpoints_.erase(it);
            return;
        }
    }
}

}  // namespace ldds
