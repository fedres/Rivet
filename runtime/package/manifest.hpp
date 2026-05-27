// runtime/package/manifest.hpp — rivet.toml package manifest definition
//
// Mirrors the rivet.toml file format described in §15 of the architecture plan.
// All fields map directly from TOML; no business logic lives in this header.
#pragma once

#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rivet::pkg {

// ─── Dependency specification ─────────────────────────────────────────────────

enum class DepKind { Registry, Git, Path };

struct DepSpec {
    DepKind     kind    = DepKind::Registry;
    std::string version;            // SemVer constraint: "1.2", "^2.0", ">=1 <3"

    // Git dependency extras:
    std::string git_url;
    std::string git_ref;            // branch, tag, or SHA

    // Path dependency extras:
    Path        local_path;

    // Feature flags:
    std::vector<std::string> features;
    bool static_link = false;

    // Cargo-style inheritance: `dep.workspace = true` means "look up the
    // version (and other fields) from the workspace root's
    // [workspace.dependencies][name]". Resolved by find_and_parse() — once
    // the manifest reaches the CLI, this flag has already been applied and
    // the spec carries the real version. Surfaced here so tests + tooling
    // can detect inheritance intent.
    bool inherit_workspace = false;
};

// ─── Platform filter ──────────────────────────────────────────────────────────

struct PlatformFilter {
    std::optional<bool> linux_x64;
    std::optional<bool> linux_arm64;
    std::optional<bool> macos_arm64;
    std::optional<bool> macos_x64;
    std::optional<bool> windows_x64;
    std::optional<bool> windows_arm64;
};

// ─── Build configuration ──────────────────────────────────────────────────────

enum class BuildSystem { Rivet, CMake, Make, Autoconf, Meson, Custom };

struct BuildSection {
    BuildSystem              system  = BuildSystem::Rivet;
    std::vector<std::string> targets;
    std::vector<std::string> extra_flags;
    std::string              cxx_std = "c++23";
};

// ─── Profile ─────────────────────────────────────────────────────────────────

struct Profile {
    std::string              name;           // "debug", "release", "asan", etc.
    std::vector<std::string> sanitizers;
    std::optional<int>       opt_level;
    bool                     debug   = true;
    bool                     lto     = false;
    std::vector<std::string> extra_flags;
};

// ─── Targets (M1: multi-artefact projects, e.g. rivet itself) ────────────────
//
// One rivet.toml can declare multiple build targets — static libraries,
// executables, tests, and vendored C sources — each with its own source
// list, include paths, compile/link flags, and dependencies on other
// targets in the same manifest.
//
// Single-binary projects don't need any of this: their `src/` tree is
// auto-discovered and the binary is named after the package. Projects
// like rivet itself (3 libs + 1 binary + ~12 tests + 2 vendored amalgs)
// declare each artefact explicitly via [[lib]] / [[bin]] / [[test]] /
// [[vendor]].

enum class TargetKind { Lib, Bin, Test, Vendor };

// Cargo-style target.cfg gate. Today we honour just `os = "linux"|"macos"|
// "windows"`. Negation and conjunctions are not implemented yet.
struct CfgPredicate {
    std::optional<std::string> os;   // "linux" / "macos" / "windows"
};

struct TargetCfgOverride {
    CfgPredicate                    cfg;
    std::vector<std::string>        extra_sources;
    std::vector<std::string>        extra_link_libs;
    std::vector<std::string>        extra_compile_flags;
};

struct Target {
    TargetKind                          kind = TargetKind::Bin;
    std::string                         name;
    std::string                         path;             // entry-point file (bins/tests only)
    std::vector<std::string>            sources;          // explicit list / globs
    std::vector<std::string>            include_dirs;
    std::vector<std::string>            depends_on;       // other intra-manifest target names
    std::vector<std::string>            compile_flags;
    std::vector<std::string>            link_libs;        // raw -l/.lib tokens
    std::vector<std::string>            defines;
    // Per-source compile-flag overrides. Key is a path (relative to root)
    // OR a glob. Used for vendored C amalgamations needing -Wno-all etc.
    std::unordered_map<std::string, std::vector<std::string>> per_source_flags;
    // cfg-conditional add-ons, evaluated against host triple at build time.
    std::vector<TargetCfgOverride>      cfg_overrides;
};

