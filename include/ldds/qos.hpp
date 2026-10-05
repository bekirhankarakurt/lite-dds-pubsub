#pragma once

#include <chrono>
#include <cstdint>

namespace ldds {

enum class Reliability : std::uint8_t {
    BestEffort = 0,  ///< fire and forget; gaps are counted but never repaired
    Reliable = 1     ///< in-order delivery, gaps repaired via NACK + retransmit
};

/// Per-endpoint quality of service.
struct Qos {
    Reliability reliability{Reliability::BestEffort};
    /// Writer: samples retained for retransmit. Reader: samples queued for take().
    std::uint16_t history_depth{16};
    /// Reader only: maximum silence per matched writer; zero disables the check.
    std::chrono::milliseconds deadline{0};

    [[nodiscard]] static Qos best_effort(std::uint16_t depth = 16) {
        return Qos{Reliability::BestEffort, depth, std::chrono::milliseconds{0}};
    }

    [[nodiscard]] static Qos reliable(std::uint16_t depth = 16) {
        return Qos{Reliability::Reliable, depth, std::chrono::milliseconds{0}};
    }

    [[nodiscard]] Qos with_deadline(std::chrono::milliseconds d) const {
        Qos q = *this;
        q.deadline = d;
        return q;
    }
};

[[nodiscard]] inline const char* to_string(Reliability r) noexcept {
    return r == Reliability::Reliable ? "reliable" : "best-effort";
}

}  // namespace ldds
