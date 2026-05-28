// runtime/build/paths.hpp — derived build paths
//
// Centralises the "where does build output go" decision so the
// RIVET_TARGET_DIR env override is honoured by every code path that
// computes `<project>/.rivet/build/...` (cmd_build, cmd_test, multi_target,
// fuzz, etc.). Cargo's equivalent is CARGO_TARGET_DIR / [build].target-dir.
#pragma once

#include "../../platform/interface/types.hpp"

namespace rivet::build {

/// Returns the absolute path where rivet should land .o, .a, and binaries
/// for this project. Default is `<project_root>/.rivet/build`. Overridden
/// by the `RIVET_TARGET_DIR` environment variable, which may be absolute
/// or relative to the process's current working directory.
[[nodiscard]] Path build_root_for(const Path& project_root);

} // namespace rivet::build
