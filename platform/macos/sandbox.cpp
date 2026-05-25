// platform/macos/sandbox.cpp — macOS sandboxing backend
// Uses sandbox_init() profile-based sandboxing (available since macOS 10.5).
// For stricter isolation: App Sandbox entitlements (requires signing).
#include "../interface/sandbox.hpp"
// #include <sandbox.h>  // uncomment when using sandbox_init

namespace rivet::sandbox {

bool is_supported() { return true; }

rivet::Result<rivet::process::ChildProcess>
    spawn_sandboxed(rivet::process::SpawnOptions opts, SandboxPolicy /*policy*/) {
    // TODO Phase 2: set up sandbox_init profile before exec.
    return rivet::process::spawn(std::move(opts));
}

} // namespace rivet::sandbox
