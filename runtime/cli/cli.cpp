// runtime/cli/cli.cpp — Rivet CLI command router implementation
#include "cli.hpp"
#include "../../platform/interface/terminal.hpp"
#include "../../platform/interface/env.hpp"
#include "../../platform/interface/fs.hpp"
#include "../../platform/interface/time.hpp"
#include "runtime/version.hpp"
#include "../build/graph.hpp"
#include "../build/executor.hpp"
#include "../build/scheduler.hpp"
#include "../build/pkgconfig.hpp"
#include "../build/multi_target.hpp"
#include "../build/cmake_drive.hpp"
#include "../toolchain/sdk.hpp"
#include "../archive/tar_zst.hpp"
#include "../cache/store.hpp"
#include "../cache/key.hpp"
#include "../package/manifest.hpp"
#include "../package/lockfile.hpp"
#include "../package/default_sources.hpp"
#include "../package/resolver.hpp"
#include "../package/sources/vcpkg.hpp"
#include "../toolchain/discovery.hpp"
#include "../toolchain/compile.hpp"
#include "../daemon/daemon.hpp"
#include "../../platform/interface/net.hpp"

#include <cstring>
#include <iostream>
#include <format>
#include <thread>

namespace rivet::cli {

std::string_view Context::program_name() const {
    return argc > 0 ? argv[0] : "rivet";
}

std::string_view Context::subcommand() const {
    return argc > 1 ? argv[1] : "";
}

std::vector<std::string_view> Context::args_after_subcommand() const {
    std::vector<std::string_view> result;
    for (int i = 2; i < argc; ++i) result.emplace_back(argv[i]);
    return result;
}

static void print_usage() {
    std::cout <<
        "rivet — a self-contained C++ build system\n"
        "\n"
        "USAGE:\n"
        "  rivet <command> [options]\n"
        "\n"
        "COMMANDS:\n"
        "  build       Build the current project\n"
        "  test        Run tests\n"
        "  run         Build and run the project binary\n"
        "  exec        Run a binary shipped by a dependency (npm-exec-style)\n"
        "  add         Add a dependency\n"
        "  remove      Remove a dependency\n"
        "  fetch       Fetch + build all locked deps (--locked/--frozen for CI)\n"
        "  new         Create a new project from a template\n"
        "  publish     Publish a package to the registry\n"
        "  cache       Manage the local build cache\n"
        "  daemon      Manage the compiler daemon\n"
        "  toolchain   Manage bundled toolchain versions\n"
        "  fuzz        Run a fuzz target with libFuzzer\n"
        "  self-update Update rivet to the latest released version\n"
        "  version     Print version information\n"
        "  help        Show this help message\n"
        "\n"
        "Run 'rivet help <command>' for help on a specific command.\n";
}

int run(const Context& ctx) {
    auto sub = ctx.subcommand();

    if (sub.empty() || sub == "help"     || sub == "--help" || sub == "-h")
        return cmd_help(ctx);
    if (sub == "version" || sub == "--version" || sub == "-V")
        return cmd_version(ctx);
    if (sub == "build")    return cmd_build(ctx);
    if (sub == "test")     return cmd_test(ctx);
    if (sub == "run")      return cmd_run(ctx);
    if (sub == "exec")     return cmd_exec(ctx);
    if (sub == "add")      return cmd_add(ctx);
    if (sub == "remove")   return cmd_remove(ctx);
    if (sub == "fetch")    return cmd_fetch(ctx);
    if (sub == "new")      return cmd_new(ctx);
    if (sub == "publish")  return cmd_publish(ctx);
    if (sub == "cache")       return cmd_cache(ctx);
    if (sub == "daemon")      return cmd_daemon(ctx);
    if (sub == "toolchain")   return cmd_toolchain(ctx);
    if (sub == "fuzz")        return cmd_fuzz(ctx);
    if (sub == "self-update") return cmd_self_update(ctx);

    std::cerr << "error: unknown command '" << sub << "'\n"
              << "Run 'rivet help' for a list of commands.\n";
    return 1;
}

int cmd_help(const Context& /*ctx*/) {
    print_usage();
    return 0;
}

int cmd_version(const Context& /*ctx*/) {
    std::cout << "rivet " << rivet::kVersion
              << " (" << rivet::kTargetTriple << ")";
    if (!rivet::kGitHash.empty())
        std::cout << " [" << rivet::kGitHash << "]";
    std::cout << "\n";
    return 0;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

static bool has_flag(const std::vector<std::string_view>& args, std::string_view flag) {
    for (const auto& a : args) if (a == flag) return true;
    return false;
}

static std::optional<std::string_view>
flag_value(const std::vector<std::string_view>& args, std::string_view flag) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == flag) return args[i + 1];
    return std::nullopt;
}

// ─── cmd_build ───────────────────────────────────────────────────────────────

// Check if `output` is up to date relative to all deps listed in `dep_file`.
// Returns true iff output exists and every listed dep is not newer than output.
static bool is_up_to_date(const Path& output, const Path& dep_file) {
    auto out_stat = rivet::fs::stat(output);
    if (!out_stat) return false;
    int64_t out_mtime = out_stat->mtime_ns;

    auto dep_data = rivet::fs::read_file(dep_file);
    if (!dep_data) return false;  // no dep file yet → must build

    std::string_view text{reinterpret_cast<const char*>(dep_data->data()), dep_data->size()};

    // Skip "output: " header to reach the dependency list.
    auto colon = text.find(':');
    if (colon == std::string_view::npos) return false;
    text.remove_prefix(colon + 1);

    // Walk whitespace-separated tokens; '\' at end-of-line is a continuation.
    std::string dep;
    dep.reserve(256);
    for (char c : text) {
        if (c == '\\' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            if (!dep.empty()) {
                if (dep != "\\") {
                    auto s = rivet::fs::stat(Path{dep});
                    if (!s || s->mtime_ns > out_mtime) return false;
                }
                dep.clear();
            }
        } else {
            dep += c;
        }
    }
    if (!dep.empty() && dep != "\\") {
        auto s = rivet::fs::stat(Path{dep});
        if (!s || s->mtime_ns > out_mtime) return false;
    }
    return true;
}

// Recursively collect C++ source files under `dir` into `out`.
static void collect_sources(const Path& dir, std::vector<Path>& out) {
    auto entries_r = rivet::fs::list_dir(dir);
    if (!entries_r) return;
    for (const auto& entry : *entries_r) {
        auto stat_r = rivet::fs::stat(entry);
        if (stat_r && stat_r->is_dir) {
            collect_sources(entry, out);
            continue;
        }
        auto ext = entry.extension().string();
        if (ext == ".cpp" || ext == ".cxx" || ext == ".cc")
            out.push_back(entry);
    }
}

