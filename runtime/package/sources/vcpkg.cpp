// runtime/package/sources/vcpkg.cpp — vcpkg backend implementation
#include "vcpkg.hpp"
#include "../../common/json.hpp"
#include "../../toolchain/discovery.hpp"
#include "../../../platform/interface/fs.hpp"
#include "../../../platform/interface/process.hpp"

#include <format>

namespace rivet::pkg {

namespace {

constexpr const char* kVcpkgRepo = "https://github.com/microsoft/vcpkg.git";

// Map our triple namespace to vcpkg's built-in triplet base.
// We then layer a "-rivet" overlay triplet that pins the compiler.
//
// On Windows we deliberately use the *-static base so the install_root
// produces standalone static libs (no msvcp140.dll etc. dropped in bin/).
// That's the deterministic-build story; users can opt into shared via
// future per-dep features.
std::string_view vcpkg_triplet_base(std::string_view rivet_triple) {
    if (rivet_triple.starts_with("x86_64-linux")) return "x64-linux";
    if (rivet_triple.starts_with("arm64-linux"))  return "arm64-linux";
    if (rivet_triple.starts_with("aarch64-linux"))return "arm64-linux";
    if (rivet_triple.starts_with("arm64-apple"))  return "arm64-osx";
    if (rivet_triple.starts_with("x86_64-apple")) return "x64-osx";
    // Windows: rivet ships llvm-mingw, so we use the mingw triplet, not
    // x64-windows-static (MSVC). Both are upstream vcpkg-supported.
    if (rivet_triple.starts_with("x86_64-windows")) return "x64-mingw-static";
    return "x64-linux";
}

#if defined(_WIN32)
constexpr const char* kHostTriple = "x86_64-windows-gnu";
#elif defined(__APPLE__) && defined(__aarch64__)
constexpr const char* kHostTriple = "arm64-apple-macos";
#elif defined(__APPLE__)
constexpr const char* kHostTriple = "x86_64-apple-macos";
#elif defined(__aarch64__)
constexpr const char* kHostTriple = "arm64-linux-gnu";
#else
constexpr const char* kHostTriple = "x86_64-linux-gnu";
#endif
} // namespace

std::string host_vcpkg_triplet() {
    return std::format("{}-rivet", vcpkg_triplet_base(kHostTriple));
}

namespace {

Result<void> write_text(const Path& dst, std::string_view text) {
    rivet::ByteSpan span{
        reinterpret_cast<const std::byte*>(text.data()), text.size()};
    return rivet::fs::write_atomic(dst, span);
}

// Bootstrap the vcpkg CLI binary from the local clone. Idempotent.
// On Windows, the .bat script must be run through cmd.exe — CreateProcessW
// does not auto-shell-out for batch files (unlike ShellExecute).
Result<Path> ensure_vcpkg_cli(const Path& vcpkg_root) {
#if defined(_WIN32)
    Path cli = vcpkg_root / "vcpkg.exe";
    Path bootstrap = vcpkg_root / "bootstrap-vcpkg.bat";
#else
    Path cli = vcpkg_root / "vcpkg";
    Path bootstrap = vcpkg_root / "bootstrap-vcpkg.sh";
#endif

    if (rivet::fs::exists(cli).value_or(false)) return cli;

    if (!rivet::fs::exists(bootstrap).value_or(false))
        return make_error<Path>(std::format(
            "vcpkg: bootstrap script not found at {}", bootstrap.string()));

    rivet::process::SpawnOptions opts;
#if defined(_WIN32)
    // cmd /c "<path>" -disableMetrics
    opts.args = { "cmd.exe", "/c", bootstrap.string(), "-disableMetrics" };
#else
    opts.args = { bootstrap.string(), "-disableMetrics" };
#endif
    opts.working_dir    = vcpkg_root;
    opts.inherit_env    = true;
    opts.capture_stdout = false;
    opts.capture_stderr = true;

    auto ch = rivet::process::spawn(std::move(opts));
    if (!ch) return make_error<Path>(std::format(
        "vcpkg/bootstrap: spawn failed: {}", ch.error().message));
    auto code = ch->wait();
    if (!code) return make_error<Path>("vcpkg/bootstrap: wait failed");
    if (*code != 0)
        return make_error<Path>(std::format(
            "vcpkg/bootstrap: exit {}: {}", *code, ch->stderr_output()));

    if (!rivet::fs::exists(cli).value_or(false))
        return make_error<Path>("vcpkg/bootstrap: CLI binary missing after bootstrap");
    return cli;
}

// Generate the overlay triplet that forces vcpkg's CMake-driven port builds
// to use our bundled clang. Returns the overlay-triplet directory path.
//
// Compiler selection (uniform across platforms now that Windows ships
// llvm-mingw): clang / clang++ / lld / llvm-ar — GNU ABI. The Windows
// special-case for clang-cl / lld-link / llvm-lib is gone, since the
// MSVC ABI is no longer the target.
Result<Path> write_overlay_triplet(const Path& vcpkg_root,
                                    std::string_view triplet_name,
                                    std::string_view rivet_triple,
                                    const toolchain::ToolchainInfo& tc) {
    Path overlay_dir = vcpkg_root / "triplets-rivet";
    if (auto r = rivet::fs::create_dirs(overlay_dir); !r)
        return make_error<Path>(r.error().message);

    // generic_string() converts to forward slashes -- safe in CMake double-quoted
    // strings on every platform. ToolchainInfo already appends .exe on Windows.
    std::string cc_path     = tc.clang().generic_string();
    std::string cxx_path    = tc.clangpp().generic_string();
    std::string ar_path     = tc.llvm_ar().generic_string();
    std::string ld_path     = tc.lld().generic_string();
    // llvm-ar in ranlib mode requires being *invoked* as `llvm-ranlib`
    // (it dispatches on argv[0]). CMake calls `<RANLIB> <archive>` without
    // any operation flag, so pointing RANLIB at llvm-ar makes llvm-ar error
    // with "expected [relpos] for 'a', 'b', or 'i' modifier".
    std::string ranlib_path = tc.llvm_ranlib().generic_string();
#if defined(_WIN32)
    // Windows resource compiler / manifest tool, both shipped by llvm-mingw.
    // vcpkg's mingw triplet doesn't drive these directly, but CMake ports
    // that call enable_language(RC) still look them up.
    std::string rc_path     = (tc.root / "bin" / "llvm-rc.exe").generic_string();
    std::string mt_path     = (tc.root / "bin" / "llvm-mt.exe").generic_string();
#endif

    // Chainload toolchain file: pins CC/CXX/AR to the bundled binaries.
    // On macOS we also probe the Xcode SDK via `xcrun --show-sdk-path` and
    // pin CMAKE_OSX_SYSROOT so the bundled clang can find /usr/include and
    // the system frameworks. Without this, vcpkg's detect_compiler step
    // fails immediately with "vcpkg was unable to detect the active
    // compiler's information" — bundled clang has no idea where the SDK is.
    Path chainload = overlay_dir / std::format("{}-toolchain.cmake", triplet_name);
    std::string chainload_text = std::format(
        "# Generated by Rivet. Pins compilers for vcpkg-driven builds.\n"
        "set(CMAKE_C_COMPILER   \"{}\" CACHE FILEPATH \"\")\n"
        "set(CMAKE_CXX_COMPILER \"{}\" CACHE FILEPATH \"\")\n"
        "set(CMAKE_AR           \"{}\" CACHE FILEPATH \"\")\n"
        "set(CMAKE_RANLIB       \"{}\" CACHE FILEPATH \"\")\n"
        "set(CMAKE_LINKER       \"{}\" CACHE FILEPATH \"\")\n"
#if defined(_WIN32)
        "set(CMAKE_RC_COMPILER  \"{}\" CACHE FILEPATH \"\")\n"
        "set(CMAKE_MT           \"{}\" CACHE FILEPATH \"\")\n"
        // We target the MinGW ABI on Windows now; no CMAKE_MSVC_RUNTIME_LIBRARY
        // pinning needed. Force GNU-style linker flags via fuse-ld=lld so
        // CMake's MinGW driver picks ld.lld instead of the system's GNU ld
        // (which probably isn't installed).
        "foreach(_v EXE SHARED MODULE)\n"
        "    set(CMAKE_${{_v}}_LINKER_FLAGS_INIT \"-fuse-ld=lld\")\n"
        "endforeach()\n"
#endif
        "if(APPLE AND NOT CMAKE_OSX_SYSROOT)\n"
        "    execute_process(COMMAND xcrun --show-sdk-path\n"
        "                    OUTPUT_VARIABLE _rivet_sdk_path\n"
        "                    OUTPUT_STRIP_TRAILING_WHITESPACE\n"
        "                    RESULT_VARIABLE _rivet_sdk_rc)\n"
        "    if(_rivet_sdk_rc EQUAL 0 AND EXISTS \"${{_rivet_sdk_path}}\")\n"
        "        set(CMAKE_OSX_SYSROOT \"${{_rivet_sdk_path}}\" CACHE PATH \"\")\n"
        "    endif()\n"
        "endif()\n"
        // Force the bundled lld on Apple. The bundled clang sets up DYLD
        // search paths that include the toolchain's lib/ — when clang then
        // invokes Apple's /usr/bin/ld, dyld loads rivet's libc++ for it
        // instead of Apple's, and Apple's ld immediately crashes with
        // "Symbol not found: __ZdaPv" (operator delete[]). Bundled lld
        // (ld64.lld) is linked against the same libc++ rivet ships, so the
        // mismatch doesn't happen. Verified by the macOS smoke test logs.
        "if(APPLE)\n"
        "    foreach(_v EXE SHARED MODULE)\n"
        "        set(CMAKE_${{_v}}_LINKER_FLAGS_INIT \"-fuse-ld=lld\")\n"
        "    endforeach()\n"
        "endif()\n",
        cc_path, cxx_path, ar_path, ranlib_path, ld_path
#if defined(_WIN32)
        , rc_path, mt_path
#endif
        );
    if (auto r = write_text(chainload, chainload_text); !r)
        return make_error<Path>(r.error().message);

    // The triplet file itself — inherits from vcpkg's built-in, then sets
    // VCPKG_CHAINLOAD_TOOLCHAIN_FILE to our chainload.
    //
    // vcpkg keeps its mainline triplets under `triplets/` and community-
    // maintained ones under `triplets/community/`. We use x64-mingw-static
    // on Windows now, which lives in community/. Pick the right sub-path
    // per triplet so the include succeeds in both cases.
    Path triplet_file = overlay_dir / std::format("{}.cmake", triplet_name);
    std::string base    = std::string(vcpkg_triplet_base(rivet_triple));
    const char* base_subdir =
        (base == "x64-mingw-static" || base == "x64-mingw-dynamic")
            ? "triplets/community" : "triplets";
    std::string content = std::format(
        "# Generated by Rivet for {}\n"
        "include(\"${{CMAKE_CURRENT_LIST_DIR}}/../{}/{}.cmake\")\n"
        "set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE \"${{CMAKE_CURRENT_LIST_DIR}}/{}-toolchain.cmake\")\n",
        rivet_triple, base_subdir, base, triplet_name);
    if (auto r = write_text(triplet_file, content); !r)
        return make_error<Path>(r.error().message);

    return overlay_dir;
}

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

Result<PortRecipe> VcpkgSource::fetch(const ResolvedPackage& pkg, const Path& cache_dir) {
    auto root = ensure_root();
    if (!root) return propagate<PortRecipe>(root);

    if (cfg_.rivet_home.empty())
        return make_error<PortRecipe>(
            "vcpkg/fetch: rivet_home not configured — cannot locate bundled toolchain");

    // Find the bundled toolchain. vcpkg's CMake-driven port builds get pinned
    // to this clang via the overlay triplet's chainload toolchain file.
    auto tc = toolchain::find_active(cfg_.rivet_home);
    if (!tc) return make_error<PortRecipe>(std::format(
        "vcpkg/fetch: no active toolchain ({}); "
        "run 'rivet toolchain install <version>' first", tc.error().message));

    auto cli = ensure_vcpkg_cli(*root);
    if (!cli) return propagate<PortRecipe>(cli);

    std::string triplet_name = host_vcpkg_triplet();
    auto overlay = write_overlay_triplet(*root, triplet_name, kHostTriple, *tc);
    if (!overlay) return propagate<PortRecipe>(overlay);

    // Per-build sandbox under cache_dir/vcpkg-installed/<triplet>/ — keeps
    // installs isolated across rivet projects sharing the same vcpkg clone.
    Path install_root = cache_dir / "vcpkg-installed";
    if (auto r = rivet::fs::create_dirs(install_root); !r)
        return make_error<PortRecipe>(std::format(
            "vcpkg/fetch: mkdir {} failed: {}",
            install_root.string(), r.error().message));

    // vcpkg install <port> --triplet=<our> --overlay-triplets=<dir>
    //                       --x-install-root=<install_root>
    //                       --no-print-usage --disable-metrics
    rivet::process::SpawnOptions opts;
    opts.args = {
        cli->string(), "install",
        pkg.name,
        std::format("--triplet={}",         triplet_name),
        std::format("--overlay-triplets={}", overlay->string()),
        std::format("--x-install-root={}",   install_root.string()),
        "--no-print-usage",
        "--disable-metrics",
        "--clean-after-build",
    };
    opts.working_dir    = *root;
    opts.inherit_env    = true;
    opts.capture_stdout = false;
    opts.capture_stderr = true;
    // Inherited env is supplemented with these (vcpkg respects them).
    opts.env["VCPKG_FORCE_SYSTEM_BINARIES"] = "1";

    auto ch = rivet::process::spawn(std::move(opts));
    if (!ch) return make_error<PortRecipe>(std::format(
        "vcpkg/install: spawn failed: {}", ch.error().message));
    auto code = ch->wait();
    if (!code) return make_error<PortRecipe>("vcpkg/install: wait failed");
    if (*code != 0)
        return make_error<PortRecipe>(std::format(
            "vcpkg/install '{}' exit {}: {}",
            pkg.name, *code, ch->stderr_output()));

    PortRecipe recipe;
    recipe.driver       = BuildDriver::Prebuilt;
    recipe.install_root = install_root / triplet_name;   // vcpkg's per-triplet tree
    recipe.source_dir   = port_dir(*root, pkg.name);     // for forensics / patches
    return recipe;
}

} // namespace rivet::pkg
