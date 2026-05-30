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
    p += "(allow file-read-metadata)\n";   // stat/lstat are everywhere

    // Common reads that every clang invocation needs: the macOS SDK + the
    // toolchain itself + libc++ headers + locale data. Allowing these by
    // default beats forcing every caller to enumerate them in PathRule.
    p +=
        "(allow file-read*"
        " (subpath \"/usr\")"
        " (subpath \"/System\")"
        " (subpath \"/Library/Developer\")"
        " (subpath \"/Library/Apple\")"
        " (subpath \"/private/etc/localtime\")"
        " (subpath \"/private/var/db/timezone\")"
        " (subpath \"/private/etc/passwd\")"
        " (literal \"/dev/null\")"
        " (literal \"/dev/random\")"
        " (literal \"/dev/urandom\")"
        ")\n";

    if (policy.allow_tmpdir) {
        // macOS gives every process a TMPDIR under /var/folders. clang puts
        // its temp .o-prep files there; sandbox-exec sees the canonical
        // path so /private prefixes both.
        p += "(allow file*"
             " (subpath \"/tmp\")"
             " (subpath \"/private/tmp\")"
             " (subpath \"/private/var/folders\")"
             " (subpath \"/var/folders\")"
             ")\n";
    }

    for (const auto& r : policy.path_rules) {
        const char* op =
            r.access == PathRule::Access::ReadOnly  ? "file-read*" :
            r.access == PathRule::Access::ReadWrite ? "file*"      : nullptr;
        if (!op) continue;
        const char* scope = r.recursive ? "subpath" : "literal";
        p += "(allow ";
        p += op;
        p += " (";
        p += scope;
        p += " \"";
        p += escape_path(r.path.string());
        p += "\"))\n";
    }

    switch (policy.network) {
        case NetworkPolicy::Allow:
            p += "(allow network*)\n";
            break;
        case NetworkPolicy::DenyOutbound:
            // Allow loopback only; deny everything else.
            p += "(allow network-bind network-inbound (local ip))\n";
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
    if (opts.args.empty()) {
        return rivet::process::spawn(std::move(opts));
    }

    // Prepend sandbox-exec to the argv. The profile rides in-line via -p;
    // argv max on macOS is 256 KB so the profile string fits comfortably.
    std::string profile = build_macos_profile(policy);
    std::vector<std::string> wrapped = {
        "/usr/bin/sandbox-exec",
        "-p", std::move(profile),
    };
    for (auto& a : opts.args) wrapped.push_back(std::move(a));
    opts.args = std::move(wrapped);

    return rivet::process::spawn(std::move(opts));
}

} // namespace rivet::sandbox
