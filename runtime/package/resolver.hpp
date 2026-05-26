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
    // --locked: existing rivet.lock is authoritative; error if it would change.
    // --frozen: same as --locked AND no network/source access — the lockfile
    //           must satisfy the manifest entirely from local information.
    // Both default false (regular resolution may update the lockfile).
    bool locked      = false;
    bool frozen      = false;
};

class Resolver {
public:
    explicit Resolver(SourceRegistry& sources, ResolverOptions opts = {})
        : sources_(sources), opts_(opts) {}

    // Walk the manifest graph, resolve every reachable dependency, and emit
    // a deterministic lockfile.
    [[nodiscard]] Result<LockFile> resolve(const Manifest& root);

    // --locked / --frozen variant: prefer the existing lockfile entries.
    // The candidate `existing` is treated as authoritative — its entries
    // are returned verbatim if they cover the manifest. Returns an error if
    // a manifest dep is missing from `existing` (covering "lockfile drift").
    [[nodiscard]] Result<LockFile>
    resolve_locked(const Manifest& root, const LockFile& existing);

private:
    SourceRegistry& sources_;
    ResolverOptions opts_;
};

// Convenience: convert a DepSpec from rivet.toml into a PackageRef
// suitable for SourceRegistry::resolve().
PackageRef dep_to_ref(std::string_view name, const DepSpec& spec);

} // namespace rivet::pkg
