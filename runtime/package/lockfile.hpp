// runtime/package/lockfile.hpp — rivet.lock format
//
// The lock file records the exact resolved version + content hash of every
// dependency in the graph. It is deterministic: same inputs always produce
// the same lock file.
#pragma once

#include "manifest.hpp"
#include "../../platform/interface/result.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rivet::pkg {

// ─── Locked dependency ────────────────────────────────────────────────────────

struct LockedDep {
    std::string name;
    std::string version;            // exact resolved version "1.2.3"
    std::string source;             // "registry", "git", "path"
    std::string checksum;           // SHA-256 of the source archive

    // Only for registry deps:
    std::string registry_url;

    // Only for git deps:
    std::string git_url;
    std::string git_commit;         // full SHA-1 commit hash

    // Only for path deps:
    Path        local_path;

    // Transitive dependencies (resolved names, by version):
    std::vector<std::string> deps;  // "name@version"
};

// ─── Lock file ────────────────────────────────────────────────────────────────

struct LockFile {
    // rivet.lock format version (bumped on incompatible layout changes).
    int         format_version = 1;

    // Ordered list of all locked packages (alphabetical for stable diffs).
    std::vector<LockedDep> packages;

    // Root package version (from rivet.toml).
    std::string root_name;
    std::string root_version;
};

// ─── I/O ──────────────────────────────────────────────────────────────────────

/// Parse an existing rivet.lock file.
[[nodiscard]] Result<LockFile> parse_lockfile(const Path& path);

/// Write a lock file to disk (atomically).
[[nodiscard]] Result<void> write_lockfile(const LockFile& lf, const Path& path);

/// Generate a lock file by resolving `manifest`'s dependencies.
/// This is the SAT-based resolver stub — full PubGrub implementation is Phase 3.
[[nodiscard]] Result<LockFile> resolve(const Manifest& manifest);

/// Check whether an existing lock file is still consistent with a manifest.
/// Returns true if the lock file is up to date (no re-resolve needed).
[[nodiscard]] bool is_up_to_date(const LockFile& lf, const Manifest& manifest);

} // namespace rivet::pkg