int cmd_build(const Context& ctx) {
    auto args    = ctx.args_after_subcommand();
    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();

    // 1. Find and parse the manifest. If no rivet.toml exists but a
    // CMakeLists.txt is present in cwd, fall back to the cmake-driving
    // path (M2 first cut) so users can `git clone` an existing CMake
    // project and `rivet build` it directly.
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        if (rivet::fs::exists(cwd / "CMakeLists.txt").value_or(false)) {
            // Need toolchain + SDK detection for the cmake path too.
            auto home_r = rivet::env::rivet_home();
            if (!home_r) {
                std::cerr << "error: cannot determine rivet home: "
                          << home_r.error().message << "\n";
                return 1;
            }
            auto tc_r = rivet::toolchain::find_active(*home_r);
            if (!tc_r) {
                std::cerr << "error: no toolchain available: "
                          << tc_r.error().message << "\n"
                          << "hint: run 'rivet toolchain install <version>'.\n";
                return 1;
            }
            if (const auto& sdk = rivet::toolchain::detect_host_sdk(); !sdk.present) {
                std::cerr << "error: platform SDK not detected.\n\n"
                          << sdk.hint << "\n";
                return 1;
            }

            std::string profile_name{flag_value(args, "--profile").value_or("debug")};
            rivet::build::CmakeDriveOptions copts;
            copts.project_dir    = cwd;
            copts.toolchain_root = tc_r->root;
            copts.profile        = profile_name;
            // Auto-pick the per-project install tree at <rivet_home>/
            // cache/deps/<dir-name>/vcpkg-installed/<triplet> if present —
            // populated by `rivet add` / `rivet fetch` ahead of this step.
            // We don't have a manifest.name; use the dir's filename.
            auto proj = cwd.filename().string();
            if (!proj.empty()) {
                Path candidate = *home_r / "cache" / "deps" / proj
                                / "vcpkg-installed" / rivet::pkg::host_vcpkg_triplet();
                if (rivet::fs::exists(candidate).value_or(false))
                    copts.extra_prefix = candidate;
            }
            std::cout << std::format(
                "no rivet.toml found; driving cmake with bundled "
                "clang {} + ninja under {}\n",
                tc_r->version, (cwd / ".rivet" / "cmake").string());
            auto r = rivet::build::build_via_cmake(copts, *tc_r);
            if (!r) {
                std::cerr << "error: " << r.error().message << "\n";
                return 1;
            }
            return 0;
        }
        std::cerr << "error: " << manifest_r.error().message << "\n"
                  << "hint: create a rivet.toml in this directory or a parent,\n"
                  << "      or run from a directory containing a CMakeLists.txt.\n";
        return 1;
    }
    const auto& manifest = *manifest_r;

    // ── Workspace dispatch (C3 phase B) ──────────────────────────────
    // If this manifest declares [workspace] and ISN'T also a package
    // (i.e. has no [package] block), recurse into each member by
    // re-invoking ourselves with cwd=<member-dir>. Forward the --profile
    // flag through.
    //
    // Workspace + package hybrid (the manifest has both) is treated as
    // a regular package build for now — the workspace is only acted
    // on when the manifest is a *pure* workspace root. Mirrors cargo's
    // "virtual workspace" concept.
    if (manifest.workspace.has_value() && manifest.name.empty()) {
        const auto& ws = *manifest.workspace;
        if (ws.members.empty()) {
            std::cerr << "error: [workspace] has no members.\n";
            return 1;
        }
        auto self = rivet::process::self_exe();
        if (!self) {
            std::cerr << "error: cannot resolve rivet binary path: "
                      << self.error().message << "\n";
            return 1;
        }
        std::string profile{flag_value(args, "--profile").value_or("debug")};
        int failed = 0;
        for (const auto& m : ws.members) {
            Path member_dir = manifest.root_dir / m;
            if (!rivet::fs::exists(member_dir / "rivet.toml").value_or(false)) {
                std::cerr << "warning: workspace member '" << m
                          << "' has no rivet.toml — skipping\n";
                continue;
            }
            std::cout << "\n══ building workspace member: " << m << " ══\n";
            rivet::process::SpawnOptions opts;
            opts.args        = {self->string(), "build", "--profile=" + profile};
            opts.working_dir = member_dir;
            opts.inherit_env = true;
            auto child = rivet::process::spawn(std::move(opts));
            if (!child) {
                std::cerr << "error: cannot spawn child rivet for '"
                          << m << "': " << child.error().message << "\n";
                ++failed;
                continue;
            }
            auto code = child->wait();
            if (!code || *code != 0) {
                std::cerr << "error: workspace member '" << m
                          << "' failed (exit " << code.value_or(-1) << ")\n";
                ++failed;
            }
        }
        std::cout << "\n══ workspace build: " << ws.members.size() - failed
                  << " ok, " << failed << " failed ══\n";
        return failed == 0 ? 0 : 1;
    }

    if (auto vr = rivet::pkg::validate(manifest); !vr) {
        std::cerr << "error: " << vr.error().message << "\n";
        return 1;
    }

    // 2. Discover toolchain.
    auto rivet_home_r = rivet::env::rivet_home();
    if (!rivet_home_r) {
        std::cerr << "error: cannot determine rivet home: "
                  << rivet_home_r.error().message << "\n";
        return 1;
    }
    const Path& rivet_home = *rivet_home_r;

    auto tc_r = rivet::toolchain::find_active(rivet_home);
    if (!tc_r) {
        std::cerr << "error: no toolchain available: " << tc_r.error().message << "\n"
                  << "hint: run 'rivet toolchain install <version>' to install one.\n";
        return 1;
    }
    const auto& tc = *tc_r;

    // Platform SDK check (Apple frameworks / Windows Kits). Rivet ships
    // its own clang and libc++, but Apple/Microsoft don't let us
    // redistribute the platform SDK — same prereq cargo/zig/bun all have.
    if (const auto& sdk = rivet::toolchain::detect_host_sdk(); !sdk.present) {
        std::cerr << "error: platform SDK not detected.\n\n"
                  << sdk.hint << "\n";
        return 1;
    }

    // ── [build] system = "cmake": manifest exists but the project's build
    // rules live in a CMakeLists.txt rather than rivet.toml. The user gets
    // rivet's package management (rivet add / rivet fetch) plus cmake's
    // build expressiveness. Dispatch to the cmake-drive path with the
    // per-project vcpkg-installed tree pre-wired into CMAKE_PREFIX_PATH.
    if (manifest.build.system == rivet::pkg::BuildSystem::CMake) {
        if (!rivet::fs::exists(manifest.root_dir / "CMakeLists.txt").value_or(false)) {
            std::cerr << "error: [build] system = \"cmake\" but no CMakeLists.txt "
                         "at " << manifest.root_dir.string() << "\n";
            return 1;
        }
        rivet::build::CmakeDriveOptions copts;
        copts.project_dir    = manifest.root_dir;
        copts.toolchain_root = tc.root;
        copts.profile        = std::string{flag_value(args, "--profile").value_or("debug")};
        Path candidate = *rivet_home_r / "cache" / "deps" / manifest.name
                         / "vcpkg-installed" / rivet::pkg::host_vcpkg_triplet();
        if (rivet::fs::exists(candidate).value_or(false))
            copts.extra_prefix = candidate;
        std::cout << std::format(
            "Building {} {} [{}] via cmake-drive with clang {}\n",
            manifest.name, manifest.version, copts.profile, tc.version);
        auto r = rivet::build::build_via_cmake(copts, tc);
        if (!r) {
            std::cerr << "error: " << r.error().message << "\n";
            return 1;
        }
        return 0;
    }

    // 3. Determine build profile and configuration.
    auto profile_name = std::string{flag_value(args, "--profile").value_or("debug")};
    bool is_release   = (profile_name == "release");

    rivet::build::BuildConfig cfg;
    cfg.cxx_std = manifest.build.cxx_std;
    cfg.opt     = is_release ? rivet::build::OptLevel::O2 : rivet::build::OptLevel::Debug;
    cfg.debug   = !is_release;

    // Apply profile settings from manifest or built-in presets.
    if (auto it = manifest.profiles.find(profile_name); it != manifest.profiles.end()) {
        const auto& prof = it->second;
        cfg.sanitizers = prof.sanitizers;
        cfg.lto        = prof.lto;
        cfg.debug      = prof.debug;
        if (prof.opt_level.has_value()) {
            switch (*prof.opt_level) {
                case 0: cfg.opt = rivet::build::OptLevel::Debug; break;
                case 1: cfg.opt = rivet::build::OptLevel::O1;    break;
                case 2: cfg.opt = rivet::build::OptLevel::O2;    break;
                case 3: cfg.opt = rivet::build::OptLevel::O3;    break;
                default: break;
            }
        }
        for (const auto& f : prof.extra_flags) cfg.extra_flags.push_back(f);
    } else if (profile_name == "asan") {
        cfg.sanitizers = {"address", "undefined"};
    } else if (profile_name == "tsan") {
        cfg.sanitizers = {"thread"};
    } else if (profile_name == "msan") {
        cfg.sanitizers = {"memory"};
    } else if (profile_name == "ubsan") {
        cfg.sanitizers = {"undefined"};
    }

    // 3a. Source layout conventions (cargo-like). All optional — if the
    // directory doesn't exist we just skip it.
    //   src/          — sources (mandatory; scanned recursively below)
    //   include/      — public headers, auto-added to compile paths so
    //                   sources can `#include "<project>/foo.h"`
    //   The project root itself is also added so `#include "config.h"`
    //   etc. resolve against the project layout.
    {
        Path include_dir = manifest.root_dir / "include";
        if (rivet::fs::exists(include_dir).value_or(false))
            cfg.include_paths.push_back(include_dir);
        cfg.include_paths.push_back(manifest.root_dir);
    }

    // 3b. Wire fetched dependencies. After `rivet fetch` the per-triplet
    // install tree sits under <rivet_home>/cache/deps/<root>/vcpkg-installed/
    // <triplet>/{include,lib,lib/pkgconfig}.
    //
    // We prefer pkg-config metadata (in lib/pkgconfig/*.pc) because it tells
    // us EXACTLY which `-l<name>` to emit and which transitive deps to pull
    // in. Falls back to file-glob for old vcpkg ports that don't ship .pc
    // files — those just get every static archive linked, which is what we
    // did before.
    struct InstalledDeps {
        std::vector<Path>        include_dirs;
        std::vector<Path>        lib_dirs;
        std::vector<Path>        link_libs;     // direct file refs (.a / .lib)
        std::vector<std::string> link_flags;    // pkg-config-derived `-lfoo`, `-pthread`, etc.
        std::vector<std::string> compile_flags; // pkg-config-derived `-DXYZ`, `-pthread`, etc.
    };
    auto installed_deps = [&]() -> InstalledDeps {
        InstalledDeps deps;
        if (manifest.dependencies.empty()) return deps;
        auto home_r = rivet::env::rivet_home();
        if (!home_r) return deps;
        Path install_root = *home_r / "cache" / "deps" / manifest.name
                            / "vcpkg-installed" / rivet::pkg::host_vcpkg_triplet();
        Path include_dir  = install_root / "include";
        Path lib_dir      = install_root / "lib";
        Path pkgcfg_dir   = lib_dir / "pkgconfig";
        if (rivet::fs::exists(include_dir).value_or(false))
            deps.include_dirs.push_back(include_dir);
        if (!rivet::fs::exists(lib_dir).value_or(false))
            return deps;
        deps.lib_dirs.push_back(lib_dir);

        // Try pkg-config first: one .pc per declared dep, then resolve
        // transitive Requires.
        bool used_pkgconfig = false;
        if (rivet::fs::exists(pkgcfg_dir).value_or(false)) {
            std::vector<std::string> roots;
            roots.reserve(manifest.dependencies.size());
            for (const auto& [name, _] : manifest.dependencies) roots.push_back(name);
            // Static link: include `Requires.private` since we're producing
            // an executable that must resolve every symbol transitively.
            auto resolved = rivet::build::resolve_pkgs(roots, {pkgcfg_dir}, /*static_link=*/true);
            if (resolved && !resolved->resolved.empty()) {
                used_pkgconfig = true;
                // Translate `-I`/`-L` from cflags/libs into include/lib dirs;
                // leave the other flags alone so they reach clang verbatim.
                for (const auto& f : resolved->cflags) {
                    if (f.starts_with("-I")) {
                        Path p{f.substr(2)};
                        // Skip the install include dir we already added.
                        if (p != include_dir) deps.include_dirs.push_back(std::move(p));
                    } else {
                        deps.compile_flags.push_back(f);
                    }
                }
                // Translate POSIX pkg-config flags to per-platform link
                // tokens. pkg-config files always speak POSIX (`-lfoo`,
                // `-framework`, `-pthread`); on Windows MSVC ABI the
                // canonical form is `foo.lib` as a positional arg, and
                // `-framework`/`-pthread` don't exist. macOS keeps the
                // POSIX form (clang understands `-framework Foo` and
                // `-lfoo` natively); Linux ditto.
                for (size_t i = 0; i < resolved->libs.size(); ++i) {
                    const auto& f = resolved->libs[i];
                    if (f.starts_with("-L")) {
                        Path p{f.substr(2)};
                        if (p != lib_dir) deps.lib_dirs.push_back(std::move(p));
                        continue;
                    }
                    if (f == "-framework" && i + 1 < resolved->libs.size()) {
                        // Apple-only two-token sequence. On other
                        // platforms drop the pair entirely — referencing
                        // a framework on Linux/Windows is a no-op at best.
#if defined(__APPLE__)
                        deps.link_flags.push_back(f);
                        deps.link_flags.push_back(resolved->libs[i + 1]);
#endif
                        ++i;  // skip the framework name regardless
                        continue;
                    }
                    if (f == "-pthread") {
#if !defined(_WIN32)
                        deps.link_flags.push_back(f);
#endif
                        continue;
                    }
                    if (f.starts_with("-l")) {
                        // clang++ accepts `-lfoo` on every host, including
                        // Windows MSVC ABI — the driver knows to translate
                        // to `foo.lib` when invoking lld-link. (Our earlier
                        // attempt at translating ourselves emitted
                        // `fmt.lib` as a positional arg that clang then
                        // treated as a filename relative to cwd and lost.)
                        deps.link_flags.push_back(f);
                        continue;
                    }
                    // Pass everything else (raw paths, `-static`, etc.)
                    // through verbatim.
                    deps.link_flags.push_back(f);
                }
            }
        }

        // Fallback for ports without .pc files: just glob the install tree.
        if (!used_pkgconfig) {
            if (auto entries = rivet::fs::list_dir(lib_dir)) {
                for (const auto& e : *entries) {
                    auto ext = e.extension().string();
                    if (ext == ".a" || ext == ".so" || ext == ".dylib" || ext == ".lib")
                        deps.link_libs.push_back(e);
                }
            }
        }
        return deps;
    }();

    for (const auto& d : installed_deps.include_dirs)
        cfg.include_paths.push_back(d);
    for (const auto& f : installed_deps.compile_flags)
        cfg.extra_flags.push_back(f);

    if (!installed_deps.include_dirs.empty()) {
        std::cout << std::format(
            "Using {} dep include dir(s); {} link lib(s); {} link flag(s)\n",
            installed_deps.include_dirs.size(),
            installed_deps.link_libs.size(),
            installed_deps.link_flags.size());
    }

    std::cout << std::format("Building {} {} [{}] with clang {}\n",
        manifest.name, manifest.version, profile_name, tc.version);

    // 4. Open cache (non-fatal if unavailable).
    std::optional<rivet::cache::Store> cache_store;
    if (auto cache_dir_r = rivet::env::cache_dir()) {
        if (auto store_r = rivet::cache::Store::open(*cache_dir_r / "rivet" / "build"))
            cache_store = std::move(*store_r);
    }

    // ─── Multi-target build path (M1: rivet self-build, large projects) ──
    //
    // If the manifest declares [[lib]] / [[bin]] / [[test]] / [[vendor]]
    // targets, dispatch to the multi-artefact engine. Single-binary
    // projects (hello-fmt) leave manifest.targets empty and fall through
    // to the original src/-scan path below.
    if (!manifest.targets.empty()) {
        rivet::build::BuildGraph graph;
        rivet::build::InstalledExternalDeps ext;
        ext.include_dirs = installed_deps.include_dirs;
        ext.lib_dirs     = installed_deps.lib_dirs;
        ext.link_libs    = installed_deps.link_libs;
        ext.link_flags   = installed_deps.link_flags;

        std::optional<rivet::cache::Store> cache_store_mt;
        if (auto cache_dir_r = rivet::env::cache_dir()) {
            if (auto store_r = rivet::cache::Store::open(*cache_dir_r / "rivet" / "build"))
                cache_store_mt = std::move(*store_r);
        }

        auto artefacts = rivet::build::build_targets(
            manifest, tc, cfg, ext, profile_name, graph,
            cache_store_mt ? &*cache_store_mt : nullptr);
        if (!artefacts) {
            std::cerr << "error: " << artefacts.error().message << "\n";
            return 1;
        }

        auto t_start = rivet::time::now();
        std::size_t jobs = std::thread::hardware_concurrency();
        auto on_progress = [](const rivet::build::TaskResult& r) {
            if (r.cache_hit)      std::cout << "  \033[34m●\033[0m " << r.task_id << " (cached)\n";
            else if (r.success)   std::cout << "  \033[32m✓\033[0m " << r.task_id << "\n";
            else std::cerr << "  \033[31m✗\033[0m " << r.task_id
                            << "\n" << r.stderr_out << "\n";
        };
        rivet::cache::Store* cache_ptr = cache_store_mt ? &*cache_store_mt : nullptr;
        rivet::build::Executor executor{graph, jobs, on_progress, cache_ptr};
        auto summary = executor.run();
        auto elapsed = rivet::time::elapsed(t_start);

        std::cout << std::format("\n{} compiled, {} cached, {} failed  [{:.1f}s]\n",
            summary.succeeded, summary.cached, summary.failed,
            static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) / 1000.0);

        if (summary.failed == 0) {
            for (const auto& a : *artefacts) {
                const char* tag =
                    a.kind == rivet::pkg::TargetKind::Lib    ? "lib"   :
                    a.kind == rivet::pkg::TargetKind::Vendor ? "lib"   :
                    a.kind == rivet::pkg::TargetKind::Test   ? "test"  : "bin";
                std::cout << std::format("  [{:<4}] {} → {}\n", tag, a.name,
                                          a.artifact_path.string());
            }
        }
        return summary.failed == 0 ? 0 : 1;
    }

    // 5. Collect source files (recursive scan of src/).
    auto src_dir = manifest.root_dir / "src";
    if (!rivet::fs::exists(src_dir).value_or(false)) {
        std::cout << "warning: no src/ directory found — nothing to build.\n";
        return 0;
    }

    std::vector<Path> sources;
    collect_sources(src_dir, sources);

    if (sources.empty()) {
        std::cout << "warning: no C++ source files found in src/ — nothing to build.\n";
        return 0;
    }

    // 6. Build the compile task graph.
    rivet::build::BuildGraph graph;
    std::vector<rivet::build::TaskId> obj_task_ids;
    std::vector<Path>                 obj_paths;

    auto obj_dir = manifest.root_dir / ".rivet" / "build" / profile_name / "obj";

    for (const auto& src : sources) {
        // .rivet/build/<profile>/obj/<rel_path>.o
        auto rel      = src.lexically_relative(manifest.root_dir);
        auto out_path = obj_dir / (rel.string() + ".o");

        // Incremental build: skip recompile if output and all deps are fresh.
        auto dep_file = out_path.parent_path() / (out_path.filename().string() + ".d");
        if (is_up_to_date(out_path, dep_file)) {
            rivet::build::TaskNode phony;
            phony.name    = src.filename().string() + " (up to date)";
            phony.kind    = rivet::build::TaskKind::Phony;
            phony.outputs = {{ out_path, true }};
            auto id = graph.add(std::move(phony));
            obj_task_ids.push_back(id);
            obj_paths.push_back(out_path);
            continue;
        }

        auto cj    = rivet::toolchain::compile_job_from(src, out_path, cfg, tc);
        auto cmd_r = rivet::toolchain::make_compile_command(cj, tc);
        if (!cmd_r) {
            std::cerr << "error building compile command: " << cmd_r.error().message << "\n";
            return 1;
        }

        rivet::build::TaskNode node;
        node.name    = src.filename().string();
        node.kind    = rivet::build::TaskKind::Compile;
        node.outputs = {{ out_path, true }};
        node.command = std::move(*cmd_r);

        // Hash source file for cache key derivation.
        std::string src_hash;
        if (auto hr = rivet::cache::sha256_file(src)) src_hash = std::move(*hr);
        node.inputs = {{ src, std::move(src_hash) }};

        // Derive cache key (command + input hashes must be populated first).
        if (auto kr = rivet::cache::derive_key(node, tc.version, cfg.target_triple))
            node.cache_key = std::move(*kr);

        auto id = graph.add(std::move(node));
        obj_task_ids.push_back(id);
        obj_paths.push_back(out_path);
    }

    // 7. Link step: all object files → binary.
    auto bin_dir    = manifest.root_dir / ".rivet" / "build" / profile_name / "bin";
    auto bin_output = bin_dir / manifest.name;

    rivet::toolchain::LinkJob lj;
    lj.inputs        = obj_paths;
    lj.output        = bin_output;
    lj.target_triple = cfg.target_triple;
    lj.lto           = cfg.lto;
    lj.sanitizers    = cfg.sanitizers;
    for (const auto& p : installed_deps.lib_dirs)   lj.lib_search_paths.push_back(p.string());
    for (const auto& l : installed_deps.link_libs)  lj.link_libs.push_back(l);
    for (const auto& f : installed_deps.link_flags) lj.flags.push_back(f);

    auto link_cmd_r = rivet::toolchain::make_link_command(lj, tc);
    if (!link_cmd_r) {
        std::cerr << "error building link command: " << link_cmd_r.error().message << "\n";
        return 1;
    }

    rivet::build::TaskNode link_node;
    link_node.name    = manifest.name + " [link]";
    link_node.kind    = rivet::build::TaskKind::Link;
    link_node.deps    = obj_task_ids;
    for (const auto& o : obj_paths) link_node.inputs.push_back({ o, "" });
    link_node.outputs = {{ bin_output, true }};
    link_node.command = std::move(*link_cmd_r);
    graph.add(std::move(link_node));

    // 8. Execute.
    auto t_start = rivet::time::now();
    std::size_t jobs = std::thread::hardware_concurrency();

    auto on_progress = [](const rivet::build::TaskResult& r) {
        if (r.cache_hit)
            std::cout << "  \033[34m●\033[0m " << r.task_id << " (cached)\n";
        else if (r.success)
            std::cout << "  \033[32m✓\033[0m " << r.task_id << "\n";
        else
            std::cerr << "  \033[31m✗\033[0m " << r.task_id
                      << "\n" << r.stderr_out << "\n";
    };

    rivet::cache::Store* cache_ptr = cache_store ? &*cache_store : nullptr;
    rivet::build::Executor executor{graph, jobs, on_progress, cache_ptr};
    auto summary = executor.run();
    auto elapsed = rivet::time::elapsed(t_start);

    std::cout << std::format("\n{} compiled, {} cached, {} failed  [{:.1f}s]\n",
        summary.succeeded,
        summary.cached,
        summary.failed,
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) / 1000.0);

    if (summary.failed == 0)
        std::cout << std::format("  Binary: {}\n", bin_output.string());

    return summary.failed == 0 ? 0 : 1;
}

