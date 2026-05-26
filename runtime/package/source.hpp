// runtime/package/source.hpp — Package source abstraction
//
// A PackageSource is a backend that can resolve a package name + version
// constraint into a concrete package, and materialize its source tree on
// disk so the build engine can compile it with the bundled toolchain.
//
// The same dependency graph can pull from multiple sources simultaneously:
//   - LocalSource     → `dep = { path = "../foo" }`
//   - GitSource       → `dep = { git = "https://...", rev = "abc123" }`
//   - VcpkgSource     → `dep = "fmt@^11"` (resolves through vcpkg registry)
//   - BinaryCache     → content-addressed prebuilt artifact (the killer feature)
//
// Sources never build. They only fetch source trees + describe build recipes.
// The build engine consumes PortRecipe and drives the actual compilation
// using the bundled clang for ABI determinism.
#pragma once

#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rivet::pkg {

// ─── Package identifiers ─────────────────────────────────────────────────────

// A request to resolve a dependency. The source backends interpret the
// constraint according to their own semantics (vcpkg uses semver, git uses
// branches/tags/SHA, local just resolves the path).
struct PackageRef {
    std::string name;
    std::string version_constraint;       // "^1", "=2.3.4", "*", git rev, etc.
    std::optional<std::string> source_id; // explicit source override ("vcpkg", "git", "path", ...)

    // Source-specific locators — populated based on the dep spec in rivet.toml.
    std::string git_url;                  // for "git"
    std::string git_ref;                  // for "git" (branch, tag, or SHA)
    Path        local_path;               // for "path"

    std::vector<std::string> features;
};

// A package after resolution: exact version + source coordinates so future
// runs can reproduce the same selection (recorded in rivet.lock).
struct ResolvedPackage {
    std::string name;
    std::string version;                  // exact, normalized version
    std::string source_id;                // which source resolved it
    std::string source_locator;           // vcpkg baseline SHA / git commit / fs path
    std::optional<std::string> archive_sha256; // for source tarballs
    std::vector<PackageRef>    deps;      // transitive deps to resolve next
    std::vector<std::string>   features;  // features actually enabled
};

// Build system used by an upstream port. Tells the build engine which
// driver to invoke (and with what flags) to produce build outputs.
//
//   Prebuilt — the package is already installed on disk (vcpkg-driven build,
//              or a hit from the content-addressed binary cache). The build
//              engine consumes `install_root` directly and skips compilation.
enum class BuildDriver { Rivet, CMake, Meson, Autotools, Custom, Prebuilt };

// Concrete recipe for building (or just consuming) a fetched package.
struct PortRecipe {
    Path        source_dir;               // extracted/checked-out source
    Path        install_root;             // populated for Prebuilt: <root>/{include,lib}
    BuildDriver driver = BuildDriver::CMake;
    std::vector<std::string> cmake_args;  // additional -D flags
    std::vector<Path>        patches;     // applied in order
    std::vector<std::string> targets;     // empty = build all
    // For Custom: shell commands to run. Composable with the bundled
    // toolchain via $CC / $CXX / $AR environment overrides.
    std::vector<std::string> custom_steps;
};

// ─── PackageSource interface ─────────────────────────────────────────────────

class PackageSource {
public:
    virtual ~PackageSource() = default;

    // Stable identifier for this source kind ("vcpkg", "git", "path", "cache").
    [[nodiscard]] virtual std::string id() const = 0;

    // Whether this source can answer queries about `name` at all.
    // Cheap predicate — used to skip sources during resolve fanout.
    [[nodiscard]] virtual bool handles(const PackageRef& ref) const = 0;

    // Resolve a version constraint into a concrete ResolvedPackage. No I/O
    // beyond reading the source's manifest index (e.g., vcpkg baseline file).
    // Returns NotFound error if the name doesn't exist in this source.
    [[nodiscard]] virtual Result<ResolvedPackage>
        resolve(const PackageRef& ref) = 0;

    // Materialize the source tree on disk at `cache_dir/<name>-<version>/`
    // and return the build recipe. May download archives, clone git repos,
    // copy local paths, or no-op if already cached.
    [[nodiscard]] virtual Result<PortRecipe>
        fetch(const ResolvedPackage& pkg, const Path& cache_dir) = 0;
};

// ─── Source registry ─────────────────────────────────────────────────────────

// Multiple sources can be active in one resolution pass. Order matters:
// earlier sources are tried first when the dep has no explicit source_id.
// The recommended order is:
//   1. BinaryCacheSource  (content-addressed prebuilt artifact)
//   2. LocalSource        (path overrides — usually for dev)
//   3. GitSource          (explicit git references)
//   4. VcpkgSource        (public catalog)
class SourceRegistry {
public:
    void add(std::unique_ptr<PackageSource> src);

    // Resolve through the chain in priority order. If `ref.source_id` is set,
    // only that source is consulted.
    [[nodiscard]] Result<ResolvedPackage> resolve(const PackageRef& ref) const;

    // Lookup a source by id (for fetch, after the resolver chose one).
    [[nodiscard]] PackageSource* find(std::string_view id) const;

    [[nodiscard]] const std::vector<std::unique_ptr<PackageSource>>& sources() const { return sources_; }

private:
    std::vector<std::unique_ptr<PackageSource>> sources_;
};

// ─── SemVer helpers (used by vcpkg + registry backends) ──────────────────────

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease;               // "rc.1", "alpha", etc.

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] auto operator<=>(const SemVer&) const = default;
};

[[nodiscard]] Result<SemVer> parse_semver(std::string_view s);

// A constraint string ("^1.2", ">=1 <3", "=2.3.4", "*") plus a candidate
// version. Returns true if the candidate satisfies the constraint.
[[nodiscard]] bool satisfies(std::string_view constraint, const SemVer& candidate);

} // namespace rivet::pkg
