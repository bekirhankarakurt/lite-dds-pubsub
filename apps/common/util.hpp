#pragma once

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <string>

namespace app {

inline std::atomic<bool>& stop_requested() {
    static std::atomic<bool> flag{false};
    return flag;
}

/// Makes Ctrl-C request a clean shutdown instead of killing the process.
inline void install_signal_handlers() {
    const auto handler = [](int) { stop_requested().store(true); };
    std::signal(SIGINT, handler);
    std::signal(SIGTERM, handler);
}

/// "HH:MM:SS.mmm" in UTC for a wall-clock timestamp in nanoseconds since the epoch.
inline std::string format_time(std::int64_t ns) {
    const std::time_t secs = static_cast<std::time_t>(ns / 1'000'000'000);
    const int millis = static_cast<int>((ns / 1'000'000) % 1000);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, millis);
    return buf;
}

}  // namespace app
