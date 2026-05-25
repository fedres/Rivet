// runtime/modules/scanner.hpp — C++20 module dependency scanner
//
// Before any compilation begins, every .cpp file is scanned for `import`
// statements to build the module dependency sub-graph.
//
// Backed by `clang-scan-deps` from the bundled toolchain (§20).
#pragma once

#include "../build/ir.hpp"
#include "../toolchain/discovery.hpp"
#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace rivet::modules {

// ─── Module dependency graph ──────────────────────────────────────────────────

struct ModuleDep {
    std::string module_name;   // "std.core", "mylib.util", etc.
    bool        is_partition = false;
};

struct SourceModuleInfo {
    Path                     source_file;
    std::optional<std::string> provides_module;   // name of module this file defines
    std::vector<ModuleDep>   imports;
};

struct ModuleGraph {
    std::vector<SourceModuleInfo> sources;

    // Map: module_name → source that provides it.
    std::unordered_map<std::string, Path> providers;
};

// ─── BMI (Binary Module Interface) ───────────────────────────────────────────

struct BmiCache {
    // module_name → path to .bmi file on disk
    std::unordered_map<std::string, Path> bmi_by_module;
    Path                                  bmi_dir;
};

// ─── Scan API ─────────────────────────────────────────────────────────────────

struct ScanOptions {
    std::vector<Path>        include_paths;
    std::vector<std::string> defines;
    std::string              cxx_std = "c++23";
    std::string              target_triple;
};

/// Scan `sources` for module declarations and imports.
/// Uses `clang-scan-deps -format=p1689` under the hood.
[[nodiscard]] Result<ModuleGraph> scan(const std::vector<Path>& sources,
                                        const toolchain::ToolchainInfo& tc,
                                        const ScanOptions& opts);

/// Augment a BuildGraph with module-dependency edges so that BMI compile
/// tasks are ordered before their consumers.
[[nodiscard]] Result<void> inject_module_deps(build::BuildGraph& graph,
                                               const ModuleGraph& mod_graph,
                                               const BmiCache& bmi_cache);

} // namespace rivet::modules
