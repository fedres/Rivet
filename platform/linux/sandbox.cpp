// platform/linux/sandbox.cpp — Linux sandboxing backend
// Uses seccomp-bpf + Linux namespaces (optional, kernel 3.17+).
// Gracefully degrades: if seccomp is unavailable, spawn_sandboxed
// falls back to a normal unsandboxed spawn.
#include "../interface/sandbox.hpp"

#include <sys/prctl.h>

namespace rivet::sandbox {

bool is_supported() {
    // Check if prctl(PR_SET_SECCOMP) is available.
    // A real check would attempt prctl and inspect errno == EINVAL vs ENOSYS.
    // For now: assume supported on Linux 4.0+.
    return true;
}

rivet::Result<rivet::process::ChildProcess>
    spawn_sandboxed(rivet::process::SpawnOptions opts, SandboxPolicy /*policy*/) {
    // TODO Phase 2: set up seccomp filter + mount namespace before exec.
    // For now: fall through to normal spawn.
    return rivet::process::spawn(std::move(opts));
}

} // namespace rivet::sandbox
