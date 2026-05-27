// runtime/build/multi_target.cpp — multi-artefact build engine
#include "multi_target.hpp"

#include "../toolchain/compile.hpp"
#include "../toolchain/triple.hpp"
#include "../cache/store.hpp"
#include "../cache/key.hpp"
#include "../../platform/interface/fs.hpp"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace rivet::build {

namespace {

// ─── Host triple → cfg("os = ...") evaluation ───────────────────────────────

constexpr std::string_view host_os() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

bool cfg_matches_host(const pkg::CfgPredicate& p) {
    if (p.os.has_value() && *p.os != host_os()) return false;
    return true;
}

// Cross-target variant: evaluates the cfg predicate against `target_os`
// rather than the host. Used by `rivet check --target=<triple>` so a
// macOS user can preview a Linux build's compile lines without hitting
// the host's cfg branches.
bool cfg_matches_target(const pkg::CfgPredicate& p, std::string_view target_os) {
    if (target_os.empty()) return cfg_matches_host(p);
    if (p.os.has_value() && *p.os != target_os) return false;
    return true;
}

// Pull the os component out of a target triple string. Best-effort —
// returns "" when the triple is empty (caller falls back to host).
//
// Real-world triples don't follow a single layout:
//   x86_64-linux-gnu             → arch-OS-abi      (os = linux)
//   arm64-apple-macos11          → arch-vendor-os
//   x86_64-pc-windows-msvc       → arch-vendor-os-abi
// So we scan the whole triple for known OS substrings, not just the second
// component. Returns the canonical "linux" / "macos" / "windows" form that
// rivet.toml's [[lib.cfg]] os = "..." entries are written against.
std::string target_os_from_triple(std::string_view triple) {
    if (triple.empty()) return {};
    if (triple.find("windows") != std::string_view::npos) return "windows";
    if (triple.find("macos")   != std::string_view::npos) return "macos";
    if (triple.find("darwin")  != std::string_view::npos) return "macos";
    if (triple.find("apple")   != std::string_view::npos) return "macos";
    if (triple.find("linux")   != std::string_view::npos) return "linux";
    // Fall through: let the Triple parser guess the os component.
    auto t = toolchain::Triple::from_string(triple);
    return t.os;
}

// ─── Source-list resolution ─────────────────────────────────────────────────
//
// Each source entry can be (a) a literal relative path or (b) a simple
// glob `dir/*.ext` / `dir/**/*.ext`. We deliberately keep glob support
// minimal — anything fancier is a manifest-quality issue and the user
// can just list the files.

bool is_glob(const std::string& s) {
    return s.find('*') != std::string::npos;
}

std::vector<std::string> source_extensions() {
    return {".c", ".cpp", ".cxx", ".cc", ".cxx", ".C"};
}

bool is_source_file(const Path& p) {
    auto ext = p.extension().string();
    if (ext.empty()) return false;
    for (const auto& s : source_extensions()) if (ext == s) return true;
    return false;
}

bool wildcard_match(std::string_view name, std::string_view pat) {
    // Single-segment wildcard: `*.ext`, `prefix_*.cpp`. No `?`, no `[...]`.
    if (pat == "*") return true;
    auto star = pat.find('*');
    if (star == std::string_view::npos) return name == pat;
    auto head = pat.substr(0, star);
    auto tail = pat.substr(star + 1);
    if (name.size() < head.size() + tail.size()) return false;
    if (name.substr(0, head.size()) != head) return false;
    if (name.substr(name.size() - tail.size()) != tail) return false;
    return true;
}

void expand_glob_into(const Path& root, const std::string& pattern,
                      std::vector<Path>& out) {
    // Split into directory prefix + filename pattern. The recursive `**`
    // segment, if present, means descend into all subdirs from that point.
    size_t star = pattern.find('*');
    std::string dir_prefix;
    bool recursive = false;
    std::string filename_pat;

    auto last_slash = pattern.rfind('/', star == std::string::npos ? pattern.size() : star);
    if (last_slash == std::string::npos) {
        filename_pat = pattern;
    } else {
        dir_prefix = pattern.substr(0, last_slash);
        filename_pat = pattern.substr(last_slash + 1);
    }
    // `dir/**/*.ext` → strip `**` from dir_prefix.
    if (auto pos = dir_prefix.find("/**"); pos != std::string::npos) {
        recursive = true;
        dir_prefix.erase(pos);
    } else if (dir_prefix == "**") {
        recursive = true;
        dir_prefix.clear();
    }

    Path base = dir_prefix.empty() ? root : (root / dir_prefix);
    if (!rivet::fs::exists(base).value_or(false)) return;

    auto consider = [&](const Path& p) {
        auto rel = p.lexically_relative(root);
        auto fn = rel.filename().string();
        if (!is_source_file(p)) return;
        if (wildcard_match(fn, filename_pat)) out.push_back(p);
    };

    auto entries = rivet::fs::list_dir(base);
    if (!entries) return;
    for (const auto& e : *entries) {
        auto stat_r = rivet::fs::stat(e);
        if (stat_r && stat_r->is_dir) {
            if (recursive) {
                // Recurse into subdirs, applying the same filename_pat.
                std::vector<Path> sub;
                expand_glob_into(e, std::string("**/") + filename_pat, sub);
                for (auto& s : sub) out.push_back(std::move(s));
            }
        } else {
            consider(e);
        }
    }
}

std::vector<Path> resolve_sources(const Path& root,
                                   const std::vector<std::string>& entries) {
    std::vector<Path> out;
    for (const auto& e : entries) {
        if (is_glob(e)) {
            expand_glob_into(root, e, out);
        } else {
            out.push_back(root / e);
        }
    }
    // Deduplicate while preserving order.
    std::unordered_set<std::string> seen;
    std::vector<Path> dedup;
    for (auto& p : out) {
        std::string k = p.lexically_normal().string();
        if (seen.insert(k).second) dedup.push_back(std::move(p));
    }
    return dedup;
}

// ─── Topological sort of targets by depends_on ──────────────────────────────

Result<std::vector<size_t>>
topo_targets(const std::vector<pkg::Target>& targets) {
    std::unordered_map<std::string, size_t> by_name;
    for (size_t i = 0; i < targets.size(); ++i) by_name[targets[i].name] = i;

    enum class Mark : uint8_t { White, Grey, Black };
    std::vector<Mark> mark(targets.size(), Mark::White);
    std::vector<size_t> order;

    std::function<Result<void>(size_t, std::vector<std::string>&)> visit =
        [&](size_t i, std::vector<std::string>& stack) -> Result<void> {
        if (mark[i] == Mark::Black) return {};
        if (mark[i] == Mark::Grey) {
            std::string cycle;
            for (const auto& n : stack) { cycle += n; cycle += " → "; }
            cycle += targets[i].name;
            return make_error("dependency cycle: " + cycle);
        }
        mark[i] = Mark::Grey;
        stack.push_back(targets[i].name);
        for (const auto& dep : targets[i].depends_on) {
            auto it = by_name.find(dep);
            if (it == by_name.end())
                return make_error("target '" + targets[i].name +
                                  "' depends on unknown '" + dep + "'");
            RIVET_TRY(visit(it->second, stack));
        }
        stack.pop_back();
        mark[i] = Mark::Black;
        order.push_back(i);
        return {};
    };

    for (size_t i = 0; i < targets.size(); ++i) {
        std::vector<std::string> stack;
        RIVET_TRY(visit(i, stack));
    }
    return order;
}

// ─── Artefact paths (per target) ────────────────────────────────────────────

Path lib_path(const Path& root, std::string_view profile, std::string_view name) {
#if defined(_WIN32)
    return root / ".rivet" / "build" / std::string(profile) / "lib" / (std::string(name) + ".lib");
#else
    return root / ".rivet" / "build" / std::string(profile) / "lib" / ("lib" + std::string(name) + ".a");
#endif
}

Path bin_path(const Path& root, std::string_view profile, std::string_view name,
              bool is_test) {
#if defined(_WIN32)
    std::string fn = std::string(name) + ".exe";
#else
    std::string fn = std::string(name);
#endif
    return root / ".rivet" / "build" / std::string(profile) /
           (is_test ? "tests" : "bin") / fn;
}

// ─── Incremental-rebuild oracle (mirrors cmd_build's logic) ─────────────────

bool is_up_to_date(const Path& out, const Path& dep_file) {
    if (!rivet::fs::exists(out).value_or(false)) return false;
    auto out_stat = rivet::fs::stat(out);
    if (!out_stat) return false;
    if (!rivet::fs::exists(dep_file).value_or(false)) return true;
    auto dep_stat = rivet::fs::stat(dep_file);
    if (!dep_stat) return true;
    // Naive: if .d file is newer than .o, recompile to be safe.
    return out_stat->mtime_ns >= dep_stat->mtime_ns;
}

} // namespace

