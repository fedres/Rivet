// runtime/package/resolver.cpp — Multi-source dependency resolver
#include "resolver.hpp"

#include <algorithm>
#include <format>
#include <queue>
#include <unordered_set>

namespace rivet::pkg {

PackageRef dep_to_ref(std::string_view name, const DepSpec& spec) {
    PackageRef r;
    r.name               = std::string(name);
    r.version_constraint = spec.version;
    r.git_url            = spec.git_url;
    r.git_ref            = spec.git_ref;
    r.local_path         = spec.local_path;
    r.features           = spec.features;
    switch (spec.kind) {
        case DepKind::Path:     r.source_id = "path";  break;
        case DepKind::Git:      r.source_id = "git";   break;
        case DepKind::Registry: /* leave unset → tried by source order */ break;
    }
    return r;
}

namespace {

// Stable ordering for the locked-deps list — alphabetical by name (deterministic
// diffs in rivet.lock).
bool dep_less(const LockedDep& a, const LockedDep& b) {
    if (a.name != b.name) return a.name < b.name;
    return a.version < b.version;
}

LockedDep to_locked(const ResolvedPackage& pkg) {
    LockedDep d;
    d.name     = pkg.name;
    d.version  = pkg.version;
    d.source   = pkg.source_id;
    d.checksum = pkg.archive_sha256.value_or("");

    if (pkg.source_id == "git") {
        // source_locator is "url#rev".
        auto hash = pkg.source_locator.find('#');
        d.git_url    = hash == std::string::npos
                       ? pkg.source_locator
                       : pkg.source_locator.substr(0, hash);
        d.git_commit = hash == std::string::npos
                       ? std::string()
                       : pkg.source_locator.substr(hash + 1);
    } else if (pkg.source_id == "path") {
        d.local_path = Path{pkg.source_locator};
    } else if (pkg.source_id == "vcpkg") {
        d.registry_url = "vcpkg";
    } else {
        d.registry_url = pkg.source_id;
    }

    for (const auto& dep : pkg.deps)
        d.deps.push_back(dep.name);
    std::sort(d.deps.begin(), d.deps.end());
    return d;
}

} // namespace

Result<LockFile> Resolver::resolve_locked(const Manifest& root,
                                          const LockFile& existing) {
    // Walk the manifest. Every direct dep must already appear in `existing`;
    // we surface the lockfile entries verbatim plus all their transitive
    // packages. No source resolution is performed — entirely local op.
    std::unordered_set<std::string> reachable;
    std::vector<std::string>        worklist;

    auto in_lock = [&](std::string_view name) -> const LockedDep* {
        for (const auto& d : existing.packages)
            if (d.name == name) return &d;
        return nullptr;
    };

    // Seed with direct deps (the manifest is the contract).
    for (const auto& [name, spec] : root.dependencies) {
        auto* d = in_lock(name);
        if (!d)
            return make_error<LockFile>(std::format(
                "locked: rivet.lock is missing '{}' (declared in rivet.toml). "
                "Re-run `rivet add` or remove the --frozen/--locked flag.", name));
        // Drift check: manifest spec must be compatible with the locked
        // version. Bare equality is the strongest signal; for general
        // constraints we re-use the SemVer matcher.
        if (!spec.version.empty() && spec.version != "*") {
            if (auto v = parse_semver(d->version);
                    v && !satisfies(spec.version, *v))
                return make_error<LockFile>(std::format(
                    "locked: '{}' {} does not satisfy manifest constraint '{}'. "
                    "Lockfile drift — re-run resolution without --frozen/--locked.",
                    name, d->version, spec.version));
        }
        if (reachable.insert(name).second)
            worklist.push_back(name);
    }
    if (opts_.include_dev) {
        for (const auto& [name, spec] : root.dev_dependencies) {
            (void)spec;
            if (auto* d = in_lock(name); d && reachable.insert(name).second)
                worklist.push_back(name);
        }
    }

    // Transitive closure within the lockfile itself.
    while (!worklist.empty()) {
        std::string cur = std::move(worklist.back());
        worklist.pop_back();
        auto* d = in_lock(cur);
        if (!d) continue;
        for (const auto& dep_name : d->deps) {
            // deps are stored as bare names in our lockfile format.
            if (reachable.insert(dep_name).second)
                worklist.push_back(dep_name);
        }
    }

    LockFile out;
    out.format_version = existing.format_version;
    out.root_name      = root.name;
    out.root_version   = root.version;
    for (const auto& d : existing.packages)
        if (reachable.count(d.name)) out.packages.push_back(d);
    std::sort(out.packages.begin(), out.packages.end(), dep_less);
    return out;
}

Result<LockFile> Resolver::resolve(const Manifest& root) {
    LockFile lf;
    lf.format_version = 1;
    lf.root_name      = root.name;
    lf.root_version   = root.version;

    std::unordered_set<std::string> seen;
    std::queue<PackageRef>           queue;

    // Seed with direct deps from the manifest.
    for (const auto& [name, spec] : root.dependencies)
        queue.push(dep_to_ref(name, spec));
    if (opts_.include_dev) {
        for (const auto& [name, spec] : root.dev_dependencies)
            queue.push(dep_to_ref(name, spec));
    }

    while (!queue.empty()) {
        PackageRef ref = std::move(queue.front());
        queue.pop();

        // Dedup: first-resolved-wins. A future version-conflict resolver
        // can inspect collisions here instead of skipping.
        if (!seen.insert(ref.name).second) continue;

        auto r = sources_.resolve(ref);
        if (!r) return make_error<LockFile>(std::format(
            "resolver: cannot resolve '{}': {}", ref.name, r.error().message));

        lf.packages.push_back(to_locked(*r));

        // Enqueue transitive deps.
        for (auto& d : r->deps) queue.push(std::move(d));
    }

    std::sort(lf.packages.begin(), lf.packages.end(), dep_less);
    return lf;
}

} // namespace rivet::pkg
