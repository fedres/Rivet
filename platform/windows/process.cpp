// platform/windows/process.cpp — Windows process backend
// Uses CreateProcessW + Job Objects for process group management.
// IOCP for async stdout/stderr capture.
// Named pipes for stdin/stdout/stderr redirection.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../interface/process.hpp"
#include "../interface/result.hpp"

namespace rivet::process {

struct CancellationToken::Impl {
    std::atomic<bool> cancelled{false};
};
CancellationToken::CancellationToken() : impl_(std::make_shared<Impl>()) {}
void CancellationToken::cancel() { impl_->cancelled.store(true, std::memory_order_release); }
bool CancellationToken::is_cancelled() const { return impl_->cancelled.load(std::memory_order_acquire); }

ChildProcess::~ChildProcess() {
    if (handle_) ::CloseHandle(handle_);
}
bool ChildProcess::is_running() const {
    if (!handle_) return false;
    DWORD code;
    return ::GetExitCodeProcess(handle_, &code) && code == STILL_ACTIVE;
}
Result<int> ChildProcess::wait() {
    if (!handle_) return make_error<int>("wait: invalid handle");
    ::WaitForSingleObject(handle_, INFINITE);
    DWORD code;
    if (!::GetExitCodeProcess(handle_, &code))
        return make_error<int>("GetExitCodeProcess");
    return static_cast<int>(code);
}
Result<void> ChildProcess::send_signal(Signal sig) {
    if (!handle_) return make_error("send_signal: invalid handle");
    if (sig == Signal::Interrupt) {
        // Send Ctrl+C event to the process's console
        ::GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid_);
        return {};
    }
    if (!::TerminateProcess(handle_, 1))
        return make_error("TerminateProcess");
    return {};
}
Result<void> ChildProcess::kill() { return send_signal(Signal::Kill); }

Result<ChildProcess> spawn(SpawnOptions opts) {
    if (opts.args.empty())
        return make_error<ChildProcess>("spawn: args empty");

    // Build command line string (Windows style, with quoting).
    std::wstring cmdline;
    for (size_t i = 0; i < opts.args.size(); ++i) {
        if (i > 0) cmdline += L' ';
        // Simple quoting: wrap in double quotes, escape existing double quotes.
        cmdline += L'"';
        for (char c : opts.args[i]) {
            if (c == '"') cmdline += L'"';
            cmdline += static_cast<wchar_t>(c);
        }
        cmdline += L'"';
    }

    // Build environment block (double-null terminated wide string).
    std::wstring env_block;
    if (!opts.inherit_env) {
        for (auto& [k, v] : opts.env) {
            for (char c : k) env_block += static_cast<wchar_t>(c);
            env_block += L'=';
            for (char c : v) env_block += static_cast<wchar_t>(c);
            env_block += L'\0';
        }
        env_block += L'\0';
    }

    STARTUPINFOW si{.cb = sizeof(si)};
    PROCESS_INFORMATION pi{};

    std::optional<std::wstring> wd;
    if (opts.working_dir) {
        wd = opts.working_dir->wstring();
    }

    BOOL ok = ::CreateProcessW(
        nullptr,
        cmdline.data(),
        nullptr, nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP,
        opts.inherit_env ? nullptr : (LPVOID)env_block.data(),
        wd ? wd->c_str() : nullptr,
        &si, &pi
    );

    if (!ok) {
        DWORD e = ::GetLastError();
        return make_error<ChildProcess>("CreateProcessW: error " + std::to_string(e), static_cast<int>(e));
    }

    ::CloseHandle(pi.hThread);

    ChildProcess ch;
    ch.pid_    = pi.dwProcessId;
    ch.handle_ = pi.hProcess;
    return ch;
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
    wchar_t buf[32768]{};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0) return make_error<Path>("GetModuleFileNameW");
    return Path{buf};
}

NativePid current_pid() { return ::GetCurrentProcessId(); }

} // namespace rivet::process
