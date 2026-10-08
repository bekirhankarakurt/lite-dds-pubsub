#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

#include "ldds/participant.hpp"
#include "ldds/serialization.hpp"

namespace ldds {

/// Typed writer endpoint. Unregisters itself on destruction.
template <class T>
class Publisher {
public:
    Publisher() = default;
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
    Publisher(Publisher&& o) noexcept
        : owner_(std::exchange(o.owner_, nullptr)), id_(o.id_), topic_(std::move(o.topic_)) {}
    Publisher& operator=(Publisher&& o) noexcept {
        if (this != &o) {
            reset();
            owner_ = std::exchange(o.owner_, nullptr);
            id_ = o.id_;
            topic_ = std::move(o.topic_);
        }
        return *this;
    }
    ~Publisher() { reset(); }

    /// Publishes a sample; returns its sequence number.
    /// Throws std::length_error if the serialized sample exceeds kMaxPayload.
    std::uint64_t write(const T& value) { return owner_->write(id_, serialize(value)); }

    /// Remote and local readers currently matched to this writer.
    [[nodiscard]] std::size_t matched_subscribers() const { return owner_->matched_readers(id_); }
    [[nodiscard]] WriterStats stats() const { return owner_->writer_stats(id_); }
    [[nodiscard]] const std::string& topic() const noexcept { return topic_; }

private:
    friend class Participant;
    Publisher(Participant* owner, std::uint32_t id, std::string topic)
        : owner_(owner), id_(id), topic_(std::move(topic)) {}

    void reset() noexcept {
        if (owner_ != nullptr) owner_->remove_writer(id_);
        owner_ = nullptr;
    }

    Participant* owner_{nullptr};
    std::uint32_t id_{0};
    std::string topic_;
};

template <class T>
struct Sample {
    T value;
    SampleInfo info;
};

/// Typed reader endpoint. With a handler, samples are pushed to it; without one
/// they queue (newest `history_depth` kept) until take()n.
template <class T>
class Subscriber {
public:
    using Handler = std::function<void(const T&, const SampleInfo&)>;

    Subscriber() = default;
    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber(Subscriber&& o) noexcept
        : owner_(std::exchange(o.owner_, nullptr)),
          id_(o.id_),
          topic_(std::move(o.topic_)),
          state_(std::move(o.state_)) {}
    Subscriber& operator=(Subscriber&& o) noexcept {
        if (this != &o) {
            reset();
            owner_ = std::exchange(o.owner_, nullptr);
            id_ = o.id_;
            topic_ = std::move(o.topic_);
            state_ = std::move(o.state_);
        }
        return *this;
    }
    ~Subscriber() { reset(); }

    /// Oldest queued sample, if any.
    [[nodiscard]] std::optional<Sample<T>> take() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->queue.empty()) return std::nullopt;
        Sample<T> s = std::move(state_->queue.front());
        state_->queue.pop_front();
        return s;
    }

    [[nodiscard]] std::vector<Sample<T>> take_all() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::vector<Sample<T>> out(std::make_move_iterator(state_->queue.begin()),
                                   std::make_move_iterator(state_->queue.end()));
        state_->queue.clear();
        return out;
    }

    [[nodiscard]] std::size_t matched_publishers() const { return owner_->matched_writers(id_); }
    [[nodiscard]] ReaderStats stats() const { return owner_->reader_stats(id_); }
    /// Samples whose payload failed to deserialize.
    [[nodiscard]] std::uint64_t decode_errors() const noexcept { return state_->decode_errors.load(); }
    [[nodiscard]] const std::string& topic() const noexcept { return topic_; }

private:
    friend class Participant;

    struct State {
        State(std::size_t depth_, Handler handler_) : depth(depth_), handler(std::move(handler_)) {}

        void on_raw(const Bytes& bytes, const SampleInfo& info) {
            std::optional<T> value;
            try {
                ByteReader r(bytes);
                value.emplace(Serializer<T>::read(r));
            } catch (const DecodeError&) {
                ++decode_errors;
                return;
            }
            if (handler) {
                handler(*value, info);
                return;
            }
            std::lock_guard<std::mutex> lock(mutex);
            queue.push_back(Sample<T>{std::move(*value), info});
            while (queue.size() > depth) queue.pop_front();
        }

        const std::size_t depth;
        const Handler handler;
        std::mutex mutex;
        std::deque<Sample<T>> queue;
        std::atomic<std::uint64_t> decode_errors{0};
    };

    Subscriber(Participant* owner, std::uint32_t id, std::string topic, std::shared_ptr<State> state)
        : owner_(owner), id_(id), topic_(std::move(topic)), state_(std::move(state)) {}

    void reset() noexcept {
        if (owner_ != nullptr) owner_->remove_reader(id_);
        owner_ = nullptr;
    }

    Participant* owner_{nullptr};
    std::uint32_t id_{0};
    std::string topic_;
    std::shared_ptr<State> state_;
};

template <class T>
Publisher<T> Participant::create_publisher(const std::string& topic, Qos qos) {
    const std::uint32_t id = add_writer(topic, Serializer<T>::type_name(), qos);
    return Publisher<T>(this, id, topic);
}

template <class T>
Subscriber<T> Participant::create_subscriber(const std::string& topic, Qos qos,
                                             std::function<void(const T&, const SampleInfo&)> handler) {
    auto state = std::make_shared<typename Subscriber<T>::State>(qos.history_depth, std::move(handler));
    const std::uint32_t id = add_reader(topic, Serializer<T>::type_name(), qos,
                                        [state](const Bytes& bytes, const SampleInfo& info) { state->on_raw(bytes, info); });
    return Subscriber<T>(this, id, topic, std::move(state));
}

}  // namespace ldds
