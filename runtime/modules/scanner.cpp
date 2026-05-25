// runtime/modules/scanner.cpp — C++20 module dependency scanner stub
#include "scanner.hpp"
#include "../../platform/interface/process.hpp"
#include "../../platform/interface/fs.hpp"

#include <format>

namespace rivet::modules {

// ─── scan() ──────────────────────────────────────────────────────────────────

Result<ModuleGraph> scan(const std::vector<Path>& sources,
                          const toolchain::ToolchainInfo& tc,
                          const ScanOptions& opts) {
    ModuleGraph graph;

    // Phase 4 TODO:
    //   1. Build a compilation database JSON for clang-scan-deps.
    //   2. Invoke: clang-scan-deps -format=p1689 -compilation-database=...
    //   3. Parse the P1689 JSON output into ModuleGraph.
    //
    // For now: assume no modules are in use (all files are traditional headers).
    for (const auto& src : sources) {
        SourceModuleInfo info;
        info.source_file = src;
        // No module declaration detected (stub).
        graph.sources.push_back(std::move(info));
    }

    return graph;
}

// ─── inject_module_deps() ────────────────────────────────────────────────────

Result<void> inject_module_deps(build::BuildGraph& /*graph*/,
                                  const ModuleGraph& /*mod_graph*/,
                                  const BmiCache& /*bmi_cache*/) {
    // Phase 4 TODO: for each (consumer, provider) pair in mod_graph,
    // add a dependency edge from the BMI-compile task to the consumer task.
    return {};
}

} // namespace rivet::modules
