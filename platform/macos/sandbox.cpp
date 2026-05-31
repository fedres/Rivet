// platform/macos/sandbox.cpp -- macOS sandboxing backend.
//
// HONEST STATUS: macOS enforcement is currently a no-op. We previously
// shipped a sandbox-exec(1) wrapper that translates SandboxPolicy into a
// TinyScheme profile (the function below still does so for unit tests),
// but macOS-14's sandbox-exec aborts (SIGABRT) when applying any profile
// with file-* path filters to our bundled clang++ binary. Empirical
// findings, established by repeated CI bisection:
//
//   - `(allow default)`                              -> works
//   - `(deny default) + (allow file*)` (unfiltered)  -> works
//   - `(deny default) + (allow file-read* (subpath X))` -> SIGABRT 134
//
// The same compact + split forms work fine in tests/platform/test_sandbox
// because those wrap /usr/bin/test (Apple-signed). We suspect macOS-14
// sandbox-exec refuses to apply file-path filters when the wrapped binary
// isn't system-signed, but that's not in any public Apple docs.
//
// sandbox-exec has been deprecated by Apple since 10.12; the modern
// replacement is the Endpoint Security framework (requires a system
// extension + entitlements). Wiring that up is a multi-week chunk well
// beyond D2's scope. Until that lands, spawn_sandboxed on macOS falls
// through to process::spawn unchanged.
//
// The Linux Landlock backend (platform/linux/sandbox.cpp) and the
// Windows Job Object backend (platform/windows/sandbox.cpp) are real.
#include "../interface/sandbox.hpp"

#include <string>
#include <utility>

namespace rivet::sandbox {

namespace {
// Escape `"` and `\` for an S-expression string literal. Used by
// build_macos_profile() so it round-trips correctly even when paths
// contain weird characters; kept here in case the Endpoint Security
// migration ends up reusing the same profile shape.
std::string escape_path(std::string_view p) {
    std::string s;
    s.reserve(p.size() + 4);
    for (char c : p) {
        if (c == '"' || c == '\\') s.push_back('\\');
        s.push_back(c);
    }
    return s;
}
} // namespace

// Public-but-internal: still emitted so the unit tests in
// tests/platform/test_sandbox.cpp can verify the profile shape we'd
// pass to a working sandbox-exec. Not called by spawn_sandboxed below.
std::string build_macos_profile(const SandboxPolicy&);

std::string build_macos_profile(const SandboxPolicy& policy) {
    auto add_read = [](std::string& p, const char* scope, std::string_view path) {
        p += "(allow file-read* (";
        p += scope; p += " \""; p += path; p += "\"))\n";
    };
    auto add_rw = [](std::string& p, const char* scope, std::string_view path) {
        p += "(allow file* (";
        p += scope; p += " \""; p += path; p += "\"))\n";
    };

    std::string p;
    p += "(version 1)\n";
    p += "(deny default)\n";
    p += "(allow process-fork)\n";
    p += "(allow process-exec*)\n";
    p += "(allow signal (target self))\n";
    p += "(allow file-read-metadata)\n";

    if (policy.allow_tmpdir) {
        add_rw(p, "subpath", "/tmp");
        add_rw(p, "subpath", "/private/tmp");
        add_rw(p, "subpath", "/private/var/folders");
        add_rw(p, "subpath", "/var/folders");
    }

    for (const auto& r : policy.path_rules) {
        const char* scope = r.recursive ? "subpath" : "literal";
        std::string esc = escape_path(r.path.string());
        if (r.access == PathRule::Access::ReadOnly) {
            add_read(p, scope, esc);
        } else if (r.access == PathRule::Access::ReadWrite) {
            add_rw(p, scope, esc);
        }
    }
    return p;
}

bool is_supported() {
    // See file header. sandbox-exec works for *some* profiles on macOS-14
    // but not the shape we'd need. Returning false makes callers treat
    // macOS as "no enforcement available," which is the correct answer
    // until the Endpoint Security migration.
    return false;
}

rivet::Result<rivet::process::ChildProcess>
spawn_sandboxed(rivet::process::SpawnOptions opts, SandboxPolicy /*policy*/) {
    return rivet::process::spawn(std::move(opts));
}

} // namespace rivet::sandbox
