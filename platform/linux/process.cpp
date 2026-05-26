// platform/linux/process.cpp — Linux process backend
// Uses fork/execve. Pipe-based stdout/stderr capture drained in wait() via select().
// Process groups ensure cancellation kills the whole tree.
// pidfd (kernel 5.3+) / io_uring (kernel 5.1+) are future optimisations.
#include "../interface/process.hpp"
#include "../interface/result.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <atomic>

extern char** environ;

namespace rivet::process {

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
    CancellationToken child;
    child.impl_ = impl_;
    return child;
}

// ─── ChildProcess ─────────────────────────────────────────────────────────────

namespace {
    void close_fd(int& fd) {
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

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
            if (r <= 0) break;

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
    o.pid_ = {}; o.handle_ = {};
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

    std::vector<char*> argv;
    argv.reserve(opts.args.size() + 1);
    for (auto& a : opts.args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    std::vector<std::string> env_strs;
    std::vector<char*>       envp;
    for (auto& [k, v] : opts.env) env_strs.push_back(k + "=" + v);
    if (opts.inherit_env) {
        for (char** ep = environ; *ep; ++ep) {
            std::string_view entry{*ep};
            auto eq = entry.find('=');
            if (eq == std::string_view::npos) continue;
            auto key = std::string(entry.substr(0, eq));
            if (opts.env.find(key) == opts.env.end())
                env_strs.push_back(std::string(entry));
        }
    }
    for (auto& e : env_strs) envp.push_back(const_cast<char*>(e.c_str()));
    envp.push_back(nullptr);

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    auto cleanup_pipes = [&] {
        for (int fd : stdout_pipe) if (fd >= 0) ::close(fd);
        for (int fd : stderr_pipe) if (fd >= 0) ::close(fd);
    };

    if (opts.capture_stdout) {
        if (::pipe2(stdout_pipe, O_CLOEXEC) != 0) {
            return make_error<ChildProcess>(std::string("pipe2 stdout: ") + std::strerror(errno), errno);
        }
    }
    if (opts.capture_stderr && !opts.merge_stderr) {
        if (::pipe2(stderr_pipe, O_CLOEXEC) != 0) {
            cleanup_pipes();
            return make_error<ChildProcess>(std::string("pipe2 stderr: ") + std::strerror(errno), errno);
        }
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        int saved = errno;
        cleanup_pipes();
        return make_error<ChildProcess>(std::string("fork: ") + std::strerror(saved), saved);
    }

    if (pid == 0) {
        if (opts.capture_stdout && stdout_pipe[1] >= 0) {
            ::dup2(stdout_pipe[1], STDOUT_FILENO);
        }
        if (opts.merge_stderr) {
            ::dup2(STDOUT_FILENO, STDERR_FILENO);
        } else if (opts.capture_stderr && stderr_pipe[1] >= 0) {
            ::dup2(stderr_pipe[1], STDERR_FILENO);
        }
        ::setpgid(0, 0);
        if (opts.working_dir) ::chdir(opts.working_dir->c_str());
        ::execve(argv[0], argv.data(), envp.data());
        ::_exit(127);
    }

    // Parent: close write-ends.
    if (stdout_pipe[1] >= 0) { ::close(stdout_pipe[1]); stdout_pipe[1] = -1; }
    if (stderr_pipe[1] >= 0) { ::close(stderr_pipe[1]); stderr_pipe[1] = -1; }

    ChildProcess ch;
    ch.pid_       = pid;
    ch.stdout_rd_ = stdout_pipe[0];
    ch.stderr_rd_ = stderr_pipe[0];
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
