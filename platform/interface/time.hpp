// platform/interface/time.hpp — rivet::time PAL
// Portable time queries and duration utilities.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace rivet::time {

// ─── Clock types ─────────────────────────────────────────────────────────────

using Clock     = std::chrono::steady_clock;
using WallClock = std::chrono::system_clock;
using Duration  = std::chrono::nanoseconds;
using TimePoint = Clock::time_point;
using WallTime  = WallClock::time_point;

// ─── Queries ──────────────────────────────────────────────────────────────────

/// Monotonic timestamp — use for measuring elapsed time. Never use for
/// wall-clock dates; prefer wall_now() for that.
[[nodiscard]] inline TimePoint now() noexcept {
    return Clock::now();
}

/// Wall-clock time — suitable for display and file timestamps.
[[nodiscard]] inline WallTime wall_now() noexcept {
    return WallClock::now();
}

/// Elapsed nanoseconds since a previous TimePoint.
[[nodiscard]] inline Duration elapsed(TimePoint from) noexcept {
    return Clock::now() - from;
}

// ─── Conversion helpers ───────────────────────────────────────────────────────

/// Convert a wall-clock time to Unix epoch seconds (suitable for SQLite storage).
[[nodiscard]] inline int64_t to_unix_sec(WallTime t) noexcept {
    using namespace std::chrono;
    return duration_cast<seconds>(t.time_since_epoch()).count();
}

/// Construct a WallTime from Unix epoch seconds.
[[nodiscard]] inline WallTime from_unix_sec(int64_t sec) noexcept {
    using namespace std::chrono;
    return WallTime{seconds{sec}};
}

// ─── Formatting ───────────────────────────────────────────────────────────────

/// Human-readable duration string, e.g. "1.23s", "450ms", "2m 3s".
[[nodiscard]] std::string format_duration(Duration d);

/// Human-readable wall-clock string (ISO-8601 local, no TZ): "2024-01-15 09:30:00".
[[nodiscard]] std::string format_wall(WallTime t);

// ─── Inline implementations ──────────────────────────────────────────────────

inline std::string format_duration(Duration d) {
    using namespace std::chrono;
    auto ns  = d.count();
    if (ns < 0) return "0ns";
    if (ns < 1'000)          return std::to_string(ns) + "ns";
    if (ns < 1'000'000)      return std::to_string(ns / 1'000) + "µs";
    if (ns < 1'000'000'000)  return std::to_string(ns / 1'000'000) + "ms";

    auto sec  = duration_cast<seconds>(d).count();
    auto min  = sec / 60;
    sec       = sec % 60;
    if (min == 0) {
        // sub-minute: show decimal seconds
        auto ms = duration_cast<milliseconds>(d).count() % 1000;
        return std::to_string(sec) + "." + std::to_string(ms / 100) + "s";
    }
    auto hr = min / 60;
    min     = min % 60;
    if (hr == 0) return std::to_string(min) + "m " + std::to_string(sec) + "s";
    return std::to_string(hr) + "h " + std::to_string(min) + "m";
}

inline std::string format_wall(WallTime t) {
    std::time_t tt = WallClock::to_time_t(t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
    return buf;
}

} // namespace rivet::time
