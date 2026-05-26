// rivet/platform/interface/process.hpp
// Process spawning Platform Abstraction Layer.
//
// RULES:
//   - Build systems spawn thousands of processes; this must be fast.
//   - Never use system() or popen() — they inherit the full shell environment.
//   - Always use inherit_env = false for build jobs; set environment explicitly.
//   - Cancellation kills the entire process group/job tree, not just the root.
//   - All output must be captured asynchronously to prevent pipe deadlocks.
//
// Platform implementations:
//   platform/linux/process.cpp   (posix_spawn / io_uring / pidfd)
//   platform/macos/process.cpp   (posix_spawn / kqueue)
//   platform/windows/process.cpp (CreateProcessW / IOCP / Job Objects)
#pragma once

#include "result.hpp"
#include "types.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rivet::process {

// Cancellation token — passed through task trees to support cooperative
// cancellation of entire build graphs.
class CancellationToken {
public:
    CancellationToken();
    void cancel();
    [[nodiscard]] bool is_cancelled() const;
    // Returns a child token that is also cancelled when this one is.
    [[nodiscard]] CancellationToken make_child() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// ─── Spawn options ────────────────────────────────────────────────────────────

struct SpawnOptions {
    // Executable + arguments.
    std::vector<std::string> args;

    // Working directory. Defaults to current directory if not set.
    std::optional<Path> working_dir;

    // Environment variables for the child process.
    std::unordered_map<std::string, std::string> env;

    // If false (default), child inherits NONE of the parent environment.
    // Always false for build jobs — set only what is needed.
    bool inherit_env = false;

    // Capture stdout/stderr into ChildProcess::stdout_output / stderr_output.
    bool capture_stdout = true;
    bool capture_stderr = true;

    // Merge stderr into stdout stream.
    bool merge_stderr = false;

    // Kill the process after this duration.
    std::optional<std::chrono::milliseconds> timeout;

    // Cancellation token. Process is killed if token is cancelled.
    std::optional<CancellationToken> cancel_token;
};

// ─── Async output reader ─────────────────────────────────────────────────────

class AsyncOutputReader {
public:
    virtual ~AsyncOutputReader() = default;

    void on_line(std::function<void(std::string_view)> cb) { line_cb_ = std::move(cb); }
    void on_close(std::function<void()> cb)                { close_cb_ = std::move(cb); }

    virtual void start() = 0;
    virtual void stop()  = 0;

protected:
    std::function<void(std::string_view)> line_cb_;
    std::function<void()>                 close_cb_;
};

// ─── Child process ───────────────────────────────────────────────────────────

class ChildProcess {
public:
    ChildProcess() = default;

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) noexcept;
    ChildProcess& operator=(ChildProcess&&) noexcept;

    ~ChildProcess();

    [[nodiscard]] NativePid    pid()        const { return pid_; }
    [[nodiscard]] bool         is_running() const;

    // Block until process exits. Returns exit code.
    [[nodiscard]] Result<int>  wait();

    // Send signal / kill.
    [[nodiscard]] Result<void> send_signal(Signal sig);
    [[nodiscard]] Result<void> kill();   // Signal::Kill

    // Write to stdin of the process (if stdin was not redirected to /dev/null).
    [[nodiscard]] Result<void> write_stdin(ByteSpan data);

    // Async readers for stdout/stderr (only valid if captured).
    AsyncOutputReader* stdout_reader();
    AsyncOutputReader* stderr_reader();

    // If capture was enabled: accumulated output after wait() returns.
    const std::string& stdout_output() const { return stdout_buf_; }
    const std::string& stderr_output() const { return stderr_buf_; }

private:
    friend Result<ChildProcess> spawn(SpawnOptions opts);

    NativePid    pid_{};
    NativeHandle handle_{};  // Win32 HANDLE; unused on POSIX
    std::string  stdout_buf_;
    std::string  stderr_buf_;

    // POSIX: pipe read-ends for captured output; -1 = not open.
    // Closed and drained inside wait().
#if !defined(_WIN32)
    int stdout_rd_ = -1;
    int stderr_rd_ = -1;
#endif
};

// ─── Spawn ───────────────────────────────────────────────────────────────────

[[nodiscard]] Result<ChildProcess> spawn(SpawnOptions opts);

// Convenience: run a process to completion and return exit code + output.
struct RunResult {
    int         exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

[[nodiscard]] Result<RunResult> run(SpawnOptions opts);

// ─── Self ─────────────────────────────────────────────────────────────────────

// Path to the currently running executable.
[[nodiscard]] Result<Path> self_exe();

// Current process's PID.
[[nodiscard]] NativePid current_pid();

} // namespace rivet::process
