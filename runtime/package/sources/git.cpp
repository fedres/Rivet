// runtime/package/sources/git.cpp — git package source
#include "git.hpp"
#include "../../../platform/interface/fs.hpp"
#include "../../../platform/interface/process.hpp"

#include <format>

namespace rivet::pkg {

namespace {

// Run `git <args>` in `cwd` and return exit code + stderr.
Result<void> run_git(const Path& cwd, std::vector<std::string> args, std::string_view ctx) {
    rivet::process::SpawnOptions opts;
    opts.args = std::move(args);
    opts.args.insert(opts.args.begin(), "git");
    opts.working_dir    = cwd;
    opts.inherit_env    = true;
    opts.capture_stdout = false;
    opts.capture_stderr = true;

    auto child = rivet::process::spawn(std::move(opts));
    if (!child) return make_error(std::format("{}: spawn git failed: {}",
        ctx, child.error().message));
    auto code = child->wait();
    if (!code) return make_error(std::format("{}: wait failed", ctx));
    if (*code != 0)
        return make_error(std::format("{}: git exited {}: {}",
            ctx, *code, child->stderr_output()));
    return {};
}

// Compute the cache directory name from name + commit-ish.
Path cache_path(const Path& cache_dir, std::string_view name, std::string_view rev) {
    std::string short_rev{rev};
    if (short_rev.size() > 12) short_rev.resize(12);
    return cache_dir / std::format("{}-git-{}", name, short_rev);
}

} // namespace

bool GitSource::handles(const PackageRef& ref) const {
    if (ref.source_id && *ref.source_id == id()) return true;
    return !ref.git_url.empty();
}

Result<ResolvedPackage> GitSource::resolve(const PackageRef& ref) {
    if (ref.git_url.empty())
        return make_error<ResolvedPackage>("git: no url provided for '" + ref.name + "'");

    // For git deps the "version" is whatever ref was requested. The actual
    // commit SHA is recorded after fetch() — for now use the ref as locator.
    ResolvedPackage out;
    out.name           = ref.name;
    out.version        = ref.git_ref.empty() ? "HEAD" : ref.git_ref;
    out.source_id      = id();
    out.source_locator = ref.git_url + "#" + out.version;
    out.features       = ref.features;
    return out;
}

Result<PortRecipe> GitSource::fetch(const ResolvedPackage& pkg, const Path& cache_dir) {
    // Parse the locator back into URL + rev.
    auto hash = pkg.source_locator.find('#');
    std::string url = hash == std::string::npos
        ? pkg.source_locator
        : pkg.source_locator.substr(0, hash);
    std::string rev = hash == std::string::npos
        ? "HEAD"
        : pkg.source_locator.substr(hash + 1);

    Path dest = cache_path(cache_dir, pkg.name, rev);
    auto ex   = rivet::fs::exists(dest);
    bool already_present = (ex && *ex);

    if (!already_present) {
        if (auto r = rivet::fs::create_dirs(dest); !r)
            return make_error<PortRecipe>(std::format(
                "git: mkdir failed: {}", r.error().message));

        // Shallow clone, then fetch the specific rev.
        if (auto r = run_git(dest, {"init", "--quiet"}, "git init"); !r)
            return make_error<PortRecipe>(r.error().message);
        if (auto r = run_git(dest, {"remote", "add", "origin", url}, "git remote add"); !r)
            return make_error<PortRecipe>(r.error().message);
        if (auto r = run_git(dest, {"fetch", "--depth=1", "origin", rev}, "git fetch"); !r) {
            // Fall back to fetching full history if shallow ref didn't resolve
            // (common for arbitrary commit SHAs).
            if (auto r2 = run_git(dest, {"fetch", "origin"}, "git fetch full"); !r2)
                return make_error<PortRecipe>(r2.error().message);
        }
        if (auto r = run_git(dest, {"checkout", "--quiet", "FETCH_HEAD"},
                             "git checkout"); !r)
            return make_error<PortRecipe>(r.error().message);
    }

    PortRecipe recipe;
    recipe.source_dir = dest;

    // Detect build system by file presence.
    auto has = [&](const char* name) {
        auto r = rivet::fs::exists(dest / name);
        return r && *r;
    };
    if      (has("rivet.toml"))     recipe.driver = BuildDriver::Rivet;
    else if (has("CMakeLists.txt")) recipe.driver = BuildDriver::CMake;
    else if (has("meson.build"))    recipe.driver = BuildDriver::Meson;
    else if (has("configure"))      recipe.driver = BuildDriver::Autotools;
    else                            recipe.driver = BuildDriver::Custom;

    return recipe;
}

} // namespace rivet::pkg