// ─── cmd_test ────────────────────────────────────────────────────────────────

int cmd_test(const Context& ctx) {
    // Build first (reuse cmd_build).
    int build_rc = cmd_build(ctx);
    if (build_rc != 0) return build_rc;

    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) return 1;
    const auto& manifest = *manifest_r;

    auto args       = ctx.args_after_subcommand();
    auto profile    = std::string{flag_value(args, "--profile").value_or("debug")};
    auto tests_dir  = manifest.root_dir / "tests";

    if (!rivet::fs::exists(tests_dir).value_or(false)) {
        std::cout << "warning: no tests/ directory found.\n";
        return 0;
    }

    // Collect test source files.
    std::vector<Path> test_sources;
    collect_sources(tests_dir, test_sources);
    if (test_sources.empty()) {
        std::cout << "warning: no test source files found.\n";
        return 0;
    }

    // Find the active toolchain.
    auto rivet_home_r = rivet::env::rivet_home();
    if (!rivet_home_r) return 1;
    auto tc_r = rivet::toolchain::find_active(*rivet_home_r);
    if (!tc_r) {
        std::cerr << "error: no toolchain: " << tc_r.error().message << "\n";
        return 1;
    }

    rivet::build::BuildConfig cfg;
    cfg.cxx_std = manifest.build.cxx_std;
    cfg.opt     = rivet::build::OptLevel::Debug;
    cfg.debug   = true;

    rivet::build::BuildGraph graph;
    std::vector<rivet::build::TaskId> obj_ids;
    std::vector<Path> obj_paths;
    auto obj_dir = manifest.root_dir / ".rivet" / "build" / profile / "test_obj";

    for (const auto& src : test_sources) {
        auto rel      = src.lexically_relative(manifest.root_dir);
        auto out_path = obj_dir / (rel.string() + ".o");
        auto cj       = rivet::toolchain::compile_job_from(src, out_path, cfg, *tc_r);
        if (auto cmd_r = rivet::toolchain::make_compile_command(cj, *tc_r)) {
            rivet::build::TaskNode node;
            node.name    = src.filename().string();
            node.kind    = rivet::build::TaskKind::Compile;
            node.inputs  = {{ src, "" }};
            node.outputs = {{ out_path, true }};
            node.command = std::move(*cmd_r);
            auto id = graph.add(std::move(node));
            obj_ids.push_back(id);
            obj_paths.push_back(out_path);
        }
    }

    auto test_bin = manifest.root_dir / ".rivet" / "build" / profile / "bin"
                  / (manifest.name + "-test");
    rivet::toolchain::LinkJob lj;
    lj.inputs  = obj_paths;
    lj.output  = test_bin;

    if (auto link_r = rivet::toolchain::make_link_command(lj, *tc_r)) {
        rivet::build::TaskNode link_node;
        link_node.name    = manifest.name + "-test [link]";
        link_node.kind    = rivet::build::TaskKind::Link;
        link_node.deps    = obj_ids;
        for (const auto& o : obj_paths) link_node.inputs.push_back({ o, "" });
        link_node.outputs = {{ test_bin, true }};
        link_node.command = std::move(*link_r);
        graph.add(std::move(link_node));
    }

    rivet::build::Executor executor{graph, std::thread::hardware_concurrency()};
    auto summary = executor.run();
    if (summary.failed) {
        std::cerr << "error: test build failed.\n";
        return 1;
    }

    // Run the test binary.
    std::cout << std::format("Running tests: {}\n", test_bin.string());
    rivet::process::SpawnOptions run_opts;
    run_opts.args        = {test_bin.string()};
    run_opts.inherit_env = true;

    auto child_r = rivet::process::spawn(std::move(run_opts));
    if (!child_r) {
        std::cerr << "error: could not run tests: " << child_r.error().message << "\n";
        return 1;
    }
    auto wait_r = child_r->wait();
    int rc      = wait_r.value_or(1);
    if (rc == 0)
        std::cout << "  All tests passed.\n";
    else
        std::cerr << std::format("  Tests failed (exit {}).\n", rc);
    return rc;
}

