// runtime/cli/cli.cpp — Rivet CLI command router implementation
#include "cli.hpp"
#include "../../platform/interface/terminal.hpp"
#include "../../platform/interface/env.hpp"

#include <cstring>
#include <iostream>
#include <format>

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

// ─── Stubs (Phase 2+) ────────────────────────────────────────────────────────

static int not_yet_impl(std::string_view cmd) {
    std::cerr << "error: '" << cmd << "' is not yet implemented\n";
    return 1;
}

int cmd_build(const Context& /*ctx*/)    { return not_yet_impl("build"); }
int cmd_test(const Context& /*ctx*/)     { return not_yet_impl("test"); }
int cmd_run(const Context& /*ctx*/)      { return not_yet_impl("run"); }
int cmd_add(const Context& /*ctx*/)      { return not_yet_impl("add"); }
int cmd_remove(const Context& /*ctx*/)   { return not_yet_impl("remove"); }
int cmd_new(const Context& /*ctx*/)      { return not_yet_impl("new"); }
int cmd_publish(const Context& /*ctx*/)  { return not_yet_impl("publish"); }
int cmd_cache(const Context& /*ctx*/)    { return not_yet_impl("cache"); }
int cmd_daemon(const Context& /*ctx*/)   { return not_yet_impl("daemon"); }
int cmd_toolchain(const Context& /*ctx*/) { return not_yet_impl("toolchain"); }

} // namespace rivet::cli
