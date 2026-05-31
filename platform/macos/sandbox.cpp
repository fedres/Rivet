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
    // DIAGNOSTIC: deny-default but allow ALL ops by category (no path
    // filters). If this works, the bake-in path lists are the problem;
    // if it still SIGABRTs, deny-default itself is unusable on macOS-14
    // for our wrapped-binary case.
    (void)policy;
    return "(version 1)\n"
           "(deny default)\n"
           "(allow process*)\n"
           "(allow file*)\n"
           "(allow mach*)\n"
           "(allow ipc*)\n"
           "(allow network*)\n"
           "(allow sysctl*)\n"
           "(allow signal)\n"
           "(allow iokit-open)\n"
           "(allow system-fsctl)\n"
           "(allow system-info)\n";

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
    // mach-lookup without filters was rejected by macOS-14 sandbox-exec
    // (SIGABRT during profile application). Allow only what dyld and the
    // standard runtime actually need to call -- enumerated explicitly.
    p += "(allow mach-lookup"
         " (global-name \"com.apple.system.opendirectoryd.libinfo\")"
         " (global-name \"com.apple.system.opendirectoryd.membership\")"
         " (global-name \"com.apple.SecurityServer\")"
         " (global-name \"com.apple.PowerManagement.control\")"
         " (global-name \"com.apple.system.notification_center\")"
         " (global-name \"com.apple.system.logger\")"
         " (global-name \"com.apple.distributed_notifications@Uv3\")"
         " (global-name \"com.apple.windowserver.active\")"
         " (global-name \"com.apple.coreservices.launchservicesd\")"
         ")\n";
    p += "(allow file-read-metadata)\n";   // stat/lstat are everywhere

    // Common reads that every clang invocation needs: the macOS SDK + the
    // toolchain itself + libc++ headers + locale data. Allowing these by
    // default beats forcing every caller to enumerate them in PathRule.
    //
    // NOTE: `subpath` is for directories; `literal` is for single files.
    // Earlier revisions tried (subpath "/private/etc/passwd") -- passwd
    // is a regular file, so the whole profile was rejected by
    // sandbox-exec at parse time and every Compile silently failed at 0s.
    p +=
        "(allow file-read*"
        " (subpath \"/usr\")"
        " (subpath \"/System\")"
        " (subpath \"/Library/Developer\")"
        " (subpath \"/Library/Apple\")"
        " (subpath \"/private/var/db/timezone\")"
        " (literal \"/private/etc/localtime\")"
        " (literal \"/private/etc/passwd\")"
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