// ─── cmd_run ─────────────────────────────────────────────────────────────────

// Run a user-defined [scripts] entry from rivet.toml.
//   sh -c "<command> $@" -- <extra args after `--`>     (POSIX)
//   cmd /c "<command>"                                   (Windows)
static int run_script(const rivet::pkg::Manifest& manifest,
                       std::string_view name,
                       std::string_view command,
                       const std::vector<std::string_view>& args) {
    rivet::process::SpawnOptions opts;
    opts.working_dir = manifest.root_dir;
    opts.inherit_env = true;

    // Collect args after `--` (npm/bun convention).
    std::vector<std::string> passthrough;
    bool past_sep = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--") { past_sep = true; continue; }
        if (past_sep) passthrough.emplace_back(args[i]);
    }

#if defined(_WIN32)
    opts.args.push_back("cmd");
    opts.args.push_back("/c");
    std::string combined{command};
    for (const auto& a : passthrough) { combined += " "; combined += a; }
    opts.args.push_back(combined);
#else
    opts.args.push_back("sh");
    opts.args.push_back("-c");
    // Use $@ + a leading sentinel so passthrough args become positional.
    std::string full{command};
    full += " \"$@\"";
    opts.args.push_back(full);
    opts.args.push_back("rivet-run");  // $0
    for (auto& a : passthrough) opts.args.push_back(std::move(a));
#endif

    std::cout << "$ " << name << ": " << command << "\n";

    auto child = rivet::process::spawn(std::move(opts));
    if (!child) {
        std::cerr << "error: " << child.error().message << "\n";
        return 1;
    }
    auto code = child->wait();
    return code.value_or(1);
}

int cmd_run(const Context& ctx) {
    auto args = ctx.args_after_subcommand();

    // Load manifest first so we can dispatch to a script if applicable.
    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n";
        return 1;
    }
    const auto& manifest = *manifest_r;

    // `rivet run` with no name: list available scripts (npm-style).
    if (args.empty() && !manifest.scripts.empty()) {
        std::cout << "Available scripts:\n";
        for (const auto& [n, c] : manifest.scripts)
            std::cout << "  " << n << "  " << c << "\n";
        std::cout << "\nRun a script:  rivet run <name>\n";
        std::cout << "Run the binary: rivet run --bin\n";
        return 0;
    }

    // If the first non-flag arg matches a script name, run it.
    if (!args.empty()) {
        std::string first{args[0]};
        auto it = manifest.scripts.find(first);
        if (it != manifest.scripts.end())
            return run_script(manifest, it->first, it->second, args);
    }

    // Build first.
    int build_rc = cmd_build(ctx);
    if (build_rc != 0) return build_rc;

    auto profile = std::string{flag_value(args, "--profile").value_or("debug")};
    auto binary  = manifest.root_dir / ".rivet" / "build" / profile / "bin" / manifest.name;

    if (!rivet::fs::exists(binary).value_or(false)) {
        std::cerr << "error: binary not found: " << binary.string() << "\n";
        return 1;
    }

    // Collect any arguments after "--".
    std::vector<std::string> run_args;
    run_args.push_back(binary.string());
    bool past_sep = false;
    for (const auto& a : args) {
        if (a == "--") { past_sep = true; continue; }
        if (past_sep) run_args.emplace_back(a);
    }

    rivet::process::SpawnOptions run_opts;
    run_opts.args        = std::move(run_args);
    run_opts.inherit_env = true;

    auto child_r = rivet::process::spawn(std::move(run_opts));
    if (!child_r) {
        std::cerr << "error: " << child_r.error().message << "\n";
        return 1;
    }
    auto wait_r = child_r->wait();
    return wait_r.value_or(1);
}

// ─── cmd_exec ────────────────────────────────────────────────────────────────
//
// `rivet exec <name> [-- args...]` — run a binary that came in via the
// dependency graph (cargo's npm-exec / `bundle exec` equivalent).
//
// Discovery searches the per-project vcpkg install tree:
//   <rivet_home>/cache/deps/<root>/vcpkg-installed/<triplet>/
//     tools/<port>/<name>[.exe]      ← canonical location for vcpkg tools
//     bin/<name>[.exe]               ← occasional alt layout
//
// Each tools/<port>/ subdir gets prepended to the spawned child's PATH so
// the binary can find sibling DLLs on Windows (vcpkg co-locates DLL
// dependencies next to .exe).

