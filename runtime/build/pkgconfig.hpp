// runtime/build/pkgconfig.hpp — minimal pkg-config (.pc) reader
//
// vcpkg drops `<triplet>/lib/pkgconfig/<lib>.pc` files describing each
// installed library: which `-l...` flags to emit, which include
// directories are public, and which other packages it transitively
// `Requires`. Reading these is the cleanest way to know how to link a
// dependency — beats globbing `<lib>/*.a` and hoping the order is right.
//
// We implement only the subset of pkg-config we actually need:
//   * variable expansion (`${name}`)
//   * Name / Version / Description headers
//   * Cflags, Libs, Cflags.private, Libs.private
//   * Requires, Requires.private (resolved transitively)
//
// Not implemented: --define-variable overrides, pkg-config-style
// comparison operators on Requires, `--variable`-style introspection.
#pragma once

#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace rivet::build {

struct PkgConfig {
    std::string name;
    std::string version;
    std::string description;
    // Tokenised. `-I/path`, `-D...`, `-pthread`, etc.
    std::vector<std::string> cflags;
    // Tokenised. `-L/path`, `-lfoo`, `-framework Foo`, `-pthread`, etc.
    std::vector<std::string> libs;
    // Bare package names (no version specifiers — those are stripped).
    std::vector<std::string> requires_;
    std::vector<std::string> requires_private;
    // Raw variables (post-expansion is lazy — we only expand on read).
    std::unordered_map<std::string, std::string> vars;
};

[[nodiscard]] Result<PkgConfig> parse_pc(const Path& pc_file);

struct ResolvedPkgs {
    std::vector<std::string> cflags;
    std::vector<std::string> libs;
    // Names successfully resolved, in topological order (deps first).
    std::vector<std::string> resolved;
    // Names we couldn't find a .pc file for.
    std::vector<std::string> unresolved;
};

// Resolve a set of root package names against a list of pkgconfig search
// directories. Walks `Requires:` transitively, deduplicates while
// preserving order (deps come before their dependents on the link line),
// and merges cflags/libs.
//
// `static_link` controls whether to include `.private` sections — set
// true for static linking, false for shared (mirrors pkg-config --static).
[[nodiscard]] Result<ResolvedPkgs>
resolve_pkgs(const std::vector<std::string>& roots,
             const std::vector<Path>&       search_dirs,
             bool                            static_link);

} // namespace rivet::build
