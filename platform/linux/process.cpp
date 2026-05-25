// platform/linux/process.cpp — Linux process backend
// Uses posix_spawn for fast process creation.
// Uses pidfd (kernel 5.3+) for reliable process tracking; falls back to waitpid.
// Async I/O via io_uring (kernel 5.1+); falls back to epoll/read.
#include "../interface/process.hpp"
#include "../interface/result.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>

// Forward: full async I/O integration and pidfd support is TODO Phase 0.

namespace rivet::process {

// ─── CancellationToken ────────────────────────────────────────────────────────

struct CancellationToken::Impl {
    std::atomic<bool> cancelled{false};
};

CancellationToken::CancellationToken() : impl_(std::make_shared<Impl>()) {}

void CancellationToken::cancel() { impl_->cancelled.store(true, std::memory_order_release); }

bool CancellationToken::is_cancelled() const {
    return impl_->cancelled.load(std::memory_order_acquire);
}

// ─── ChildProcess ─────────────────────────────────────────────────────────────

ChildProcess::~ChildProcess() {
    if (pid_ > 0) {
        // Reap zombie if not yet waited.
        int status;
        ::waitpid(pid_, &status, WNOHANG);
    }
}

bool ChildProcess::is_running() const {
    if (pid_ <= 0) return false;
    return ::kill(pid_, 0) == 0;
}

Result<int> ChildProcess::wait() {
    if (pid_ <= 0) return make_error<int>("wait: invalid pid");
    int status = 0;
    pid_t r;
    do {
        r = ::waitpid(pid_, &status, 0);
    } while (r == -1 && errno == EINTR);
    if (r == -1)
        return make_error<int>(std::string("waitpid: ") + std::strerror(errno), errno);
    pid_ = 0;
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return make_error<int>("waitpid: unexpected status");
}

Result<void> ChildProcess::send_signal(Signal sig) {
    if (pid_ <= 0) return make_error("send_signal: invalid pid");
    int s;
    switch (sig) {
        case Signal::Terminate: s = SIGTERM; break;
        case Signal::Kill:      s = SIGKILL; break;
        case Signal::Interrupt: s = SIGINT;  break;
    }
    if (::kill(pid_, s) != 0)
        return make_error(std::string("kill: ") + std::strerror(errno), errno);
    return {};
}

Result<void> ChildProcess::kill() { return send_signal(Signal::Kill); }

// ─── Spawn ────────────────────────────────────────────────────────────────────

Result<ChildProcess> spawn(SpawnOptions opts) {
    if (opts.args.empty())
        return make_error<ChildProcess>("spawn: args must not be empty");

    // Build argv
    std::vector<char*> argv;
    argv.reserve(opts.args.size() + 1);
    for (auto& a : opts.args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    // Build envp
    std::vector<std::string> env_strs;
    std::vector<char*>       envp;
    if (opts.inherit_env) {
        // Start from current env, override with opts.env.
        // TODO: merge with environ
    }
    for (auto& [k, v] : opts.env) {
        env_strs.push_back(k + "=" + v);
    }
    for (auto& e : env_strs) envp.push_back(const_cast<char*>(e.c_str()));
    envp.push_back(nullptr);

    pid_t pid;
    // TODO: set up stdout/stderr pipes, working dir, timeouts.
    // For now: simple fork/exec bootstrap.
    pid = ::fork();
    if (pid < 0)
        return make_error<ChildProcess>(std::string("fork: ") + std::strerror(errno), errno);

    if (pid == 0) {
        // Child
        if (opts.working_dir) {
            if (::chdir(opts.working_dir->c_str()) != 0) ::_exit(127);
        }
        ::execve(argv[0], argv.data(), envp.data());
        ::_exit(127);
    }

    ChildProcess ch;
    ch.pid_ = pid;
    return ch;
}

Result<RunResult> run(SpawnOptions opts) {
    opts.capture_stdout = true;
    opts.capture_stderr = true;
    auto ch = spawn(std::move(opts));
    if (!ch) return propagate<RunResult>(ch);
    auto code = ch->wait();
    if (!code) return propagate<RunResult>(code);
    return RunResult{*code, ch->stdout_output(), ch->stderr_output()};
}

Result<Path> self_exe() {
    char buf[4096]{};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0)
        return make_error<Path>(std::string("readlink /proc/self/exe: ") + std::strerror(errno), errno);
    return Path{buf};
}

NativePid current_pid() { return ::getpid(); }

} // namespace rivet::process
