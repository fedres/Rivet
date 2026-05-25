// rivet/platform/interface/sandbox.hpp
// Process sandboxing Platform Abstraction Layer.
//
// Sandboxing is optional and always gracefully degraded.
// A build can run without sandboxing — it just won't enforce hermeticity.
//
// Platform implementations:
//   platform/linux/sandbox.cpp    (seccomp-bpf + namespaces, optional)
//   platform/macos/sandbox.cpp    (sandbox_init profile)
//   platform/windows/sandbox.cpp  (Job Objects + ACL restrictions)
#pragma once

#include "result.hpp"
#include "process.hpp"

#include <vector>

namespace rivet::sandbox {

// Filesystem access rule for a sandboxed process.
struct PathRule {
    enum class Access { ReadOnly, ReadWrite, Deny };
    Path   path;
    Access access;
    bool   recursive = true;
};

// Network access policy.
enum class NetworkPolicy {
    Allow,         // default: allow all
    DenyOutbound,  // no outbound connections
    DenyAll,       // no network at all
};

struct SandboxPolicy {
    std::vector<PathRule>  path_rules;
    NetworkPolicy          network      = NetworkPolicy::Allow;
    bool                   allow_exec   = true;   // allow spawning child processes
    bool                   allow_tmpdir = true;   // allow writes to temp dir
};

// Returns true if sandboxing is supported on the current platform/kernel.
[[nodiscard]] bool is_supported();

// Spawn a sandboxed process. Falls back to unsandboxed spawn if !is_supported().
// The caller sees no difference — only enforcement is absent when unsupported.
[[nodiscard]] rivet::Result<rivet::process::ChildProcess>
    spawn_sandboxed(rivet::process::SpawnOptions opts, SandboxPolicy policy);

} // namespace rivet::sandbox
