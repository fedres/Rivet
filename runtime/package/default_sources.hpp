// runtime/package/default_sources.hpp — Convenience factory for SourceRegistry
//
// Builds the standard backend chain used by `rivet add`, `rivet build`, and
// `rivet lock`. The order is the lookup priority during resolve:
//   1. LocalSource   — fast path for `path = "../foo"` overrides
//   2. GitSource     — explicit `git = "..."` references
//   3. VcpkgSource   — default for any bare-name dep
//
// (BinaryCache is intentionally NOT a PackageSource — it's a post-resolve
// fast path that bypasses build-from-source; see runtime/cache/binary_cache.hpp.)
#pragma once

#include "source.hpp"

namespace rivet::pkg {

// Build a SourceRegistry populated with the default backend chain.
// `rivet_home` is the root for cached vcpkg clones and other source state.
[[nodiscard]] SourceRegistry make_default_registry(const Path& rivet_home);

} // namespace rivet::pkg