Result<std::vector<TargetArtifact>>
build_targets(const pkg::Manifest& manifest,
              const toolchain::ToolchainInfo& tc,
              const BuildConfig& base_cfg,
              const InstalledExternalDeps& external_deps,
              std::string_view profile_name,
              BuildGraph& graph,
              cache::Store* cache_store) {
    (void)cache_store;  // cache integration is per-task via Executor

    // Resolve cfg-evaluation OS once. When base_cfg.target_triple is empty
    // (the default), fall back to host_os(). cmd_check sets it explicitly
    // so cross-target previews evaluate Linux/Windows cfg correctly on a
    // Mac host.
    std::string target_os = target_os_from_triple(base_cfg.target_triple);
    if (target_os.empty()) target_os = host_os();

    auto order_r = topo_targets(manifest.targets);
    if (!order_r) return propagate<std::vector<TargetArtifact>>(order_r);

    std::vector<TargetArtifact> results;
    std::unordered_map<std::string, TargetArtifact> by_name;

    auto obj_dir_for = [&](const std::string& tname) {
        return manifest.root_dir / ".rivet" / "build" / std::string(profile_name)
               / "obj" / tname;
    };

    for (size_t idx : *order_r) {
        const auto& tgt = manifest.targets[idx];

        // Apply matching cfg overrides on top of the literal target fields.
        std::vector<std::string> all_sources       = tgt.sources;
        std::vector<std::string> all_link_libs     = tgt.link_libs;
        std::vector<std::string> all_compile_flags = tgt.compile_flags;
        for (const auto& ov : tgt.cfg_overrides) {
            if (!cfg_matches_target(ov.cfg, target_os)) continue;
            for (const auto& s : ov.extra_sources)       all_sources.push_back(s);
            for (const auto& l : ov.extra_link_libs)     all_link_libs.push_back(l);
            for (const auto& f : ov.extra_compile_flags) all_compile_flags.push_back(f);
        }

        // For bins/tests with no explicit sources, the `path` entry-point
        // is the single source file.
        if (all_sources.empty() && !tgt.path.empty())
            all_sources.push_back(tgt.path);

        auto resolved = resolve_sources(manifest.root_dir, all_sources);
        if (resolved.empty() && tgt.kind != pkg::TargetKind::Bin
                              && tgt.kind != pkg::TargetKind::Test) {
            return make_error<std::vector<TargetArtifact>>(
                "target '" + tgt.name + "' has no source files (after cfg/glob expansion)");
        }

        // Per-target compile config (added to base).
        BuildConfig tgt_cfg = base_cfg;
        for (const auto& f : all_compile_flags) tgt_cfg.extra_flags.push_back(f);
        for (const auto& d : tgt.defines)       tgt_cfg.defines.push_back(d);
        for (const auto& inc : tgt.include_dirs)
            tgt_cfg.include_paths.push_back(manifest.root_dir / inc);

        // External deps' include dirs already in base_cfg; nothing to add.

        std::vector<TaskId> obj_tasks;
        std::vector<Path>   obj_paths;

        for (const auto& src : resolved) {
            auto rel = src.lexically_relative(manifest.root_dir).string();
            auto out_path = obj_dir_for(tgt.name) / (rel + ".o");

            // Per-source compile-flag overrides: literal path match first.
            std::vector<std::string> extra_for_src;
            for (const auto& [pat, flags] : tgt.per_source_flags) {
                if (pat == rel || (is_glob(pat) && wildcard_match(rel, pat))) {
                    for (const auto& f : flags) extra_for_src.push_back(f);
                }
            }

            BuildConfig job_cfg = tgt_cfg;
            for (const auto& f : extra_for_src) job_cfg.extra_flags.push_back(f);

            // Incremental: if the obj is fresh, emit a Phony so dependents
            // still link against the existing file.
            auto dep_file = out_path.parent_path() / (out_path.filename().string() + ".d");
            if (is_up_to_date(out_path, dep_file)) {
                TaskNode phony;
                phony.name    = src.filename().string() + " [" + tgt.name + "] (up to date)";
                phony.kind    = TaskKind::Phony;
                phony.outputs = {{ out_path, true }};
                auto id = graph.add(std::move(phony));
                obj_tasks.push_back(id);
                obj_paths.push_back(out_path);
                continue;
            }

            auto cj    = toolchain::compile_job_from(src, out_path, job_cfg, tc);
            auto cmd_r = toolchain::make_compile_command(cj, tc);
            if (!cmd_r)
                return make_error<std::vector<TargetArtifact>>(
                    "compile command (" + tgt.name + "/" + rel + "): " + cmd_r.error().message);

            TaskNode node;
            node.name    = src.filename().string() + " [" + tgt.name + "]";
            node.kind    = TaskKind::Compile;
            node.outputs = {{ out_path, true }};
            node.command = std::move(*cmd_r);

            std::string src_hash;
            if (auto hr = cache::sha256_file(src)) src_hash = std::move(*hr);
            node.inputs = {{ src, std::move(src_hash) }};

            if (auto kr = cache::derive_key(node, tc.version, base_cfg.target_triple))
                node.cache_key = std::move(*kr);

            auto id = graph.add(std::move(node));
            obj_tasks.push_back(id);
            obj_paths.push_back(out_path);
        }

        // Build the final artefact: archive for lib/vendor, link for bin/test.
        TargetArtifact artefact;
        artefact.name = tgt.name;
        artefact.kind = tgt.kind;

        if (tgt.kind == pkg::TargetKind::Lib || tgt.kind == pkg::TargetKind::Vendor) {
            artefact.artifact_path = lib_path(manifest.root_dir, profile_name, tgt.name);

            toolchain::ArchiveJob aj;
            aj.inputs        = obj_paths;
            aj.output        = artefact.artifact_path;
            aj.target_triple = base_cfg.target_triple;
            auto cmd_r = toolchain::make_archive_command(aj, tc);
            if (!cmd_r)
                return make_error<std::vector<TargetArtifact>>(
                    "archive (" + tgt.name + "): " + cmd_r.error().message);

            TaskNode node;
            node.name    = tgt.name + " [archive]";
            node.kind    = TaskKind::Archive;
            node.deps    = obj_tasks;
            node.outputs = {{ artefact.artifact_path, true }};
            node.command = std::move(*cmd_r);
            artefact.final_task = graph.add(std::move(node));
        } else {
            bool is_test = (tgt.kind == pkg::TargetKind::Test);
            artefact.artifact_path = bin_path(manifest.root_dir, profile_name,
                                              tgt.name, is_test);

            toolchain::LinkJob lj;
            lj.inputs        = obj_paths;
            lj.output        = artefact.artifact_path;
            lj.target_triple = base_cfg.target_triple;
            lj.lto           = base_cfg.lto;
            lj.sanitizers    = base_cfg.sanitizers;

            // Intra-manifest deps: pull every TRANSITIVE archive in as a
            // link input, plus the dep's cfg-active link_libs (system libs
            // that the dep itself requires, e.g. -framework on macOS or
            // -lssl on linux). Static-archive linking is order-sensitive on
            // POSIX ld — dependents come first, dependencies last — so we
            // emit in DFS order from the user's target outwards. by_name
            // already holds artefacts in build order (deepest deps first)
            // because we populate it as we iterate the topo order, but
            // that's not the right order for *linking* — we need the
            // visit-from-root order.
            std::vector<TaskId> dep_task_ids;
            {
                std::unordered_set<std::string> seen;
                std::function<Result<void>(const std::string&)> visit =
                    [&](const std::string& dep_name) -> Result<void> {
                    if (!seen.insert(dep_name).second) return {};
                    auto by_it = by_name.find(dep_name);
                    if (by_it == by_name.end())
                        return make_error("target '" + tgt.name +
                            "' depends on unknown '" + dep_name + "'");
                    if (by_it->second.kind != pkg::TargetKind::Lib
                     && by_it->second.kind != pkg::TargetKind::Vendor)
                        return make_error("target '" + tgt.name +
                            "' depends on '" + dep_name +
                            "' which is not a lib/vendor");

                    lj.inputs.push_back(by_it->second.artifact_path);
                    dep_task_ids.push_back(by_it->second.final_task);

                    // Look up the original target to recurse into its own
                    // depends_on AND pull in its cfg-active link_libs.
                    const pkg::Target* dep_tgt = nullptr;
                    for (const auto& t : manifest.targets)
                        if (t.name == dep_name) { dep_tgt = &t; break; }
                    if (!dep_tgt) return {};  // shouldn't happen
                    for (const auto& l : dep_tgt->link_libs) lj.flags.push_back(l);
                    for (const auto& ov : dep_tgt->cfg_overrides) {
                        if (!cfg_matches_target(ov.cfg, target_os)) continue;
                        for (const auto& l : ov.extra_link_libs)
                            lj.flags.push_back(l);
                    }
                    for (const auto& sub : dep_tgt->depends_on)
                        RIVET_TRY(visit(sub));
                    return {};
                };
                for (const auto& dep_name : tgt.depends_on) {
                    auto vr = visit(dep_name);
                    if (!vr)
                        return make_error<std::vector<TargetArtifact>>(vr.error().message);
                }
            }

            // External deps from rivet.toml [dependencies] (already
            // resolved via pkg-config / file glob upstream).
            for (const auto& p : external_deps.lib_dirs)
                lj.lib_search_paths.push_back(p.string());
            for (const auto& l : external_deps.link_libs)
                lj.link_libs.push_back(l);
            for (const auto& f : external_deps.link_flags)
                lj.flags.push_back(f);
            // Target-declared link_libs (post cfg merge).
            for (const auto& l : all_link_libs)
                lj.flags.push_back(l);

            auto cmd_r = toolchain::make_link_command(lj, tc);
            if (!cmd_r)
                return make_error<std::vector<TargetArtifact>>(
                    "link (" + tgt.name + "): " + cmd_r.error().message);

            TaskNode node;
            node.name    = tgt.name + " [link]";
            node.kind    = TaskKind::Link;
            node.deps    = obj_tasks;
            for (auto did : dep_task_ids) node.deps.push_back(did);
            for (const auto& o : obj_paths) node.inputs.push_back({ o, "" });
            node.outputs = {{ artefact.artifact_path, true }};
            node.command = std::move(*cmd_r);
            artefact.final_task = graph.add(std::move(node));
        }

        by_name[artefact.name] = artefact;
        results.push_back(std::move(artefact));
    }

    return results;
}

} // namespace rivet::build
