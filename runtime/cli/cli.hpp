// runtime/cli/cli.hpp — Rivet CLI command router
#pragma once

#include "../../platform/interface/result.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace rivet::cli {

struct Context {
    int   argc;
    char** argv;

    std::string_view           program_name() const;
    std::string_view           subcommand()   const;
    std::vector<std::string_view> args_after_subcommand() const;
};

// Top-level CLI entry point. Returns process exit code.
int run(const Context& ctx);

// Individual command handlers — each returns an exit code.
int cmd_build(const Context& ctx);
int cmd_check(const Context& ctx);
int cmd_clean(const Context& ctx);
int cmd_test(const Context& ctx);
int cmd_bench(const Context& ctx);
int cmd_run(const Context& ctx);
int cmd_exec(const Context& ctx);
int cmd_add(const Context& ctx);
int cmd_remove(const Context& ctx);
int cmd_update(const Context& ctx);
int cmd_fetch(const Context& ctx);
int cmd_new(const Context& ctx);
int cmd_publish(const Context& ctx);
int cmd_tree(const Context& ctx);
int cmd_metadata(const Context& ctx);
int cmd_cache(const Context& ctx);
int cmd_daemon(const Context& ctx);
int cmd_toolchain(const Context& ctx);
int cmd_fuzz(const Context& ctx);
int cmd_self_update(const Context& ctx);
int cmd_version(const Context& ctx);
int cmd_help(const Context& ctx);

} // namespace rivet::cli
