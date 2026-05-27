// runtime/build/multi_target.hpp — multi-artefact build engine
//
// When a rivet.toml declares [[lib]] / [[bin]] / [[test]] / [[vendor]]
// targets (M1: rivet-self-build, OpenFOAM-grade projects), this module
// resolves their dep graph and populates a BuildGraph with one Compile
// task per source, one Archive task per lib/vendor, and one Link task
// per bin/test. Per-source compile flags and cfg-conditional overrides
// (os = "linux"/"macos"/"windows") are applied here.
//
// Single-binary projects with no targets continue to use the original
// src/-scanning path in cmd_build — this module is a strict opt-in.
#pragma once

#include "graph.hpp"
#include "../package/manifest.hpp"
#include "../toolchain/discovery.hpp"

namespace rivet::cache { class Store; }

namespace rivet::build {

struct InstalledExternalDeps {
    std::vector<Path>        include_dirs;
    std::vector<Path>        lib_dirs;
    std::vector<Path>        link_libs;     // direct file refs (.a / .lib)
    std::vector<std::string> link_flags;    // -lfoo, foo.lib, -framework Foo, etc.
};

// One row per target after the build engine has resolved it: where its
// final artefact lives on disk, and which graph task represents that
// artefact's completion (downstream link tasks depend on it).
struct TargetArtifact {
    std::string name;
    pkg::TargetKind kind;
    Path artifact_path;
    TaskId final_task = 0;
};

// Populate `graph` with all compile/archive/link tasks for the targets
// declared in `manifest`. Returns the per-target artefacts so the caller
// can print build summaries or run tests against them.
[[nodiscard]] Result<std::vector<TargetArtifact>>
build_targets(const pkg::Manifest&         manifest,
              const toolchain::ToolchainInfo& tc,
              const BuildConfig&            base_cfg,
              const InstalledExternalDeps&  external_deps,
              std::string_view              profile_name,
              BuildGraph&                   graph,
              cache::Store*                 cache_store);

} // namespace rivet::build