namespace exec_detail {

// Append a directory to a PATH-style env var.
std::string append_to_path(std::string current, std::string_view dir) {
    if (dir.empty()) return current;
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    if (!current.empty()) current.push_back(sep);
    current.append(dir);
    return current;
}

struct ResolvedBinary {
    Path binary;
    std::vector<Path> sibling_dirs;  // dirs to prepend to PATH for DLL resolution
};

std::optional<ResolvedBinary>
find_binary(const Path& install_root, std::string_view name) {
#if defined(_WIN32)
    std::string exe = std::string(name) + ".exe";
#else
    std::string exe = std::string(name);
#endif

    // 1) `tools/<port>/<name>` — preferred; vcpkg's canonical layout.
    Path tools = install_root / "tools";
    if (rivet::fs::exists(tools).value_or(false)) {
        if (auto ports = rivet::fs::list_dir(tools)) {
            for (const auto& port_dir : *ports) {
                auto stat_r = rivet::fs::stat(port_dir);
                if (!stat_r || !stat_r->is_dir) continue;
                Path candidate = port_dir / exe;
                if (rivet::fs::exists(candidate).value_or(false)) {
                    ResolvedBinary out;
                    out.binary = candidate;
                    out.sibling_dirs.push_back(port_dir);
                    return out;
                }
            }
        }
    }

    // 2) `bin/<name>` — fallback for ports that drop binaries into the
    //    runtime bin/ (e.g. anything that ships a shared library bundle).
    Path bin_candidate = install_root / "bin" / exe;
    if (rivet::fs::exists(bin_candidate).value_or(false)) {
        ResolvedBinary out;
        out.binary = bin_candidate;
        out.sibling_dirs.push_back(install_root / "bin");
        return out;
    }

    return std::nullopt;
}

} // namespace exec_detail

int cmd_exec(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    if (args.empty()) {
        std::cerr << "usage: rivet exec <name> [-- <args>]\n"
                  << "       runs a binary shipped by one of this project's dependencies\n";
        return 1;
    }

    std::string name{args[0]};

    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n"
                  << "hint: rivet exec runs binaries fetched via dependencies; "
                     "needs a rivet.toml in this dir or a parent.\n";
        return 1;
    }
    const auto& manifest = *manifest_r;

    auto home_r = rivet::env::rivet_home();
    if (!home_r) {
        std::cerr << "error: cannot determine rivet home: "
                  << home_r.error().message << "\n";
        return 1;
    }
    Path install_root = *home_r / "cache" / "deps" / manifest.name
                        / "vcpkg-installed" / rivet::pkg::host_vcpkg_triplet();

    auto resolved = exec_detail::find_binary(install_root, name);
    if (!resolved) {
        std::cerr << "error: no binary named '" << name << "' found in this project's deps.\n"
                  << "       searched: " << install_root.string() << "/tools/*/" << name
#if defined(_WIN32)
                  << ".exe"
#endif
                  << "\n"
                  << "hint: run 'rivet fetch' first, or check that the dependency that\n"
                  << "      provides this tool is declared in rivet.toml.\n";
        return 1;
    }

    // Collect args after `--` (cargo convention).
    std::vector<std::string> child_args;
    child_args.push_back(resolved->binary.string());
    bool past_sep = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--") { past_sep = true; continue; }
        if (past_sep) child_args.emplace_back(args[i]);
        else          child_args.emplace_back(args[i]);  // forward bare args too
    }

    rivet::process::SpawnOptions opts;
    opts.args        = std::move(child_args);
    opts.inherit_env = true;

    // Prepend sibling_dirs to PATH so co-located DLLs / .dylibs resolve.
    auto current_path = rivet::env::get("PATH").value_or("");
    std::string augmented = current_path;
    for (const auto& d : resolved->sibling_dirs)
        augmented = exec_detail::append_to_path(d.string(), augmented);
    opts.env["PATH"] = augmented;

    auto child = rivet::process::spawn(std::move(opts));
    if (!child) {
        std::cerr << "error: " << child.error().message << "\n";
        return 1;
    }
    auto code = child->wait();
    return code.value_or(1);
}

// ─── cmd_add ─────────────────────────────────────────────────────────────────

// Extract "value" from {"key":"value",...} JSON — minimal, for well-formed registry JSON.
static std::string json_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) { ++pos; }
        val += json[pos++];
    }
    return val;
}

int cmd_add(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    if (args.empty()) {
        std::cerr << "usage: rivet add <package>[@version]\n"
                  << "       rivet add fmt@10.2.0\n";
        return 1;
    }

    // Parse <name>[@version].
    std::string pkg_spec{args[0]};
    std::string pkg_name, pkg_version;
    auto at = pkg_spec.find('@');
    if (at != std::string::npos) {
        pkg_name    = pkg_spec.substr(0, at);
        pkg_version = pkg_spec.substr(at + 1);
    } else {
        pkg_name = pkg_spec;
    }

    for (char c : pkg_name) {
        if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') {
            std::cerr << "error: invalid package name '" << pkg_name
                      << "': only a-z, 0-9, _, - allowed\n";
            return 1;
        }
    }

    // Find manifest.
    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n";
        return 1;
    }
    auto manifest = *manifest_r;

    // Resolve through the SourceRegistry (vcpkg by default, plus local/git
    // overrides). Falls back to "*" if every source fails — the user can
    // pin manually.
    auto home_r = rivet::env::rivet_home();
    Path rivet_home_path = home_r ? *home_r : Path{".rivet"};
    auto registry = rivet::pkg::make_default_registry(rivet_home_path);

    rivet::pkg::PackageRef ref;
    ref.name               = pkg_name;
    ref.version_constraint = pkg_version.empty() ? "*" : pkg_version;

    std::cout << std::format("Resolving {} via vcpkg...\n", pkg_name);
    if (auto resolved = registry.resolve(ref)) {
        pkg_version = resolved->version;
        std::cout << std::format("  Found {} {} ({})\n",
            resolved->name, resolved->version, resolved->source_id);
    } else if (pkg_version.empty()) {
        std::cout << std::format("note: could not resolve {}: {}\n",
            pkg_name, resolved.error().message);
        std::cout << "      pinning to '*' (any version) — edit rivet.toml to refine.\n";
        pkg_version = "*";
    }

    bool updating = manifest.dependencies.count(pkg_name) > 0;
    std::cout << std::format("{} {} = \"{}\"\n",
        updating ? "Updating" : "Adding", pkg_name, pkg_version);

    rivet::pkg::DepSpec spec;
    spec.kind    = rivet::pkg::DepKind::Registry;
    spec.version = pkg_version;
    manifest.dependencies[pkg_name] = std::move(spec);

    // Re-serialise rivet.toml.
    auto toml_text  = rivet::pkg::serialize(manifest);
    auto toml_bytes = rivet::ByteSpan{
        reinterpret_cast<const std::byte*>(toml_text.data()), toml_text.size()};
    if (auto r = rivet::fs::write_atomic(manifest.root_dir / "rivet.toml", toml_bytes); !r) {
        std::cerr << "error: could not update rivet.toml: " << r.error().message << "\n";
        return 1;
    }

    // Regenerate lock file via the new multi-source resolver.
    rivet::pkg::Resolver r{registry};
    if (auto lock_r = r.resolve(manifest)) {
        (void)rivet::pkg::write_lockfile(*lock_r, manifest.root_dir / "rivet.lock");
    } else if (auto stub = rivet::pkg::resolve(manifest)) {
        // Fall back to flat lockfile if vcpkg is unreachable so `add` still
        // updates rivet.lock entries the user can inspect.
        (void)rivet::pkg::write_lockfile(*stub, manifest.root_dir / "rivet.lock");
    }

    std::cout << std::format("  {} \"{}\" written to rivet.toml\n", pkg_name, pkg_version);
    return 0;
}

// ─── cmd_fetch ──────────────────────────────────────────────────────────────
//
// Materialize every locked dependency on disk via its source backend.
// For vcpkg deps this triggers `vcpkg install` with the bundled-clang triplet;
// for git/local deps it's just a clone/copy. Idempotent.
//
// Flags:
//   --locked   use rivet.lock as authoritative; error if a dep is missing
//              from it (cargo-style "lockfile must satisfy manifest").
//   --frozen   --locked + no network access (no vcpkg clone, no git fetch).
//              Intended for CI: forbid any drift, fail loud.

