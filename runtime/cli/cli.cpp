// runtime/cli/cli.cpp — Rivet CLI command router implementation
#include "cli.hpp"
#include "../../platform/interface/terminal.hpp"
#include "../../platform/interface/env.hpp"
#include "../../platform/interface/fs.hpp"
#include "../../platform/interface/time.hpp"
#include "../build/graph.hpp"
#include "../build/executor.hpp"
#include "../build/scheduler.hpp"
#include "../cache/store.hpp"
#include "../package/manifest.hpp"
#include "../package/lockfile.hpp"
#include "../toolchain/discovery.hpp"
#include "../daemon/daemon.hpp"

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
        "  add         Add a dependency\n"
        "  remove      Remove a dependency\n"
        "  new         Create a new project from a template\n"
        "  publish     Publish a package to the registry\n"
        "  cache       Manage the local build cache\n"
        "  daemon      Manage the compiler daemon\n"
        "  toolchain   Manage bundled toolchain versions\n"
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
    if (sub == "add")      return cmd_add(ctx);
    if (sub == "remove")   return cmd_remove(ctx);
    if (sub == "new")      return cmd_new(ctx);
    if (sub == "publish")  return cmd_publish(ctx);
    if (sub == "cache")    return cmd_cache(ctx);
    if (sub == "daemon")   return cmd_daemon(ctx);
    if (sub == "toolchain") return cmd_toolchain(ctx);

    std::cerr << "error: unknown command '" << sub << "'\n"
              << "Run 'rivet help' for a list of commands.\n";
    return 1;
}

int cmd_help(const Context& /*ctx*/) {
    print_usage();
    return 0;
}

int cmd_version(const Context& /*ctx*/) {
    std::cout << "rivet 0.1.0 (" << rivet::env::host_triple() << ")\n";
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

int cmd_build(const Context& ctx) {
    auto args    = ctx.args_after_subcommand();
    auto cwd_opt = rivet::env::get("PWD");
    Path cwd     = cwd_opt ? Path{*cwd_opt} : Path{"."};

    // 1. Find and parse the manifest.
    auto manifest_r = rivet::pkg::find_and_parse(cwd);
    if (!manifest_r) {
        std::cerr << "error: " << manifest_r.error().message << "\n"
                  << "hint: create a rivet.toml in this directory or a parent.\n";
        return 1;
    }
    const auto& manifest = *manifest_r;

    if (auto vr = rivet::pkg::validate(manifest); !vr) {
        std::cerr << "error: " << vr.error().message << "\n";
        return 1;
    }

    // 2. Discover toolchain.
    auto rivet_home_r = rivet::env::rivet_home();
    if (!rivet_home_r) {
        std::cerr << "error: cannot determine rivet home: " << rivet_home_r.error().message << "\n";
        return 1;
    }
    const Path& rivet_home = *rivet_home_r;

    auto tc_r = rivet::toolchain::find_active(rivet_home);
    if (!tc_r) {
        std::cerr << "error: " << tc_r.error().message << "\n";
        return 1;
    }
    const auto& tc = *tc_r;

    // 3. Determine build profile.
    auto profile_name = flag_value(args, "--profile").value_or("debug");
    std::cout << std::format("Building {} {} [{}] with clang {}\n",
        manifest.name, manifest.version, profile_name, tc.version);

    // 4. Open cache.
    auto cache_dir_r = rivet::env::cache_dir();
    if (cache_dir_r) {
        auto store_r = rivet::cache::Store::open(*cache_dir_r / "rivet" / "build");
        // Cache is non-fatal if unavailable.
        (void)store_r;
    }

    // 5. Build graph + execute.
    // A real implementation scans sources, creates TaskNodes, and runs the executor.
    // Phase 2: emit a clear "no sources yet" message rather than silently succeeding.
    auto src_dir = manifest.root_dir / "src";
    if (!rivet::fs::exists(src_dir).value_or(false)) {
        std::cout << "warning: no src/ directory found — nothing to build.\n";
        return 0;
    }

    auto sources_r = rivet::fs::list_dir(src_dir);
    if (!sources_r || sources_r->empty()) {
        std::cout << "warning: src/ is empty — nothing to build.\n";
        return 0;
    }

    rivet::build::BuildGraph graph;
    std::size_t compile_count = 0;

    for (const auto& entry : *sources_r) {
        auto ext = entry.extension().string();
        if (ext != ".cpp" && ext != ".cxx" && ext != ".cc") continue;

        auto out_path = manifest.root_dir / ".rivet" / "build" / "obj"
                      / (entry.filename().string() + ".o");

        rivet::build::TaskNode node;
        node.name   = entry.filename().string();
        node.kind   = rivet::build::TaskKind::Compile;
        node.inputs = {{ entry, "" }};
        node.outputs= {{ out_path, true }};
        graph.add(std::move(node));
        ++compile_count;
    }

    if (compile_count == 0) {
        std::cout << "warning: no C++ source files in src/ — nothing to build.\n";
        return 0;
    }

    auto t_start  = rivet::time::now();
    std::size_t jobs = std::thread::hardware_concurrency();

    auto on_progress = [](const rivet::build::TaskResult& r) {
        if (r.success)
            std::cout << "  \033[32m✓\033[0m " << r.task_id << "\n";
        else
            std::cerr << "  \033[31m✗\033[0m " << r.task_id
                      << "\n" << r.stderr_out << "\n";
    };

    rivet::build::Executor executor{graph, jobs, on_progress};
    auto summary = executor.run();
    auto elapsed = rivet::time::elapsed(t_start);

    std::cout << std::format("\n{} compiled, {} cached, {} failed  [{:.1f}s]\n",
        summary.succeeded,
        summary.cached,
        summary.failed,
        static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) / 1000.0);

    return summary.failed == 0 ? 0 : 1;
}

