// runtime/daemon/daemon.hpp — Compiler daemon protocol and lifecycle
//
// The daemon amortises clang process startup costs for long build sessions
// by keeping warm clang instances alive between builds.
//
// ┌────────────────────────────────────────────┐
// │  rivet CLI                                  │
// │    cmd_build() → DaemonClient::compile()    │
// └──────────────┬─────────────────────────────┘
//                │  Unix socket / named pipe
//                │  length-prefixed binary protocol
// ┌──────────────▼─────────────────────────────┐
// │  rivet daemon (background process)          │
// │    DaemonServer: manages in-process clang   │
// │    job queue, PCH/module cache              │
// └────────────────────────────────────────────┘
#pragma once

#include "../build/ir.hpp"
#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace rivet::daemon {

// ─── Socket paths ─────────────────────────────────────────────────────────────

/// Platform-appropriate socket path.
///   Linux/macOS: ~/.rivet/daemon.sock
///   Windows:     \\.\pipe\rivet-daemon
[[nodiscard]] std::string socket_path(const Path& rivet_home);

// ─── Protocol types ───────────────────────────────────────────────────────────

enum class RequestKind : uint8_t {
    Ping      = 0,
    Compile   = 1,
    Status    = 2,
    Shutdown  = 3,
};

struct DaemonMetrics {
    uint64_t  jobs_completed  = 0;
    uint64_t  cache_hits      = 0;
    uint64_t  uptime_sec      = 0;
    uint32_t  queue_depth     = 0;
    uint32_t  worker_count    = 0;
};

struct DaemonRequest {
    RequestKind     kind       = RequestKind::Ping;
    uint64_t        request_id = 0;
    build::TaskNode task;       // valid only for Compile requests
};

struct DaemonResponse {
    uint64_t        request_id = 0;
    bool            success    = false;
    int             exit_code  = 0;
    std::string     stdout_out;
    std::string     stderr_out;
    DaemonMetrics   metrics;
    std::string     error_message;
};

// ─── Client ───────────────────────────────────────────────────────────────────

/// Connects to a running daemon and submits compile requests.
/// If the daemon is not running, auto-starts it.
class DaemonClient {
public:
    explicit DaemonClient(Path rivet_home);
    ~DaemonClient();

    // Non-copyable.
    DaemonClient(const DaemonClient&)            = delete;
    DaemonClient& operator=(const DaemonClient&) = delete;

    /// Ensure a daemon is running. Auto-starts one if needed.
    [[nodiscard]] Result<void> connect();

    /// Send a compile task and wait for the response.
    [[nodiscard]] Result<DaemonResponse> compile(const build::TaskNode& task);

    /// Query daemon health and metrics.
    [[nodiscard]] Result<DaemonMetrics> status();

    /// Ask the daemon to shut down gracefully.
    [[nodiscard]] Result<void> shutdown();

    [[nodiscard]] bool is_connected() const noexcept { return connected_; }

private:
    Path   rivet_home_;
    bool   connected_ = false;
    int    socket_fd_ = -1;    // POSIX fd; on Windows use a HANDLE variant

    [[nodiscard]] Result<void> send(const DaemonRequest& req);
    [[nodiscard]] Result<DaemonResponse> recv();
    [[nodiscard]] Result<void> start_daemon_process();
};

// ─── Server lifecycle ────────────────────────────────────────────────────────

/// Entry point for the daemon subprocess (called from cmd_daemon / auto-start).
/// Blocks until shutdown is requested.
int daemon_main(const Path& rivet_home);

// ─── Auto-idle shutdown ───────────────────────────────────────────────────────

/// Default idle timeout before the daemon self-terminates.
inline constexpr std::chrono::minutes kDefaultIdleTimeout{30};

} // namespace rivet::daemon
