// platform/macos/process.cpp — macOS process backend
// Uses fork/execve. Pipe-based stdout/stderr capture drained in wait() via select().
// Process groups ensure cancellation kills the whole tree.
#include "../interface/process.hpp"
#include "../interface/result.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>

extern char** environ;

namespace rivet::process {

namespace {

// Resolve a bare executable name against PATH so callers can pass "tar"
// instead of "/usr/bin/tar". We do this in the parent before fork() so we
// can use std::string safely (post-fork pre-exec is async-signal-safe only).
// If `argv[0]` already contains a '/', it's returned verbatim; otherwise we
// walk the PATH that the child will see (env-overrides win over `environ`).
std::string resolve_via_path(const std::string& name,
                             const std::vector<std::string>& envp_strs) {
    if (name.find('/') != std::string::npos) return name;
    std::string_view path;
    for (const auto& e : envp_strs) {
        if (e.starts_with("PATH=")) { path = std::string_view(e).substr(5); break; }
    }
    if (path.empty()) {
        if (const char* p = std::getenv("PATH")) path = p;
    }
    if (path.empty()) return name;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        size_t len = (end == std::string_view::npos ? path.size() : end) - start;
        std::string candidate;
        if (len == 0) candidate = name;
        else { candidate.assign(path.data() + start, len); candidate += '/'; candidate += name; }
        if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return name;  // not found — let execve fail with ENOENT
}

} // namespace

// ─── CancellationToken ────────────────────────────────────────────────────────

struct CancellationToken::Impl {
    std::atomic<bool> cancelled{false};
};

CancellationToken::CancellationToken() : impl_(std::make_shared<Impl>()) {}

void CancellationToken::cancel() {
    impl_->cancelled.store(true, std::memory_order_release);
}

bool CancellationToken::is_cancelled() const {
    return impl_->cancelled.load(std::memory_order_acquire);
}

CancellationToken CancellationToken::make_child() const {
    // Share the same Impl so cancelling the parent also cancels the child.
    CancellationToken child;
    child.impl_ = impl_;
    return child;
}

// ─── ChildProcess ─────────────────────────────────────────────────────────────

namespace {
    void close_fd(int& fd) {
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

    // Drain both pipe read-ends into the string buffers until both are closed.
    // Uses select() to avoid deadlock when both stdout and stderr fill up.
    void drain_pipes(int& stdout_rd, std::string& stdout_buf,
                     int& stderr_rd, std::string& stderr_buf) {
        char tmp[8192];
        while (stdout_rd >= 0 || stderr_rd >= 0) {
            fd_set fds;
            FD_ZERO(&fds);
            int maxfd = -1;
            if (stdout_rd >= 0) { FD_SET(stdout_rd, &fds); maxfd = stdout_rd; }
            if (stderr_rd >= 0) { FD_SET(stderr_rd, &fds); maxfd = std::max(maxfd, stderr_rd); }

            int r = ::select(maxfd + 1, &fds, nullptr, nullptr, nullptr);
            if (r <= 0) break;  // error or spurious wakeup

            if (stdout_rd >= 0 && FD_ISSET(stdout_rd, &fds)) {
                ssize_t n = ::read(stdout_rd, tmp, sizeof(tmp));
                if (n <= 0) close_fd(stdout_rd);
                else        stdout_buf.append(tmp, static_cast<size_t>(n));
            }
            if (stderr_rd >= 0 && FD_ISSET(stderr_rd, &fds)) {
                ssize_t n = ::read(stderr_rd, tmp, sizeof(tmp));
                if (n <= 0) close_fd(stderr_rd);
                else        stderr_buf.append(tmp, static_cast<size_t>(n));
            }
        }
    }
} // namespace

ChildProcess::~ChildProcess() {
    close_fd(stdout_rd_);
    close_fd(stderr_rd_);
    if (pid_ > 0) {
        int s;
        ::waitpid(pid_, &s, WNOHANG);
    }
}

ChildProcess::ChildProcess(ChildProcess&& o) noexcept
    : pid_(o.pid_), handle_(o.handle_),
      stdout_buf_(std::move(o.stdout_buf_)),
      stderr_buf_(std::move(o.stderr_buf_)),
      stdout_rd_(o.stdout_rd_),
      stderr_rd_(o.stderr_rd_)
{
    o.pid_ = {};  o.handle_ = {};
    o.stdout_rd_ = -1; o.stderr_rd_ = -1;
}

ChildProcess& ChildProcess::operator=(ChildProcess&& o) noexcept {
    if (this != &o) {
        close_fd(stdout_rd_);
        close_fd(stderr_rd_);
        if (pid_ > 0) { int s; ::waitpid(pid_, &s, WNOHANG); }
        pid_ = o.pid_;  handle_ = o.handle_;
        stdout_buf_ = std::move(o.stdout_buf_);
        stderr_buf_ = std::move(o.stderr_buf_);
        stdout_rd_ = o.stdout_rd_;  stderr_rd_ = o.stderr_rd_;
        o.pid_ = {}; o.handle_ = {};
        o.stdout_rd_ = -1; o.stderr_rd_ = -1;
    }
    return *this;
}

bool ChildProcess::is_running() const {
    return pid_ > 0 && ::kill(pid_, 0) == 0;
}

Result<int> ChildProcess::wait() {
    if (pid_ <= 0) return make_error<int>("wait: invalid pid");

    // Drain captured output before waiting to prevent pipe deadlock.
    drain_pipes(stdout_rd_, stdout_buf_, stderr_rd_, stderr_buf_);

    int status = 0;
    pid_t r;
    do { r = ::waitpid(pid_, &status, 0); } while (r == -1 && errno == EINTR);
    if (r == -1)
        return make_error<int>(std::string("waitpid: ") + std::strerror(errno), errno);
    pid_ = 0;
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return make_error<int>("waitpid: unexpected status");
}

Result<void> ChildProcess::send_signal(Signal sig) {
    if (pid_ <= 0) return make_error("send_signal: invalid pid");
    int s = (sig == Signal::Terminate) ? SIGTERM
          : (sig == Signal::Kill)      ? SIGKILL
          :                              SIGINT;
    // Kill the entire process group to avoid orphans.
    if (::killpg(pid_, s) != 0 && ::kill(pid_, s) != 0)
        return make_error(std::string("kill: ") + std::strerror(errno), errno);
    return {};
}

Result<void> ChildProcess::kill() { return send_signal(Signal::Kill); }

Result<void> ChildProcess::write_stdin(ByteSpan /*data*/) {
    return make_error("write_stdin: not yet implemented");
}

AsyncOutputReader* ChildProcess::stdout_reader() { return nullptr; }
AsyncOutputReader* ChildProcess::stderr_reader() { return nullptr; }

// ─── Spawn ────────────────────────────────────────────────────────────────────

Result<ChildProcess> spawn(SpawnOptions opts) {
    if (opts.args.empty())
        return make_error<ChildProcess>("spawn: args must not be empty");

    // Build argv.
    std::vector<char*> argv;
    argv.reserve(opts.args.size() + 1);
    for (auto& a : opts.args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    // Build envp.
    std::vector<std::string> env_strs;
    std::vector<char*>       envp;
    for (auto& [k, v] : opts.env) env_strs.push_back(k + "=" + v);
    for (auto& e : env_strs) envp.push_back(const_cast<char*>(e.c_str()));
    if (opts.inherit_env) {
        // Append parent environment entries not already overridden.
        for (char** ep = environ; *ep; ++ep) {
            std::string_view entry{*ep};
            auto eq = entry.find('=');
            if (eq == std::string_view::npos) continue;
            auto key = std::string(entry.substr(0, eq));
            if (opts.env.find(key) == opts.env.end()) {
                env_strs.push_back(std::string(entry));
            }
        }
        // Rebuild envp after appending inherited entries.
        envp.clear();
        for (auto& e : env_strs) envp.push_back(const_cast<char*>(e.c_str()));
    }
    envp.push_back(nullptr);

    // Resolve PATH before fork (std::string is not async-signal-safe).
    std::string resolved_exe = resolve_via_path(opts.args[0], env_strs);

    // Set up pipes for stdout/stderr capture.
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    auto cleanup_pipes = [&] {
        for (int fd : stdout_pipe) if (fd >= 0) ::close(fd);
        for (int fd : stderr_pipe) if (fd >= 0) ::close(fd);
    };

    if (opts.capture_stdout) {
        if (::pipe(stdout_pipe) != 0) {
            return make_error<ChildProcess>(std::string("pipe stdout: ") + std::strerror(errno), errno);
        }
    }
    if (opts.capture_stderr && !opts.merge_stderr) {
        if (::pipe(stderr_pipe) != 0) {
            cleanup_pipes();
            return make_error<ChildProcess>(std::string("pipe stderr: ") + std::strerror(errno), errno);
        }
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        int saved = errno;
        cleanup_pipes();
        return make_error<ChildProcess>(std::string("fork: ") + std::strerror(saved), saved);
    }

    if (pid == 0) {
        // Child process.
        if (opts.capture_stdout && stdout_pipe[1] >= 0) {
            ::dup2(stdout_pipe[1], STDOUT_FILENO);
            ::close(stdout_pipe[0]); ::close(stdout_pipe[1]);
        }
        if (opts.merge_stderr) {
            ::dup2(STDOUT_FILENO, STDERR_FILENO);
        } else if (opts.capture_stderr && stderr_pipe[1] >= 0) {
            ::dup2(stderr_pipe[1], STDERR_FILENO);
            ::close(stderr_pipe[0]); ::close(stderr_pipe[1]);
        }
        // Create new process group so we can kill the entire tree.
        ::setpgid(0, 0);
        if (opts.working_dir) ::chdir(opts.working_dir->c_str());
        ::execve(resolved_exe.c_str(), argv.data(), envp.data());
        ::_exit(127);
    }

    // Parent: close write-ends of the pipes.
    if (stdout_pipe[1] >= 0) { ::close(stdout_pipe[1]); stdout_pipe[1] = -1; }
    if (stderr_pipe[1] >= 0) { ::close(stderr_pipe[1]); stderr_pipe[1] = -1; }

    ChildProcess ch;
    ch.pid_       = pid;
    ch.stdout_rd_ = stdout_pipe[0];
    ch.stderr_rd_ = stderr_pipe[0];
    return ch;
}

// ─── run (convenience) ────────────────────────────────────────────────────────

Result<RunResult> run(SpawnOptions opts) {
    opts.capture_stdout = true;
    opts.capture_stderr = true;
    auto ch = spawn(std::move(opts));
    if (!ch) return propagate<RunResult>(ch);
    auto code = ch->wait();
    if (!code) return propagate<RunResult>(code);
    return RunResult{*code, ch->stdout_output(), ch->stderr_output()};
}

// ─── Self ─────────────────────────────────────────────────────────────────────

Result<Path> self_exe() {
    uint32_t size = 4096;
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        buf.resize(size);
        if (_NSGetExecutablePath(buf.data(), &size) != 0)
            return make_error<Path>("_NSGetExecutablePath failed");
    }
    buf.resize(std::strlen(buf.c_str()));
    return Path{buf};
}

NativePid current_pid() { return ::getpid(); }

} // namespace rivet::process