// ─── cmd_test ────────────────────────────────────────────────────────────────

int cmd_test(const Context& /*ctx*/) {
    std::cerr << "error: 'test' command coming in Phase 2 milestone.\n"
              << "       Build with a 'test' target in rivet.toml.\n";
    return 1;
}

// ─── cmd_run ─────────────────────────────────────────────────────────────────

int cmd_run(const Context& /*ctx*/) {
    std::cerr << "error: 'run' command coming in Phase 2 milestone.\n";
    return 1;
}

// ─── cmd_add ─────────────────────────────────────────────────────────────────

int cmd_add(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    if (args.empty()) {
        std::cerr << "usage: rivet add <package>[@version]\n";
        return 1;
    }
    std::cerr << "error: package registry not yet available (Phase 3).\n"
              << "hint:  add a [dependencies] entry manually to rivet.toml.\n";
    return 1;
}

// ─── cmd_remove ──────────────────────────────────────────────────────────────

int cmd_remove(const Context& ctx) {
    auto args = ctx.args_after_subcommand();
    if (args.empty()) {
        std::cerr << "usage: rivet remove <package>\n";
        return 1;
    }
    std::cerr << "error: package registry not yet available (Phase 3).\n";
    return 1;
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
    rivet::fs::write_atomic(project_dir / "rivet.toml", str_bytes(toml));

    // src/main.cpp
    std::string main_cpp = std::format(
        "// {}/src/main.cpp\n"
        "#include <print>\n\n"
        "int main() {{\n"
        "    std::println(\"Hello from {}!\");\n"
        "}}\n", name, name);
    rivet::fs::write_atomic(src_dir / "main.cpp", str_bytes(main_cpp));

    // .gitignore
    std::string gitignore = ".rivet/\nbuild/\n*.o\n*.a\n";
    rivet::fs::write_atomic(project_dir / ".gitignore", str_bytes(gitignore));

    std::cout << std::format("  Created {} project '{}'\n", tmpl, name);
    std::cout << std::format("\n  To get started:\n    cd {}\n    rivet build\n", name);
    return 0;
}

// ─── cmd_publish ─────────────────────────────────────────────────────────────

int cmd_publish(const Context& /*ctx*/) {
    std::cerr << "error: publishing not yet available (Phase 3).\n";
    return 1;
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
        // TODO Phase 2: download the toolchain bundle from releases.
        std::cerr << "error: bundled toolchain download not yet implemented.\n"
                  << "hint:  manually place the toolchain at "
                  << (*rivet_home_r / "toolchains" / std::string{args[1]}).string() << "\n";
        return 1;
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

} // namespace rivet::cli