int cmd_fetch(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    bool locked = has_flag(args, "--locked") || has_flag(args, "--frozen");
    bool frozen = has_flag(args, "--frozen");

    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n";
        return 1;
    }
    const auto& manifest = *manifest_r;

    if (manifest.dependencies.empty()) {
        std::cout << "no dependencies declared in rivet.toml — nothing to fetch.\n";
        return 0;
    }

    // Platform SDK check — vcpkg builds need the Apple frameworks /
    // Windows Kits headers just like `rivet build` does.
    if (const auto& sdk = rivet::toolchain::detect_host_sdk(); !sdk.present) {
        std::cerr << "error: platform SDK not detected (required to build dependencies).\n\n"
                  << sdk.hint << "\n";
        return 1;
    }

    auto home_r = rivet::env::rivet_home();
    Path rivet_home_path = home_r ? *home_r : Path{".rivet"};
    auto registry = rivet::pkg::make_default_registry(rivet_home_path);
    rivet::pkg::ResolverOptions opts;
    opts.locked = locked;
    opts.frozen = frozen;
    rivet::pkg::Resolver resolver{registry, opts};

    rivet::Result<rivet::pkg::LockFile> lock;
    Path lockfile_path = manifest.root_dir / "rivet.lock";
    if (locked) {
        auto existing = rivet::pkg::parse_lockfile(lockfile_path);
        if (!existing) {
            std::cerr << "error: --locked/--frozen requires an existing rivet.lock: "
                      << existing.error().message << "\n";
            return 1;
        }
        lock = resolver.resolve_locked(manifest, *existing);
    } else {
        lock = resolver.resolve(manifest);
    }
    if (!lock) {
        std::cerr << "error: " << lock.error().message << "\n";
        return 1;
    }

    // Cache for fetched artifacts: <rivet_home>/cache/deps/<root_pkg>/
    Path fetch_cache = rivet_home_path / "cache" / "deps" / manifest.name;
    if (auto r = rivet::fs::create_dirs(fetch_cache); !r) {
        std::cerr << "error: cannot create cache dir " << fetch_cache.string()
                  << ": " << r.error().message << "\n";
        return 1;
    }

    int failures = 0;
    for (const auto& dep : lock->packages) {
        std::cout << std::format("Fetching {} {} ({})\n",
            dep.name, dep.version, dep.source);
        auto* src = registry.find(dep.source);
        if (!src) {
            std::cerr << "  error: source '" << dep.source << "' not configured\n";
            ++failures;
            continue;
        }
        // Reconstruct a minimal ResolvedPackage for fetch().
        rivet::pkg::ResolvedPackage rp;
        rp.name           = dep.name;
        rp.version        = dep.version;
        rp.source_id      = dep.source;
        rp.archive_sha256 = dep.checksum.empty()
            ? std::optional<std::string>{} : dep.checksum;
        if (dep.source == "git") {
            rp.source_locator = dep.git_url + "#" + dep.git_commit;
        } else if (dep.source == "path") {
            rp.source_locator = dep.local_path.string();
        } else {
            rp.source_locator = "vcpkg://" + dep.name;
        }

        auto recipe = src->fetch(rp, fetch_cache);
        if (!recipe) {
            std::cerr << "  error: " << recipe.error().message << "\n";
            ++failures;
            continue;
        }
        if (recipe->driver == rivet::pkg::BuildDriver::Prebuilt
                && !recipe->install_root.empty()) {
            std::cout << "  installed → " << recipe->install_root.string() << "\n";
        } else if (!recipe->source_dir.empty()) {
            std::cout << "  source    → " << recipe->source_dir.string() << "\n";
        }
    }

    if (failures > 0) {
        std::cerr << std::format("\n{} dependency fetch(es) failed.\n", failures);
        return 1;
    }
    std::cout << std::format("\nFetched {} dependency(ies).\n", lock->packages.size());
    return 0;
}

// ─── cmd_remove ──────────────────────────────────────────────────────────────

int cmd_remove(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    if (args.empty()) {
        std::cerr << "usage: rivet remove <package>\n";
        return 1;
    }
    std::string pkg_name{args[0]};

    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n";
        return 1;
    }
    auto manifest = *manifest_r;

    if (!manifest.dependencies.count(pkg_name)) {
        std::cerr << "error: package '" << pkg_name << "' is not a dependency.\n";
        return 1;
    }

    manifest.dependencies.erase(pkg_name);

    auto toml_text  = rivet::pkg::serialize(manifest);
    auto toml_bytes = rivet::ByteSpan{
        reinterpret_cast<const std::byte*>(toml_text.data()), toml_text.size()};
    if (auto r = rivet::fs::write_atomic(manifest.root_dir / "rivet.toml", toml_bytes); !r) {
        std::cerr << "error: could not update rivet.toml: " << r.error().message << "\n";
        return 1;
    }

    if (auto lock_r = rivet::pkg::resolve(manifest))
        (void)rivet::pkg::write_lockfile(*lock_r, manifest.root_dir / "rivet.lock");

    std::cout << std::format("  Removed {} from rivet.toml\n", pkg_name);
    return 0;
}

// ─── cmd_new ─────────────────────────────────────────────────────────────────

int cmd_new(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    if (args.empty()) {
        std::cerr << "usage: rivet new <project-name> [--template=<name>]\n";
        return 1;
    }

    std::string name{args[0]};
    auto tmpl = flag_value(args, "--template").value_or("cli-cpp");

    // Validate name.
    for (char c : name) {
        if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') {
            std::cerr << "error: invalid project name '" << name
                      << "': only a-z, 0-9, _, - allowed\n";
            return 1;
        }
    }

    Path project_dir = Path{"."} / name;
    if (rivet::fs::exists(project_dir).value_or(false)) {
        std::cerr << "error: directory '" << name << "' already exists.\n";
        return 1;
    }

    // Create directory structure.
    auto src_dir = project_dir / "src";
    if (auto r = rivet::fs::create_dirs(src_dir); !r) {
        std::cerr << "error: failed to create project directory: " << r.error().message << "\n";
        return 1;
    }

    // rivet.toml
    rivet::pkg::Manifest manifest;
    manifest.name        = name;
    manifest.version     = "0.1.0";
    manifest.description = "A new Rivet project";

    auto str_bytes = [](const std::string& s) {
        return std::vector<std::byte>(reinterpret_cast<const std::byte*>(s.data()),
                                      reinterpret_cast<const std::byte*>(s.data()) + s.size());
    };

    std::string toml = rivet::pkg::serialize(manifest);
    (void)rivet::fs::write_atomic(project_dir / "rivet.toml", str_bytes(toml));

    // src/main.cpp
    std::string main_cpp = std::format(
        "// {}/src/main.cpp\n"
        "#include <print>\n\n"
        "int main() {{\n"
        "    std::println(\"Hello from {}!\");\n"
        "}}\n", name, name);
    (void)rivet::fs::write_atomic(src_dir / "main.cpp", str_bytes(main_cpp));

    // .gitignore
    std::string gitignore = ".rivet/\nbuild/\n*.o\n*.a\n";
    (void)rivet::fs::write_atomic(project_dir / ".gitignore", str_bytes(gitignore));

    std::cout << std::format("  Created {} project '{}'\n", tmpl, name);
    std::cout << std::format("\n  To get started:\n    cd {}\n    rivet build\n", name);
    return 0;
}

// ─── cmd_publish ─────────────────────────────────────────────────────────────

int cmd_publish(const Context& ctx) {
    auto args = ctx.args_after_subcommand();

    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n";
        return 1;
    }
    const auto& manifest = *manifest_r;

    if (auto vr = rivet::pkg::validate(manifest); !vr) {
        std::cerr << "error: " << vr.error().message << "\n";
        return 1;
    }

    // Check for API token.
    auto token_opt = rivet::env::get("RIVET_REGISTRY_TOKEN");
    if (!token_opt) {
        std::cerr << "error: RIVET_REGISTRY_TOKEN not set.\n"
                  << "hint:  get a token at https://registry.cx.dev/tokens\n";
        return 1;
    }

    // Build a source archive: tar.zst of the project (excluding .rivet/, build/).
    auto archive_name = std::format("{}-{}.tar.zst", manifest.name, manifest.version);
    auto archive_path = rivet::fs::temp_path_near(cwd / ".rivet" / archive_name);

    // Ensure .rivet dir exists for temp file placement.
    (void)rivet::fs::create_dirs(cwd / ".rivet");

    std::cout << std::format("Packing {} v{}...\n", manifest.name, manifest.version);

    rivet::process::SpawnOptions tar_opts;
    tar_opts.args = {"tar", "--zstd", "-cf", archive_path.string(),
                     "--exclude=.rivet", "--exclude=build", "--exclude=.git",
                     "-C", manifest.root_dir.string(), "."};
    tar_opts.inherit_env    = true;
    tar_opts.capture_stderr = true;

    auto child_r = rivet::process::spawn(std::move(tar_opts));
    if (!child_r) {
        std::cerr << "error: tar failed: " << child_r.error().message << "\n";
        return 1;
    }
    auto wait_r = child_r->wait();
    if (!wait_r || *wait_r != 0) {
        std::cerr << "error: tar exited " << wait_r.value_or(-1) << "\n"
                  << child_r->stderr_output() << "\n";
        (void)rivet::fs::remove_file(archive_path);
        return 1;
    }

    // Read archive bytes.
    auto data = rivet::fs::read_file(archive_path);
    (void)rivet::fs::remove_file(archive_path);
    if (!data) {
        std::cerr << "error: could not read packed archive.\n";
        return 1;
    }

    std::cout << std::format("Uploading {} ({} KB)...\n",
        archive_name, data->size() / 1024);

    std::string registry_base =
        rivet::env::get("RIVET_REGISTRY_URL")
            .value_or("https://registry.cx.dev");

    (void)rivet::net::init_tls_trust_store();
    rivet::net::HttpClient client{registry_base,
        rivet::net::RequestOptions{
            .headers = {{"Authorization", "Bearer " + *token_opt}}}};

    auto resp = client.put(
        "/api/v1/publish",
        rivet::ByteSpan{data->data(), data->size()},
        "application/x-tar+zstd");

    if (!resp || !resp->ok()) {
        int code = resp ? resp->status_code : 0;
        std::cerr << std::format("error: publish failed (HTTP {})\n", code);
        if (resp) std::cerr << resp->body_str() << "\n";
        return 1;
    }

    std::cout << std::format("  Published {} v{} to {}\n",
        manifest.name, manifest.version, registry_base);
    return 0;
}

// ─── cmd_cache ───────────────────────────────────────────────────────────────

