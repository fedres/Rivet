// runtime/package/sources/vcpkg.cpp — vcpkg backend implementation
#include "vcpkg.hpp"
#include "../../common/json.hpp"
#include "../../../platform/interface/fs.hpp"
#include "../../../platform/interface/process.hpp"

#include <format>

namespace rivet::pkg {

namespace {

constexpr const char* kVcpkgRepo = "https://github.com/microsoft/vcpkg.git";

Result<void> run_git(const Path& cwd, std::vector<std::string> args, std::string_view ctx) {
    rivet::process::SpawnOptions opts;
    opts.args = std::move(args);
    opts.args.insert(opts.args.begin(), "git");
    opts.working_dir    = cwd;
    opts.inherit_env    = true;
    opts.capture_stdout = true;
    opts.capture_stderr = true;

    auto child = rivet::process::spawn(std::move(opts));
    if (!child) return make_error(std::format(
        "vcpkg/{}: spawn git failed: {}", ctx, child.error().message));
    auto code = child->wait();
    if (!code) return make_error(std::format("vcpkg/{}: wait failed", ctx));
    if (*code != 0)
        return make_error(std::format(
            "vcpkg/{}: git exited {}: {}", ctx, *code, child->stderr_output()));
    return {};
}

Path port_dir(const Path& vcpkg_root, std::string_view name) {
    return vcpkg_root / "ports" / std::string(name);
}

// Extract a SemVer-style version string from a vcpkg.json. vcpkg uses a few
// different keys: "version", "version-semver", "version-string", "version-date".
std::string read_version(const json::Value& manifest) {
    for (const char* key : {"version", "version-semver", "version-string", "version-date"}) {
        const auto& v = manifest[key];
        if (v.is_string() && !v.as_string().empty()) return v.as_string();
    }
    return {};
}

// Translate the "dependencies" array — entries may be plain strings or
// objects: {"name": "...", "features": [...], "host": true}.
std::vector<PackageRef> read_deps(const json::Value& manifest) {
    std::vector<PackageRef> out;
    for (const auto& entry : manifest["dependencies"].as_array()) {
        PackageRef dep;
        if (entry.is_string()) {
            dep.name = entry.as_string();
        } else if (entry.is_object()) {
            dep.name = entry["name"].as_string();
            for (const auto& f : entry["features"].as_array())
                if (f.is_string()) dep.features.push_back(f.as_string());
            // Skip host-only deps for the runtime dep graph — those are
            // build-time tools (e.g., cmake) which our bundled toolchain
            // already provides.
            if (entry["host"].as_bool(false)) continue;
        }
        if (!dep.name.empty()) {
            dep.source_id = "vcpkg";
            out.push_back(std::move(dep));
        }
    }
    return out;
}

} // namespace

VcpkgSource::VcpkgSource(VcpkgConfig cfg) : cfg_(std::move(cfg)) {}

Result<Path> VcpkgSource::ensure_root() {
    if (cfg_.vcpkg_root.empty())
        return make_error<Path>("vcpkg: vcpkg_root not configured");

    auto ex = rivet::fs::exists(cfg_.vcpkg_root / ".git");
    if (ex && *ex) {
        // Already cloned. If a baseline is pinned, ensure that commit is checked out.
        if (!cfg_.baseline.empty()) {
            (void)run_git(cfg_.vcpkg_root, {"fetch", "--quiet", "origin"}, "fetch");
            if (auto r = run_git(cfg_.vcpkg_root,
                    {"checkout", "--quiet", cfg_.baseline}, "checkout"); !r)
                return make_error<Path>(r.error().message);
        }
        return cfg_.vcpkg_root;
    }

    if (auto r = rivet::fs::create_dirs(cfg_.vcpkg_root); !r)
        return make_error<Path>(std::format(
            "vcpkg: mkdir {} failed: {}", cfg_.vcpkg_root.string(), r.error().message));

    if (auto r = run_git(cfg_.vcpkg_root.parent_path(),
            {"clone", "--depth=1", kVcpkgRepo, cfg_.vcpkg_root.filename().string()},
            "clone"); !r)
        return make_error<Path>(r.error().message);

    if (!cfg_.baseline.empty()) {
        // Unshallow only if we need a specific baseline that isn't HEAD.
        (void)run_git(cfg_.vcpkg_root, {"fetch", "--unshallow"}, "unshallow");
        if (auto r = run_git(cfg_.vcpkg_root,
                {"checkout", "--quiet", cfg_.baseline}, "checkout"); !r)
            return make_error<Path>(r.error().message);
    }

    return cfg_.vcpkg_root;
}

bool VcpkgSource::handles(const PackageRef& ref) const {
    if (ref.source_id && *ref.source_id == id()) return true;
    // Default backend for any bare name that isn't path/git.
    return !ref.source_id.has_value() && ref.local_path.empty() && ref.git_url.empty();
}

Result<ResolvedPackage> VcpkgSource::resolve(const PackageRef& ref) {
    auto root = ensure_root();
    if (!root) return propagate<ResolvedPackage>(root);

    auto pdir = port_dir(*root, ref.name);
    auto pex  = rivet::fs::exists(pdir);
    if (!pex || !*pex)
        return make_error<ResolvedPackage>(std::format(
            "vcpkg: no port '{}' in {}", ref.name, root->string()));

    auto manifest_path = pdir / "vcpkg.json";
    auto data = rivet::fs::read_file(manifest_path);
    if (!data) return make_error<ResolvedPackage>(std::format(
        "vcpkg: read {} failed: {}", manifest_path.string(), data.error().message));

    std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
    auto manifest = json::parse(text);
    if (!manifest) return make_error<ResolvedPackage>(std::format(
        "vcpkg: parse {} failed: {}", manifest_path.string(), manifest.error().message));

    std::string version = read_version(*manifest);
    if (version.empty())
        return make_error<ResolvedPackage>(std::format(
            "vcpkg: no version field in {}", manifest_path.string()));

    if (!ref.version_constraint.empty() && ref.version_constraint != "*") {
        // Best-effort SemVer match. Date and string versions skip the check.
        if (auto v = parse_semver(version); v) {
            if (!satisfies(ref.version_constraint, *v))
                return make_error<ResolvedPackage>(std::format(
                    "vcpkg: {} {} does not satisfy '{}'",
                    ref.name, version, ref.version_constraint));
        }
    }

    ResolvedPackage out;
    out.name           = ref.name;
    out.version        = version;
    out.source_id      = id();
    out.source_locator = std::format("vcpkg://{}", ref.name);
    out.features       = ref.features;
    out.deps           = read_deps(*manifest);
    return out;
}

Result<PortRecipe> VcpkgSource::fetch(const ResolvedPackage& pkg, const Path& /*cache_dir*/) {
    auto root = ensure_root();
    if (!root) return propagate<PortRecipe>(root);

    // For B3 the recipe just points at the port directory. B4 will wire in
    // the actual upstream source fetch + build dispatch under our triplet.
    PortRecipe r;
    r.source_dir = port_dir(*root, pkg.name);
    r.driver     = BuildDriver::CMake;  // vcpkg ports overwhelmingly use cmake
    return r;
}

} // namespace rivet::pkg
