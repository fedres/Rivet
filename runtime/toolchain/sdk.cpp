// runtime/toolchain/sdk.cpp — host SDK detection
#include "sdk.hpp"
#include "../../platform/interface/process.hpp"
#include "../../platform/interface/env.hpp"
#include "../../platform/interface/fs.hpp"

#include <mutex>

namespace rivet::toolchain {

namespace {

#if defined(__APPLE__)
SdkInfo detect_apple() {
    SdkInfo s;
    s.hint =
        "Install the Xcode Command Line Tools (one-time, ~1 GB):\n"
        "\n"
        "    xcode-select --install\n"
        "\n"
        "Rivet ships its own clang and libc++, but Apple forbids\n"
        "redistributing the macOS SDK — the platform frameworks and\n"
        "system headers have to come from the user's machine. Every\n"
        "other C/C++ build tool on macOS (cargo, bun, zig, brew) needs\n"
        "this too.";

    rivet::process::SpawnOptions opts;
    opts.args = {"xcrun", "--show-sdk-path"};
    opts.inherit_env    = true;
    opts.capture_stdout = true;
    opts.capture_stderr = true;
    auto r = rivet::process::run(std::move(opts));
    if (!r || r->exit_code != 0) return s;

    std::string out = std::move(r->stdout_output);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    if (out.empty()) return s;
    if (!rivet::fs::exists(Path{out}).value_or(false)) return s;

    s.present = true;
    s.path    = std::move(out);
    return s;
}
#endif

#if defined(_WIN32)
// Windows SDK detection: clang-cl needs INCLUDE / LIB / PATH set the way
// vcvarsall.bat configures them. The simplest reliable check is that
// `INCLUDE` env contains both the MSVC include dir and the Windows Kits
// "um" (user-mode) include dir. msvc-dev-cmd@v1 / vcvarsall both populate
// these; a developer who's run "Developer Command Prompt" gets them too.
SdkInfo detect_windows() {
    SdkInfo s;
    s.hint =
        "Install Visual Studio Build Tools 2022 (one-time, ~6 GB):\n"
        "\n"
        "    winget install --id Microsoft.VisualStudio.2022.BuildTools \\\n"
        "      --override \"--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended\"\n"
        "\n"
        "Then open \"x64 Native Tools Command Prompt for VS 2022\" and re-run\n"
        "rivet. Rivet ships its own clang-cl / lld-link, but the Windows SDK\n"
        "(kernel32.lib, windows.h, the UCRT headers) is governed by\n"
        "Microsoft's licence and cannot be redistributed.\n"
        "\n"
        "Detection looks for these env vars (set by vcvarsall.bat / Developer\n"
        "Command Prompt):\n"
        "  INCLUDE  must contain a Windows Kits 'um' include dir\n"
        "  LIB      must contain a Windows Kits 'um' lib dir\n"
        "\n"
        "Set RIVET_SKIP_SDK_CHECK=1 to override (e.g. for custom toolchains).";

    if (rivet::env::get("RIVET_SKIP_SDK_CHECK").value_or("") == "1") {
        s.present = true;
        return s;
    }

    auto include = rivet::env::get("INCLUDE").value_or("");
    auto lib     = rivet::env::get("LIB").value_or("");
    // Crude but reliable: vcvars sets these to multi-entry paths that
    // include "Windows Kits" — that string is unique to the SDK.
    bool inc_ok = include.find("Windows Kits") != std::string::npos
               || include.find("Microsoft Visual Studio") != std::string::npos;
    bool lib_ok = lib.find("Windows Kits") != std::string::npos
               || lib.find("Microsoft Visual Studio") != std::string::npos;
    if (inc_ok && lib_ok) {
        s.present = true;
        // We don't have a single "SDK path" on Windows — INCLUDE is the
        // authoritative locator. Stash it in `path` for diagnostics.
        s.path = include;
    }
    return s;
}
#endif

} // namespace

const SdkInfo& detect_host_sdk() {
    static std::once_flag once;
    static SdkInfo cached;
    std::call_once(once, [] {
#if defined(__APPLE__)
        cached = detect_apple();
#elif defined(_WIN32)
        cached = detect_windows();
#else
        // Linux: glibc/musl + kernel headers are part of the distro.
        // No actionable detection beyond "is /usr/include there?" — we
        // treat it as always-present for now.
        cached.present = true;
#endif
    });
    return cached;
}

} // namespace rivet::toolchain
