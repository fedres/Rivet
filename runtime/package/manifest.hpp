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

    // Absolute path to the directory containing this rivet.toml.
    Path root_dir;
};

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