// ─── Workspace (cargo-style monorepo support) ────────────────────────────────
//
// A workspace root has a `[workspace]` block in its rivet.toml. The `members`
// list points at sub-directories — each containing its own rivet.toml with a
// `[package]` block. `rivet build` at the workspace root builds every member
// in dependency order; `rivet build` inside a member builds that member and
// any workspace-member deps it pulls in.
//
// Workspace-level pins (`[workspace.dependencies]`) let every member share a
// single source of truth for external versions via `fmt.workspace = true` in
// the member's [dependencies].

struct WorkspaceSection {
    std::vector<std::string> members;            // relative dirs containing member rivet.toml
    std::vector<std::string> exclude;            // members glob-excluded from the build
    std::unordered_map<std::string, DepSpec> dependencies;  // shared pins
};

// ─── Package manifest (top-level rivet.toml) ─────────────────────────────────

struct Manifest {
    // [package]
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> authors;
    std::string license;
    std::string homepage;
    std::string repository;
    std::string readme;
    std::vector<std::string> keywords;

    // [dependencies]
    std::unordered_map<std::string, DepSpec> dependencies;

    // [dev-dependencies]
    std::unordered_map<std::string, DepSpec> dev_dependencies;

    // [build]
    BuildSection build;

    // [platforms]
    PlatformFilter platforms;

    // [profiles.*]
    std::unordered_map<std::string, Profile> profiles;

    // [scripts]
    // Free-form commands invoked via `rivet run <name>` — npm/bun-style DX.
    // Commands are executed by the shell with the manifest dir as CWD and
    // <rivet_home>/bin prepended to PATH (so dep binaries are reachable).
    std::unordered_map<std::string, std::string> scripts;

    // [[lib]], [[bin]], [[test]], [[vendor]] arrays. Empty when the
    // manifest is a single-binary project that relies on src/-scanning.
    std::vector<Target> targets;

    // [workspace] — optional. When present this manifest is a workspace
    // root; members[] points at sub-dirs that each contain their own
    // rivet.toml. The root may itself also be a package (have a
    // [package] block) or just be a workspace container.
    std::optional<WorkspaceSection> workspace;

    // Absolute path to the directory containing this rivet.toml.
    Path root_dir;

    // If this manifest is a workspace member, the absolute path to the
    // workspace root's directory (the dir holding the rivet.toml with the
    // [workspace] block). Empty when the manifest is standalone or is
    // itself the workspace root. Populated by find_and_parse().
    Path workspace_root;
};

/// Path to the lockfile this manifest should read/write. Resolves to the
/// workspace root's rivet.lock when the manifest is a workspace member,
/// else the manifest's own dir. Use this everywhere you'd otherwise write
/// `manifest.root_dir / "rivet.lock"`.
[[nodiscard]] Path lockfile_path_for(const Manifest& m);

// ─── Parsing ──────────────────────────────────────────────────────────────────

/// Parse a rivet.toml file. `path` must point to the file (not the directory).
[[nodiscard]] Result<Manifest> parse_manifest(const Path& path);

/// Search for a rivet.toml starting at `start_dir`, walking up to the filesystem root.
/// Returns the manifest and its location.
[[nodiscard]] Result<Manifest> find_and_parse(const Path& start_dir);

/// Validate a parsed manifest (version format, required fields, etc.).
[[nodiscard]] Result<void> validate(const Manifest& m);

/// Serialize a manifest back to TOML text (for `rivet new` scaffolding).
[[nodiscard]] std::string serialize(const Manifest& m);

} // namespace rivet::pkg