int cmd_cache(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    auto sub  = args.empty() ? "status" : std::string{args[0]};

    auto cache_dir_r = rivet::env::cache_dir();
    if (!cache_dir_r) {
        std::cerr << "error: " << cache_dir_r.error().message << "\n";
        return 1;
    }

    auto store_r = rivet::cache::Store::open(*cache_dir_r / "rivet" / "build");
    if (!store_r) {
        std::cerr << "error: cannot open cache: " << store_r.error().message << "\n";
        return 1;
    }
    auto& store = *store_r;

    if (sub == "status" || sub == "info") {
        auto stats = store.stats();
        if (!stats) {
            std::cerr << "error: " << stats.error().message << "\n";
            return 1;
        }
        std::cout << std::format("Cache entries : {}\n", stats->entry_count);
        std::cout << std::format("Total size    : {} bytes\n", stats->total_bytes);
        return 0;
    }

    if (sub == "clean") {
        // `rivet cache clean --older-than 30d` or `--max-size 10G`
        auto older_than = flag_value(args, "--older-than");
        auto max_size   = flag_value(args, "--max-size");

        if (older_than) {
            // Parse "30d", "7d", etc.
            int64_t sec = 0;
            std::string s{*older_than};
            if (!s.empty() && s.back() == 'd')
                sec = std::stoll(s.substr(0, s.size() - 1)) * 86400;
            else
                sec = std::stoll(s);
            auto n = store.evict_older_than(sec);
            if (n) std::cout << std::format("Evicted {} entries older than {}.\n", *n, *older_than);
        }

        if (!older_than && !max_size) {
            // Evict everything.
            auto n = store.trim_to(0);
            if (n) std::cout << std::format("Cleared {} cache entries.\n", *n);
        }
        return 0;
    }

    std::cerr << "usage: rivet cache [status|clean] [--older-than <age>] [--max-size <size>]\n";
    return 1;
}

// ─── cmd_daemon ──────────────────────────────────────────────────────────────

int cmd_daemon(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    auto sub  = args.empty() ? "status" : std::string{args[0]};

    auto rivet_home_r = rivet::env::rivet_home();
    if (!rivet_home_r) {
        std::cerr << "error: " << rivet_home_r.error().message << "\n";
        return 1;
    }

    if (sub == "start") {
        if (has_flag(args, "--background")) {
            // We ARE the background daemon process.
            return rivet::daemon::daemon_main(*rivet_home_r);
        }
        // Spawn a background daemon.
        rivet::daemon::DaemonClient client{*rivet_home_r};
        auto r = client.connect();
        if (!r)
            std::cerr << "note: " << r.error().message << "\n";
        std::cout << "daemon: " << rivet::daemon::socket_path(*rivet_home_r) << "\n";
        return 0;
    }

    if (sub == "status") {
        rivet::daemon::DaemonClient client{*rivet_home_r};
        auto metrics = client.status();
        if (!metrics) {
            std::cout << "daemon: not running\n";
        } else {
            std::cout << std::format("daemon: up  jobs={} queue={} uptime={}s\n",
                metrics->jobs_completed, metrics->queue_depth, metrics->uptime_sec);
        }
        return 0;
    }

    if (sub == "stop") {
        rivet::daemon::DaemonClient client{*rivet_home_r};
        auto r = client.shutdown();
        if (!r) std::cout << "daemon: not running.\n";
        else    std::cout << "daemon: stopped.\n";
        return 0;
    }

    std::cerr << "usage: rivet daemon [start|stop|status]\n";
    return 1;
}

// ─── cmd_toolchain ───────────────────────────────────────────────────────────

int cmd_toolchain(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    auto sub  = args.empty() ? "list" : std::string{args[0]};

    auto rivet_home_r = rivet::env::rivet_home();
    if (!rivet_home_r) {
        std::cerr << "error: " << rivet_home_r.error().message << "\n";
        return 1;
    }

    if (sub == "list") {
        auto installed = rivet::toolchain::list_installed(*rivet_home_r);
        if (!installed || installed->empty()) {
            std::cout << "No toolchains installed.\n"
                      << "Run 'rivet toolchain install <version>' to download one.\n";
            return 0;
        }
        // Show active version.
        auto active = rivet::toolchain::find_active(*rivet_home_r);
        for (const auto& tc : *installed) {
            bool is_active = active && active->version == tc.version;
            std::cout << std::format("  {} clang-{}  {}\n",
                is_active ? "* " : "  ",
                tc.version,
                tc.root.string());
        }
        return 0;
    }

    if (sub == "install") {
        if (args.size() < 2) {
            std::cerr << "usage: rivet toolchain install <version>\n"
                      << "example: rivet toolchain install 18.1.0\n";
            return 1;
        }
        std::string version{args[1]};

        // Already installed?
        if (auto installed_r = rivet::toolchain::list_installed(*rivet_home_r)) {
            for (const auto& tc : *installed_r) {
                if (tc.version == version) {
                    std::cout << std::format("toolchain clang-{} already installed at {}\n",
                        version, tc.root.string());
                    return 0;
                }
            }
        }

        // Determine download triple.
        std::string triple;
#if defined(__APPLE__)
#  if defined(__aarch64__)
        triple = "arm64-apple-macos";
#  else
        triple = "x86_64-apple-macos";
#  endif
#elif defined(__linux__)
#  if defined(__aarch64__)
        triple = "arm64-linux-gnu";
#  else
        triple = "x86_64-linux-gnu";
#  endif
#elif defined(_WIN32)
        triple = "x86_64-windows-msvc";
#else
        triple = "unknown";
#endif

        std::string archive_name = std::format(
            "rivet-toolchain-clang-{}-{}.tar.zst", version, triple);
        // Hosted by the publish-toolchain.yml workflow (Actions → "Publish
        // LLVM Toolchain Bundle"). Override with RIVET_TOOLCHAIN_BASE_URL
        // to host bundles privately (e.g. for air-gapped enterprises).
        std::string base = rivet::env::get("RIVET_TOOLCHAIN_BASE_URL")
            .value_or("https://github.com/fedres/Rivet/releases/download");
        std::string url_str = std::format(
            "{}/toolchain-{}/{}", base, version, archive_name);

        auto url_r = rivet::net::Url::parse(url_str);
        if (!url_r) {
            std::cerr << "error: malformed URL: " << url_r.error().message << "\n";
            return 1;
        }

        // Ensure toolchains dir exists.
        auto tc_dir      = *rivet_home_r / "toolchains" / version;
        auto toolchains  = *rivet_home_r / "toolchains";
        if (auto r = rivet::fs::create_dirs(toolchains); !r) {
            std::cerr << "error: " << r.error().message << "\n";
            return 1;
        }

        auto tmp_archive = rivet::fs::temp_path_near(toolchains / ".tmp");

        std::cout << std::format("Downloading clang-{} for {}...\n", version, triple);

        auto progress = [](uint64_t done, uint64_t total) {
            if (total > 0) {
                int pct = static_cast<int>(done * 100 / total);
                std::cout << std::format("\r  {}%  ({} MB / {} MB)",
                    pct, done >> 20, total >> 20) << std::flush;
            }
        };

        (void)rivet::net::init_tls_trust_store();
        auto dl = rivet::net::download_file(*url_r, tmp_archive, {}, progress);
        std::cout << "\n";
        if (!dl) {
            (void)rivet::fs::remove_file(tmp_archive);
            std::cerr << "error: download failed: " << dl.error().message << "\n";
            return 1;
        }

        // Extract archive (in-process via vendored libzstd — no host
        // `zstd` / `tar` prereq).
        if (auto r = rivet::fs::create_dirs(tc_dir); !r) {
            std::cerr << "error: " << r.error().message << "\n";
            (void)rivet::fs::remove_file(tmp_archive);
            return 1;
        }

        rivet::archive::ExtractOptions xopts;
        xopts.strip_components = 1;
        auto ex = rivet::archive::extract_tar_zst(tmp_archive, tc_dir, xopts);
        (void)rivet::fs::remove_file(tmp_archive);
        if (!ex) {
            std::cerr << "error: extraction failed: " << ex.error().message << "\n";
            return 1;
        }

        // Activate the newly installed toolchain.
        (void)rivet::toolchain::set_active(*rivet_home_r, version);
        std::cout << std::format("Installed clang-{} → {}\n", version, tc_dir.string());
        std::cout << std::format("Run 'rivet toolchain use {}' to switch to it.\n", version);
        return 0;
    }

    if (sub == "use") {
        if (args.size() < 2) {
            std::cerr << "usage: rivet toolchain use <version>\n";
            return 1;
        }
        auto r = rivet::toolchain::set_active(*rivet_home_r, args[1]);
        if (!r) { std::cerr << "error: " << r.error().message << "\n"; return 1; }
        std::cout << std::format("Active toolchain set to clang-{}\n", args[1]);
        return 0;
    }

    if (sub == "active") {
        auto tc = rivet::toolchain::find_active(*rivet_home_r);
        if (!tc) {
            std::cerr << "error: " << tc.error().message << "\n";
            return 1;
        }
        std::cout << std::format("clang-{}  {}\n", tc->version, tc->root.string());
        return 0;
    }

    std::cerr << "usage: rivet toolchain [list|install|use|active]\n";
    return 1;
}

// ─── cmd_fuzz ─────────────────────────────────────────────────────────────────
//
// Phase 4: libFuzzer integration. Compiles the target with -fsanitize=fuzzer
// and launches the fuzz loop. RIVET_FUZZ_TARGET names the entry-point function.

