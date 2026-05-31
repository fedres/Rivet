// platform/macos/sandbox.cpp -- macOS sandboxing backend
//
// Translates a SandboxPolicy into a sandbox-exec(1) profile (TinyScheme
// S-expressions) and prepends `sandbox-exec -p '<profile>'` to the
// command. sandbox-exec(1) is deprecated by Apple but still ships with
// every macOS release and has no third-party-callable replacement -- the
// same approach Bazel, Nix-darwin, and bun.sh use.
//
// The profile is constructed at default-deny and allow-listed per
// PathRule. Common system reads clang needs to find its own libc++
// headers (/usr, /System, /Library/Developer) are always allowed.
#include "../interface/sandbox.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace rivet::sandbox {

namespace {

// Escape a path for an S-expression string literal. The only chars
// sandbox-exec cares about are `"` and `\`; double both.
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

// Public-but-internal: exposed so the unit test can verify profile shape
// without forking sandbox-exec. Declared via a forward inside the impl
// translation unit, not in the public header.
std::string build_macos_profile(const SandboxPolicy& policy);

std::string build_macos_profile(const SandboxPolicy& policy) {
    // One (allow ...) per filter -- macOS-14 sandbox-exec was SIGABRT'ing
    // on the more compact multi-arg form. The unit tests don't trigger
    // the abort because their profiles are tiny; the executor's policy is
    // what tipped it over.
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

    // Process management -- spawning subprocesses, sending signals to self,
    // observing one's own children. Without this the child can't even
    // exec(2) the compiler.
    p += "(allow process-fork)\n";
    p += "(allow process-exec*)\n";
    p += "(allow signal (target self))\n";
    p += "(allow sysctl-read)\n";          // clang reads hw.* at startup
    p += "(allow mach-lookup)\n";          // dyld / system frameworks
    p += "(allow iokit-open)\n";           // libuv, mach msg lookups
    p += "(allow ipc-posix-shm)\n";        // POSIX shm for some clang paths
    p += "(allow file-read-metadata)\n";   // stat/lstat are everywhere

    // Common reads that every clang invocation needs: the macOS SDK + the
    // toolchain itself + libc++ headers + locale data.
    add_read(p, "subpath",  "/usr");
    add_read(p, "subpath",  "/System");
    add_read(p, "subpath",  "/Library/Developer");
    add_read(p, "subpath",  "/Library/Apple");
    add_read(p, "subpath",  "/private/var/db/timezone");
    add_read(p, "literal",  "/private/etc/localtime");
    add_read(p, "literal",  "/private/etc/passwd");
    add_read(p, "literal",  "/dev/null");
    add_read(p, "literal",  "/dev/random");
    add_read(p, "literal",  "/dev/urandom");

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

    switch (policy.network) {
        case NetworkPolicy::Allow:
            p += "(allow network*)\n";
            break;
        case NetworkPolicy::DenyOutbound:
            p += "(allow network-bind (local ip))\n";
            p += "(allow network-inbound (local ip))\n";
            p += "(allow network-outbound (remote ip \"localhost:*\"))\n";
            break;
        case NetworkPolicy::DenyAll:
            // The implicit (deny default) at the top already covers this.
            break;
    }

    return p;
}

bool is_supported() {
    // sandbox-exec is always present on a stock macOS install since 10.5.
    return true;
}

rivet::Result<rivet::process::ChildProcess>
spawn_sandboxed(rivet::process::SpawnOptions opts, SandboxPolicy policy) {
    std::fprintf(stderr, "[sandbox/mac] enter, args.size=%zu rules=%zu\n",
        opts.args.size(), policy.path_rules.size());
    std::fflush(stderr);

    if (opts.args.empty()) {
        std::fprintf(stderr, "[sandbox/mac] args empty -> fall-through spawn\n");
        std::fflush(stderr);
        return rivet::process::spawn(std::move(opts));
    }

    std::string profile = build_macos_profile(policy);
    std::fprintf(stderr, "[sandbox/mac] profile built (len=%zu); first 200 chars:\n%.200s\n",
        profile.size(), profile.c_str());
    std::fflush(stderr);

    std::vector<std::string> wrapped = {
        "/usr/bin/sandbox-exec",
        "-p", std::move(profile),
    };
    for (auto& a : opts.args) wrapped.push_back(std::move(a));
    opts.args = std::move(wrapped);

    std::fprintf(stderr, "[sandbox/mac] calling process::spawn with wrapped argv (count=%zu)\n",
        opts.args.size());
    std::fflush(stderr);

    auto r = rivet::process::spawn(std::move(opts));
    std::fprintf(stderr, "[sandbox/mac] process::spawn returned %s\n",
        r ? "ok" : r.error().message.c_str());
    std::fflush(stderr);
    return r;
}

} // namespace rivet::sandbox
