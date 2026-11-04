#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace ldds {

using TimePoint = std::chrono::steady_clock::time_point;

/// Time source. Injectable so lease, deadline and retransmit logic can be
/// tested deterministically.
class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual TimePoint now() const = 0;
};

class SteadyClock final : public Clock {
public:
    [[nodiscard]] TimePoint now() const override { return std::chrono::steady_clock::now(); }
};

/// Clock that only moves when told to.
class ManualClock final : public Clock {
public:
    [[nodiscard]] TimePoint now() const override {
        return TimePoint{std::chrono::nanoseconds{ns_.load(std::memory_order_relaxed)}};
    }

    void advance(std::chrono::milliseconds d) {
        ns_.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count(),
                      std::memory_order_relaxed);
    }

private:
    std::atomic<std::int64_t> ns_{1'000'000'000};
};

}  // namespace ldds
