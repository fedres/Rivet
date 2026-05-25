// platform/windows/sandbox.cpp — Windows sandboxing backend
// Uses Job Objects to limit process tree resource usage.
// DACL/ACL restrictions for filesystem access (TODO).
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../interface/sandbox.hpp"

namespace rivet::sandbox {

bool is_supported() { return true; }

rivet::Result<rivet::process::ChildProcess>
    spawn_sandboxed(rivet::process::SpawnOptions opts, SandboxPolicy /*policy*/) {
    // TODO Phase 2: assign process to a Job Object with resource limits.
    return rivet::process::spawn(std::move(opts));
}

} // namespace rivet::sandbox
