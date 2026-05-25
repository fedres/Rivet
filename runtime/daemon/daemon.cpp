// runtime/daemon/daemon.cpp — Compiler daemon stub implementation
//
// Full daemon IPC is Phase 4. This file provides the lifecycle scaffolding
// so that `rivet daemon start|stop|status` and the auto-start path compile
// and link without error.
#include "daemon.hpp"
#include "../../platform/interface/process.hpp"
#include "../../platform/interface/env.hpp"
#include "../../platform/interface/terminal.hpp"

#include <format>
#include <iostream>

namespace rivet::daemon {

// ─── socket_path() ───────────────────────────────────────────────────────────

std::string socket_path(const Path& rivet_home) {
#if defined(_WIN32)
    return R"(\\.\pipe\rivet-daemon)";
#else
    return (rivet_home / "daemon.sock").string();
#endif
}

// ─── DaemonClient ────────────────────────────────────────────────────────────

DaemonClient::DaemonClient(Path rivet_home) : rivet_home_(std::move(rivet_home)) {}
DaemonClient::~DaemonClient() {
    if (socket_fd_ >= 0) {
#if !defined(_WIN32)
        ::close(socket_fd_);
#endif
        socket_fd_ = -1;
    }
}

Result<void> DaemonClient::connect() {
    // TODO Phase 4: open Unix socket / named pipe, retry with backoff,
    // call start_daemon_process() on ENOENT.
    return make_error<void>("daemon IPC not yet implemented (Phase 4)");
}

Result<DaemonResponse> DaemonClient::compile(const build::TaskNode& /*task*/) {
    return make_error<DaemonResponse>("daemon IPC not yet implemented (Phase 4)");
}

Result<DaemonMetrics> DaemonClient::status() {
    return make_error<DaemonMetrics>("daemon not running");
}

Result<void> DaemonClient::shutdown() {
    return make_error<void>("daemon not running");
}

Result<void> DaemonClient::send(const DaemonRequest& /*req*/) {
    return make_error<void>("not implemented");
}

Result<DaemonResponse> DaemonClient::recv() {
    return make_error<DaemonResponse>("not implemented");
}

Result<void> DaemonClient::start_daemon_process() {
    // Spawn `rivet daemon start --background` as a detached process.
    auto self = rivet::process::self_exe();
    if (!self) return propagate<void>(self);

    rivet::process::SpawnOptions opts;
    opts.args        = {"daemon", "start", "--background"};
    opts.inherit_env = false;

    auto child = rivet::process::spawn(opts);
    if (!child) return propagate<void>(child);

    // Do not wait — daemon runs independently.
    return {};
}

// ─── daemon_main() ───────────────────────────────────────────────────────────

int daemon_main(const Path& rivet_home) {
    std::cerr << "[rivet daemon] starting — socket: "
              << socket_path(rivet_home) << "\n";

    // TODO Phase 4:
    //   1. Bind the Unix socket / named pipe.
    //   2. Enter accept loop.
    //   3. For each connection: deserialise DaemonRequest, dispatch to worker.
    //   4. Self-terminate after kDefaultIdleTimeout of inactivity.

    std::cerr << "[rivet daemon] daemon IPC not yet implemented (Phase 4)\n";
    return 0;
}

} // namespace rivet::daemon
