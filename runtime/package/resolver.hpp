// runtime/package/resolver.hpp — Multi-source dependency resolver.
//
// Walks the manifest's dependency graph through SourceRegistry to produce
// a deterministic LockFile. Each dep is resolved once (deduplicated by
// name). First-wins on conflicting versions; full PubGrub-style conflict
// resolution is a future enhancement.
#pragma once

#include "lockfile.hpp"
#include "manifest.hpp"
#include "source.hpp"

namespace rivet::pkg {

struct ResolverOptions {
    bool include_dev = false;       // include [dev-dependencies] (default no)
    bool offline     = false;       // forbid any network access (--frozen / --locked)
};

class Resolver {
public:
    explicit Resolver(SourceRegistry& sources, ResolverOptions opts = {})
        : sources_(sources), opts_(opts) {}

    // Walk the manifest graph, resolve every reachable dependency, and emit
    // a deterministic lockfile. Returns an error if any dep cannot be
    // resolved or if --frozen is on and the lockfile would change.
    [[nodiscard]] Result<LockFile> resolve(const Manifest& root);

private:
    SourceRegistry& sources_;
    ResolverOptions opts_;
};

// Convenience: convert a DepSpec from rivet.toml into a PackageRef
// suitable for SourceRegistry::resolve().
PackageRef dep_to_ref(std::string_view name, const DepSpec& spec);

} // namespace rivet::pkg
