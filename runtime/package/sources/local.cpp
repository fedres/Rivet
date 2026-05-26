// runtime/package/sources/local.cpp — local-path package source
#include "local.hpp"
#include "../manifest.hpp"
#include "../../../platform/interface/fs.hpp"

#include <format>

namespace rivet::pkg {

bool LocalSource::handles(const PackageRef& ref) const {
    if (ref.source_id && *ref.source_id == id()) return true;
    return !ref.local_path.empty();
}

Result<ResolvedPackage> LocalSource::resolve(const PackageRef& ref) {
    if (ref.local_path.empty())
        return make_error<ResolvedPackage>("local: no path provided for '" + ref.name + "'");

    auto exists = rivet::fs::exists(ref.local_path);
    if (!exists || !*exists)
        return make_error<ResolvedPackage>(
            std::format("local: path does not exist: {}", ref.local_path.string()));

    auto manifest_path = ref.local_path / "rivet.toml";
    auto m = parse_manifest(manifest_path);
    if (!m) return make_error<ResolvedPackage>(
        std::format("local: failed to read {}: {}",
                    manifest_path.string(), m.error().message));

    if (!ref.name.empty() && m->name != ref.name)
        return make_error<ResolvedPackage>(
            std::format("local: name mismatch — manifest says '{}' but ref expects '{}'",
                        m->name, ref.name));

    ResolvedPackage out;
    out.name           = m->name;
    out.version        = m->version;
    out.source_id      = id();
    out.source_locator = ref.local_path.string();
    out.features       = ref.features;

    // Translate transitive deps. (Just the names + constraints; full resolution
    // happens recursively in the SourceRegistry resolver.)
    for (const auto& [dep_name, spec] : m->dependencies) {
        PackageRef d;
        d.name               = dep_name;
        d.version_constraint = spec.version;
        d.git_url            = spec.git_url;
        d.git_ref            = spec.git_ref;
        d.local_path         = spec.local_path;
        d.features           = spec.features;
        switch (spec.kind) {
            case DepKind::Path:     d.source_id = "path";  break;
            case DepKind::Git:      d.source_id = "git";   break;
            case DepKind::Registry: d.source_id = "vcpkg"; break;  // default to vcpkg
        }
        out.deps.push_back(std::move(d));
    }

    return out;
}

Result<PortRecipe> LocalSource::fetch(const ResolvedPackage& pkg, const Path& /*cache_dir*/) {
    PortRecipe recipe;
    recipe.source_dir = Path{pkg.source_locator};

    // If the local package has its own rivet.toml, drive with Rivet's native
    // build system. CMake/Meson sources are detectable by file presence.
    auto rivet_toml = recipe.source_dir / "rivet.toml";
    auto cmake_lists = recipe.source_dir / "CMakeLists.txt";

    auto has_rivet = rivet::fs::exists(rivet_toml);
    auto has_cmake = rivet::fs::exists(cmake_lists);

    if (has_rivet && *has_rivet)        recipe.driver = BuildDriver::Rivet;
    else if (has_cmake && *has_cmake)   recipe.driver = BuildDriver::CMake;
    else                                recipe.driver = BuildDriver::Custom;

    return recipe;
}

} // namespace rivet::pkg
