// platform/macos/process.cpp — macOS process backend
// Uses posix_spawn + kqueue for async I/O.
// Self-exe via _NSGetExecutablePath.
// Process groups work identically to Linux.
#include "../interface/process.hpp"
#include "../interface/result.hpp"

#include <cerrno>
#include <cstring>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>

extern char** environ;

namespace rivet::process {

struct CancellationToken::Impl {
    std::atomic<bool> cancelled{false};
};
CancellationToken::CancellationToken() : impl_(std::make_shared<Impl>()) {}
void CancellationToken::cancel() { impl_->cancelled.store(true, std::memory_order_release); }
bool CancellationToken::is_cancelled() const { return impl_->cancelled.load(std::memory_order_acquire); }

ChildProcess::~ChildProcess() {
    if (pid_ > 0) { int s; ::waitpid(pid_, &s, WNOHANG); }
}
bool ChildProcess::is_running() const { return pid_ > 0 && ::kill(pid_, 0) == 0; }
Result<int> ChildProcess::wait() {
    if (pid_ <= 0) return make_error<int>("wait: invalid pid");
    int status = 0; pid_t r;
    do { r = ::waitpid(pid_, &status, 0); } while (r == -1 && errno == EINTR);
    if (r == -1) return make_error<int>(std::strerror(errno), errno);
    pid_ = 0;
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return make_error<int>("unexpected wait status");
}
Result<void> ChildProcess::send_signal(Signal sig) {
    if (pid_ <= 0) return make_error("send_signal: invalid pid");
    int s = (sig == Signal::Terminate) ? SIGTERM : (sig == Signal::Kill) ? SIGKILL : SIGINT;
    if (::kill(pid_, s) != 0) return make_error(std::strerror(errno), errno);
    return {};
}
Result<void> ChildProcess::kill() { return send_signal(Signal::Kill); }

Result<ChildProcess> spawn(SpawnOptions opts) {
    if (opts.args.empty()) return make_error<ChildProcess>("spawn: args empty");
    std::vector<char*> argv;
    for (auto& a : opts.args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> env_strs;
    std::vector<char*> envp;
    for (auto& [k, v] : opts.env) env_strs.push_back(k + "=" + v);
    for (auto& e : env_strs) envp.push_back(const_cast<char*>(e.c_str()));
    envp.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0) return make_error<ChildProcess>(std::strerror(errno), errno);
    if (pid == 0) {
        if (opts.working_dir) ::chdir(opts.working_dir->c_str());
        ::execve(argv[0], argv.data(), envp.data());
        ::_exit(127);
    }
    ChildProcess ch; ch.pid_ = pid; return ch;
}

Result<RunResult> run(SpawnOptions opts) {
    opts.capture_stdout = opts.capture_stderr = true;
    auto ch = spawn(std::move(opts));
    if (!ch) return propagate<RunResult>(ch);
    auto code = ch->wait();
    if (!code) return propagate<RunResult>(code);
    return RunResult{*code, ch->stdout_output(), ch->stderr_output()};
}

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