int cmd_fuzz(const Context& ctx) {
    auto args = ctx.args_after_subcommand();

    // Use the actual OS-level cwd, not $PWD: tools that invoke rivet via
    // fork+exec (Python subprocess, build scripts, etc.) update the process
    // cwd via chdir() but leave $PWD pointing at the parent's cwd. Reading
    // $PWD here makes rivet walk up from the wrong directory and find an
    // unrelated rivet.toml on the way to the filesystem root.
    Path cwd = std::filesystem::current_path();
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n";
        return 1;
    }
    const auto& manifest = *manifest_r;

    // Default target: look for RIVET_FUZZ_TARGET env var or first positional arg.
    std::string target_fn;
    if (!args.empty()) target_fn = std::string{args[0]};
    if (target_fn.empty()) {
        if (auto t = rivet::env::get("RIVET_FUZZ_TARGET")) target_fn = *t;
    }
    if (target_fn.empty()) {
        std::cerr << "usage: rivet fuzz <target_function>\n"
                  << "       or set RIVET_FUZZ_TARGET in the environment\n";
        return 1;
    }

    auto rivet_home_r = rivet::env::rivet_home();
    if (!rivet_home_r) return 1;
    auto tc_r = rivet::toolchain::find_active(*rivet_home_r);
    if (!tc_r) {
        std::cerr << "error: no toolchain: " << tc_r.error().message << "\n";
        return 1;
    }

    // Build with fuzzer sanitizer + address sanitizer.
    rivet::build::BuildConfig cfg;
    cfg.cxx_std    = manifest.build.cxx_std;
    cfg.opt        = rivet::build::OptLevel::Debug;
    cfg.debug      = true;
    cfg.sanitizers = {"fuzzer", "address"};
    cfg.extra_flags = {"-DRIVET_FUZZ_TARGET=" + target_fn};

    auto src_dir = manifest.root_dir / "fuzz";
    if (!rivet::fs::exists(src_dir).value_or(false))
        src_dir = manifest.root_dir / "src";

    std::vector<Path> sources;
    collect_sources(src_dir, sources);
    if (sources.empty()) {
        std::cerr << "error: no source files found for fuzzing in " << src_dir.string() << "\n";
        return 1;
    }

    rivet::build::BuildGraph graph;
    std::vector<rivet::build::TaskId> obj_ids;
    std::vector<Path> obj_paths;
    auto obj_dir = manifest.root_dir / ".rivet" / "build" / "fuzz" / "obj";

    for (const auto& src : sources) {
        auto rel      = src.lexically_relative(manifest.root_dir);
        auto out_path = obj_dir / (rel.string() + ".o");
        auto cj       = rivet::toolchain::compile_job_from(src, out_path, cfg, *tc_r);
        auto cmd_r    = rivet::toolchain::make_compile_command(cj, *tc_r);
        if (!cmd_r) continue;

        rivet::build::TaskNode node;
        node.name    = src.filename().string();
        node.kind    = rivet::build::TaskKind::Compile;
        node.outputs = {{ out_path, true }};
        node.command = std::move(*cmd_r);
        std::string src_hash;
        if (auto hr = rivet::cache::sha256_file(src)) src_hash = std::move(*hr);
        node.inputs  = {{ src, std::move(src_hash) }};
        auto id = graph.add(std::move(node));
        obj_ids.push_back(id);
        obj_paths.push_back(out_path);
    }

    auto fuzz_bin = manifest.root_dir / ".rivet" / "build" / "fuzz" / "bin"
                  / (manifest.name + "-fuzz-" + target_fn);

    rivet::toolchain::LinkJob lj;
    lj.inputs     = obj_paths;
    lj.output     = fuzz_bin;
    lj.sanitizers = {"fuzzer", "address"};

    if (auto link_r = rivet::toolchain::make_link_command(lj, *tc_r)) {
        rivet::build::TaskNode link_node;
        link_node.name    = manifest.name + "-fuzz [link]";
        link_node.kind    = rivet::build::TaskKind::Link;
        link_node.deps    = obj_ids;
        for (const auto& o : obj_paths) link_node.inputs.push_back({ o, "" });
        link_node.outputs = {{ fuzz_bin, true }};
        link_node.command = std::move(*link_r);
        graph.add(std::move(link_node));
    }

    rivet::build::Executor executor{graph, std::thread::hardware_concurrency()};
    auto summary = executor.run();
    if (summary.failed) {
        std::cerr << "error: fuzz build failed.\n";
        return 1;
    }

    std::cout << std::format("Running fuzzer: {}\n", fuzz_bin.string());

    // Collect any corpus directories / libFuzzer flags after "--".
    std::vector<std::string> fuzz_args;
    fuzz_args.push_back(fuzz_bin.string());
    bool past_sep = false;
    for (const auto& a : args) {
        if (a == "--") { past_sep = true; continue; }
        if (past_sep) fuzz_args.emplace_back(a);
    }
    // Default: run indefinitely with -runs=-1.
    if (!past_sep) fuzz_args.push_back("-runs=-1");

    rivet::process::SpawnOptions run_opts;
    run_opts.args        = std::move(fuzz_args);
    run_opts.inherit_env = true;

    auto child_r = rivet::process::spawn(std::move(run_opts));
    if (!child_r) {
        std::cerr << "error: " << child_r.error().message << "\n";
        return 1;
    }
    auto wait_r = child_r->wait();
    return wait_r.value_or(1);
}

// ─── cmd_self_update ─────────────────────────────────────────────────────────
//
// Phase 5 self-hosting milestone: rivet can update itself from GitHub releases.
// Downloads the latest bundle for the current platform, verifies checksum,
// extracts the new binary, and atomically replaces the running executable.

int cmd_self_update(const Context& /*ctx*/) {
    // Determine current platform triple (matches ci/bundle.sh naming).
    std::string triple;
#if defined(__APPLE__)
#  if defined(__aarch64__)
    triple = "arm64-apple-macos";
#  else
    triple = "x86_64-apple-macos";
#  endif
#elif defined(__linux__)
#  if defined(__aarch64__)
    triple = "arm64-linux-gnu";
#  else
    triple = "x86_64-linux-gnu";
#  endif
#elif defined(_WIN32)
    triple = "x86_64-windows-msvc";
#else
    std::cerr << "error: self-update not supported on this platform.\n";
    return 1;
#endif

    // Fetch the latest release version from GitHub.
    std::cout << "Checking for latest rivet release...\n";

    (void)rivet::net::init_tls_trust_store();

    auto api_url_r = rivet::net::Url::parse(
        "https://api.github.com/repos/rivet-lang/rivet/releases/latest");
    if (!api_url_r) {
        std::cerr << "error: " << api_url_r.error().message << "\n";
        return 1;
    }

    auto resp = rivet::net::http_get(*api_url_r,
        rivet::net::RequestOptions{
            .headers = {{"Accept", "application/vnd.github+json"},
                        {"User-Agent", std::format("rivet/{}", rivet::kVersion)}}});

    if (!resp || !resp->ok()) {
        std::cerr << "error: could not reach GitHub API.\n";
        return 1;
    }

    // Extract "tag_name" from JSON (format: "v0.2.1").
    auto tag = json_str(resp->body_str(), "tag_name");
    if (tag.empty()) {
        std::cerr << "error: could not parse release tag.\n";
        return 1;
    }

    // Strip leading 'v'.
    std::string latest_version = (tag[0] == 'v') ? tag.substr(1) : tag;

    if (latest_version == rivet::kVersion) {
        std::cout << std::format("rivet {} is already up to date.\n", rivet::kVersion);
        return 0;
    }

    std::cout << std::format("Updating rivet {} → {}...\n",
        rivet::kVersion, latest_version);

    // Determine own executable path.
    auto self_r = rivet::process::self_exe();
    if (!self_r) {
        std::cerr << "error: cannot locate running executable: "
                  << self_r.error().message << "\n";
        return 1;
    }

    // Download the new bundle.
    std::string archive_name = std::format("rivet-{}-{}.tar.zst", latest_version, triple);
    std::string dl_url       = std::format(
        "https://github.com/rivet-lang/rivet/releases/download/v{}/{}",
        latest_version, archive_name);

    auto dl_url_r = rivet::net::Url::parse(dl_url);
    if (!dl_url_r) {
        std::cerr << "error: " << dl_url_r.error().message << "\n";
        return 1;
    }

    auto tmp_archive = rivet::fs::temp_path_near(*self_r);

    auto progress = [](uint64_t done, uint64_t total) {
        if (total > 0)
            std::cout << std::format("\r  {}%  ({} KB / {} KB)",
                done * 100 / total, done >> 10, total >> 10) << std::flush;
    };

    auto dl = rivet::net::download_file(*dl_url_r, tmp_archive, {}, progress);
    std::cout << "\n";
    if (!dl) {
        (void)rivet::fs::remove_file(tmp_archive);
        std::cerr << "error: download failed: " << dl.error().message << "\n";
        return 1;
    }

    // Extract the new binary (via vendored libzstd — no host `tar`/`zstd`
    // prereq). The bundle layout is `rivet-<ver>-<triple>/bin/rivet`; we
    // strip the top dir and pluck out bin/rivet via a staging dir.
    auto tmp_dir = rivet::fs::temp_path_near(*self_r);
    (void)rivet::fs::create_dirs(tmp_dir);

    rivet::archive::ExtractOptions xopts;
    xopts.strip_components = 1;
    auto ex = rivet::archive::extract_tar_zst(tmp_archive, tmp_dir, xopts);
    (void)rivet::fs::remove_file(tmp_archive);
    if (!ex) {
        (void)rivet::fs::remove_all(tmp_dir);
        std::cerr << "error: extraction failed: " << ex.error().message << "\n";
        return 1;
    }

    auto tmp_bin = tmp_dir / "bin" / "rivet";
#if defined(_WIN32)
    tmp_bin += ".exe";
#endif
    if (!rivet::fs::exists(tmp_bin).value_or(false)) {
        (void)rivet::fs::remove_all(tmp_dir);
        std::cerr << "error: extracted bundle missing bin/rivet\n";
        return 1;
    }

    // Atomically replace running binary.
    if (auto rr = rivet::fs::rename_atomic(tmp_bin, *self_r); !rr) {
        (void)rivet::fs::remove_file(tmp_bin);
        std::cerr << "error: could not replace binary: " << rr.error().message << "\n";
        return 1;
    }

    std::cout << std::format("  Updated to rivet {} at {}\n",
        latest_version, self_r->string());
    return 0;
}

} // namespace rivet::cli

