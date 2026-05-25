// runtime/toolchain/compile.hpp — CompileJob / LinkJob wrappers around clang
//
// These types bridge the Build IR (runtime/build/ir.hpp) and the actual clang
// invocation. The toolchain layer translates high-level job descriptions into
// precise command lines using the active ToolchainInfo.
#pragma once

#include "discovery.hpp"
#include "../build/ir.hpp"
#include "../../platform/interface/result.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rivet::toolchain {

// ─── High-level job descriptions ─────────────────────────────────────────────

struct CompileJob {
    Path                     source;
    Path                     output;         // .o output path
    Path                     dep_file;       // .d file for incremental deps (optional)
    std::string              target_triple;
    std::string              cxx_std  = "c++23";
    build::OptLevel          opt      = build::OptLevel::Debug;
    bool                     debug    = true;
    bool                     lto      = false;
    std::vector<std::string> flags;
    std::vector<Path>        include_paths;
    std::vector<std::string> defines;
    std::vector<std::string> sanitizers;
    bool                     generate_deps = true;  // emit .d file
};

struct LinkJob {
    std::vector<Path>        inputs;         // .o files
    std::vector<Path>        link_libs;      // .a or .so paths
    Path                     output;
    std::string              target_triple;
    bool                     shared   = false;
    bool                     lto      = false;
    std::vector<std::string> flags;
    std::vector<std::string> lib_search_paths;
    std::vector<std::string> sanitizers;
};

struct ArchiveJob {
    std::vector<Path> inputs;    // .o files
    Path              output;    // .a output
    std::string       target_triple;
};

// ─── Job → command translation ───────────────────────────────────────────────

/// Translate a CompileJob into a build::CompileCommand using the given toolchain.
[[nodiscard]] Result<build::CompileCommand>
make_compile_command(const CompileJob& job, const ToolchainInfo& tc);

/// Translate a LinkJob into a build::CompileCommand.
[[nodiscard]] Result<build::CompileCommand>
make_link_command(const LinkJob& job, const ToolchainInfo& tc);

/// Translate an ArchiveJob into a build::CompileCommand.
[[nodiscard]] Result<build::CompileCommand>
make_archive_command(const ArchiveJob& job, const ToolchainInfo& tc);

/// Build a CompileJob directly from the BuildConfig + source path.
[[nodiscard]] CompileJob compile_job_from(const Path& source,
                                          const Path& output,
                                          const build::BuildConfig& cfg,
                                          const ToolchainInfo& tc);

} // namespace rivet::toolchain
