// runtime/modules/scanner.cpp — C++20 module dependency scanner
#include "scanner.hpp"
#include "../build/graph.hpp"
#include "../../platform/interface/process.hpp"
#include "../../platform/interface/fs.hpp"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace rivet::modules {

// ─── P1689 JSON parser ────────────────────────────────────────────────────────
//
// Hand-rolled scanner for well-formed clang-scan-deps -format=p1689 output.
// We never need error recovery — if clang-scan-deps wrote it, it is valid JSON.

namespace {

// Advance `pos` past whitespace.
static void skip_ws(std::string_view s, std::size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
                              s[pos] == '\r' || s[pos] == '\n'))
        ++pos;
}

// Consume the next JSON string value (pos must be pointing at the opening '"').
// Returns the unescaped contents and advances pos past the closing '"'.
// Returns empty string if pos does not point at '"'.
static std::string read_string(std::string_view s, std::size_t& pos) {
    if (pos >= s.size() || s[pos] != '"') return {};
    ++pos; // skip opening "
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            ++pos; // skip backslash
            char c = s[pos];
            switch (c) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += c;   break;
            }
        } else {
            result += s[pos];
        }
        ++pos;
    }
    if (pos < s.size()) ++pos; // skip closing "
    return result;
}

// Find the value (string content) for a JSON key in an object.
// Scans forward from `pos`, looking for `"key"` followed by `:` then a string.
// Returns the value string, or empty if not found before the matching `}`.
// Does NOT handle nested objects or arrays — only flat key lookups.
// `pos` is NOT modified; use find_key_pos for advancing.
static std::string find_string_value(std::string_view s, std::size_t start,
                                      std::string_view key) {
    // We scan the raw text for the key pattern rather than a full parse.
    std::size_t pos = start;
    int depth = 0;
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '{') { ++depth; ++pos; continue; }
        if (c == '}') {
            if (depth == 0) break;
            --depth; ++pos; continue;
        }
        if (c == '[') { ++depth; ++pos; continue; }
        if (c == ']') {
            if (depth == 0) break;
            --depth; ++pos; continue;
        }
        if (c == '"') {
            std::size_t tmp = pos;
            std::string k = read_string(s, tmp);
            skip_ws(s, tmp);
            if (tmp < s.size() && s[tmp] == ':' && k == key) {
                ++tmp; // skip ':'
                skip_ws(s, tmp);
                if (tmp < s.size() && s[tmp] == '"') {
                    return read_string(s, tmp);
                }
                if (tmp < s.size() && (s[tmp] == 't' || s[tmp] == 'f')) {
                    // boolean — return "true"/"false" as string
                    bool val = (s[tmp] == 't');
                    return val ? "true" : "false";
                }
                return {};
            }
            pos = tmp;
            continue;
        }
        ++pos;
    }
    return {};
}

// Returns true if the boolean JSON value for `key` at `start` is true.
static bool find_bool_value(std::string_view s, std::size_t start,
                             std::string_view key) {
    return find_string_value(s, start, key) == "true";
}

// Struct holding extracted info from one P1689 rule object.
struct P1689Rule {
    std::string                primary_output;  // may be empty
    std::string                source_path;     // from "provides[].source-path"
    std::optional<std::string> provides_module; // logical-name if is-interface
    std::vector<std::string>   requires_modules;
};

// Parse the contents of a "provides" array.
// `pos` points just after the '['; advances past the matching ']'.
static void parse_provides(std::string_view s, std::size_t& pos,
                            P1689Rule& rule) {
    // Each element is a JSON object { "logical-name":..., "source-path":..., "is-interface":... }
    while (pos < s.size()) {
        skip_ws(s, pos);
        if (pos >= s.size()) break;
        if (s[pos] == ']') { ++pos; break; }
        if (s[pos] == '{') {
            ++pos; // skip '{'
            std::size_t obj_start = pos;
            // Find matching '}'
            int inner = 1;
            std::size_t obj_end = pos;
            while (obj_end < s.size() && inner > 0) {
                if (s[obj_end] == '{') ++inner;
                else if (s[obj_end] == '}') --inner;
                // Skip strings to avoid counting braces inside them
                else if (s[obj_end] == '"') {
                    ++obj_end;
                    while (obj_end < s.size() && s[obj_end] != '"') {
                        if (s[obj_end] == '\\') ++obj_end;
                        ++obj_end;
                    }
                }
                if (inner > 0) ++obj_end;
            }
            // Extract fields from [obj_start, obj_end)
            std::string obj_text(s.substr(obj_start, obj_end - obj_start));
            std::string logical_name = find_string_value(obj_text, 0, "logical-name");
            std::string src_path     = find_string_value(obj_text, 0, "source-path");
            bool is_iface = find_bool_value(obj_text, 0, "is-interface");

            if (!logical_name.empty() && is_iface) {
                rule.provides_module = logical_name;
            }
            if (!src_path.empty() && rule.source_path.empty()) {
                rule.source_path = src_path;
            }
            pos = obj_end + 1; // skip past '}'
            continue;
        }
        ++pos;
    }
}

// Parse the contents of a "requires" array.
// `pos` points just after the '['; advances past the matching ']'.
static void parse_requires(std::string_view s, std::size_t& pos,
                            P1689Rule& rule) {
    while (pos < s.size()) {
        skip_ws(s, pos);
        if (pos >= s.size()) break;
        if (s[pos] == ']') { ++pos; break; }
        if (s[pos] == '{') {
            ++pos;
            std::size_t obj_start = pos;
            int inner = 1;
            std::size_t obj_end = pos;
            while (obj_end < s.size() && inner > 0) {
                if (s[obj_end] == '{') ++inner;
                else if (s[obj_end] == '}') --inner;
                else if (s[obj_end] == '"') {
                    ++obj_end;
                    while (obj_end < s.size() && s[obj_end] != '"') {
                        if (s[obj_end] == '\\') ++obj_end;
                        ++obj_end;
                    }
                }
                if (inner > 0) ++obj_end;
            }
            std::string obj_text(s.substr(obj_start, obj_end - obj_start));
            std::string logical_name = find_string_value(obj_text, 0, "logical-name");
            if (!logical_name.empty()) {
                rule.requires_modules.push_back(std::move(logical_name));
            }
            pos = obj_end + 1;
            continue;
        }
        ++pos;
    }
}

// Top-level P1689 parser. Returns one P1689Rule per entry in the "rules" array.
static std::vector<P1689Rule> parse_p1689(std::string_view json) {
    std::vector<P1689Rule> rules;

    // Locate "rules" array.
    auto rules_key = json.find("\"rules\"");
    if (rules_key == std::string_view::npos) return rules;

    std::size_t pos = rules_key + 7; // skip past "rules"
    skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') return rules;
    ++pos;
    skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != '[') return rules;
    ++pos; // skip '['

    // Iterate over rule objects.
    while (pos < json.size()) {
        skip_ws(json, pos);
        if (pos >= json.size()) break;
        if (json[pos] == ']') { ++pos; break; }
        if (json[pos] != '{') { ++pos; continue; }

        ++pos; // skip '{'
        P1689Rule rule;

        // Walk key-value pairs inside this rule object.
        // We need to handle nested arrays ("provides", "requires") specially.
        while (pos < json.size()) {
            skip_ws(json, pos);
            if (pos >= json.size()) break;
            if (json[pos] == '}') { ++pos; break; }
            if (json[pos] != '"') { ++pos; continue; }

            // Read key
            std::string key = read_string(json, pos);
            skip_ws(json, pos);
            if (pos >= json.size() || json[pos] != ':') continue;
            ++pos; // skip ':'
            skip_ws(json, pos);
            if (pos >= json.size()) break;

            if (key == "primary-output") {
                if (json[pos] == '"') {
                    rule.primary_output = read_string(json, pos);
                }
            } else if (key == "provides") {
                if (json[pos] == '[') {
                    ++pos; // skip '['
                    parse_provides(json, pos, rule);
                }
            } else if (key == "requires") {
                if (json[pos] == '[') {
                    ++pos; // skip '['
                    parse_requires(json, pos, rule);
                }
            } else {
                // Skip the value (string, number, object, array, or literal).
                if (json[pos] == '"') {
                    read_string(json, pos);
                } else if (json[pos] == '{' || json[pos] == '[') {
                    // Skip nested structure.
                    char open  = json[pos];
                    char close = (open == '{') ? '}' : ']';
                    int depth = 1;
                    ++pos;
                    while (pos < json.size() && depth > 0) {
                        if (json[pos] == open)  ++depth;
                        else if (json[pos] == close) --depth;
                        else if (json[pos] == '"') {
                            ++pos;
                            while (pos < json.size() && json[pos] != '"') {
                                if (json[pos] == '\\') ++pos;
                                ++pos;
                            }
                        }
                        if (depth > 0) ++pos;
                    }
                    if (pos < json.size()) ++pos; // skip closing bracket
                } else {
                    // number, true, false, null
                    while (pos < json.size() && json[pos] != ',' &&
                           json[pos] != '}' && json[pos] != ']' &&
                           json[pos] != ' ' && json[pos] != '\n') {
                        ++pos;
                    }
                }
            }

            // Skip optional comma
            skip_ws(json, pos);
            if (pos < json.size() && json[pos] == ',') ++pos;
        }

        rules.push_back(std::move(rule));

        // Skip optional comma between rule objects
        skip_ws(json, pos);
        if (pos < json.size() && json[pos] == ',') ++pos;
    }

    return rules;
}

// Check whether a file exists on disk (best-effort; no exceptions).
static bool file_exists(const Path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

} // anonymous namespace

// ─── scan() ──────────────────────────────────────────────────────────────────

Result<ModuleGraph> scan(const std::vector<Path>& sources,
                          const toolchain::ToolchainInfo& tc,
                          const ScanOptions& opts) {
    ModuleGraph mod_graph;

    // Pre-populate sources with basic info (no module data yet).
    mod_graph.sources.reserve(sources.size());
    for (const auto& src : sources) {
        SourceModuleInfo info;
        info.source_file = src;
        mod_graph.sources.push_back(std::move(info));
    }

    // Check that clang-scan-deps is available.
    Path scan_deps_bin = tc.scan_deps();
    if (!file_exists(scan_deps_bin)) {
        // Tool not found — return the graph with no module info (graceful fallback).
        return mod_graph;
    }

    // Run clang-scan-deps once per source file in P1689 mode.
    // We invoke: clang-scan-deps -format=p1689 -- clang++ -std=<cxx_std> -c <file>
    for (auto& source_info : mod_graph.sources) {
        const Path& src = source_info.source_file;

        // Build the argument list.
        process::SpawnOptions run_opts;
        run_opts.inherit_env     = false;
        run_opts.capture_stdout  = true;
        run_opts.capture_stderr  = true;

        std::vector<std::string> args;
        args.push_back(scan_deps_bin.string());
        args.push_back("-format=p1689");
        args.push_back("--");
        args.push_back(tc.clangpp().string());
        args.push_back(std::format("-std={}", opts.cxx_std));

        // Pass include paths.
        for (const auto& inc : opts.include_paths) {
            args.push_back("-I" + inc.string());
        }

        // Pass preprocessor defines.
        for (const auto& def : opts.defines) {
            args.push_back("-D" + def);
        }

        if (!opts.target_triple.empty()) {
            args.push_back("--target=" + opts.target_triple);
        }

        args.push_back("-c");
        args.push_back(src.string());

        run_opts.args = std::move(args);

        // Run clang-scan-deps and capture its stdout.
        auto result = process::run(std::move(run_opts));
        if (!result.has_value()) {
            // Spawn failed — skip this file gracefully.
            continue;
        }

        if (result->exit_code != 0 || result->stdout_output.empty()) {
            // clang-scan-deps failed (e.g. not a module file) — skip gracefully.
            continue;
        }

        // Parse the P1689 JSON.
        auto rules = parse_p1689(result->stdout_output);

        for (const auto& rule : rules) {
            // Determine which source this rule corresponds to.
            // clang-scan-deps emits source-path inside "provides", or we match
            // by primary-output / the source we just scanned.
            //
            // Since we invoke clang-scan-deps one file at a time, each result
            // corresponds to `src`.

            // Record what this file provides.
            if (rule.provides_module.has_value()) {
                source_info.provides_module = rule.provides_module;
                // Record in the providers map.
                mod_graph.providers[*rule.provides_module] = src;
            }

            // Record what this file imports.
            for (const auto& req : rule.requires_modules) {
                ModuleDep dep;
                dep.module_name   = req;
                // Module partitions use ':' as separator.
                dep.is_partition  = (req.find(':') != std::string::npos);
                source_info.imports.push_back(std::move(dep));
            }
        }
    }

    return mod_graph;
}

// ─── inject_module_deps() ────────────────────────────────────────────────────

Result<void> inject_module_deps(build::BuildGraph& graph,
                                  const ModuleGraph& mod_graph,
                                  const BmiCache& bmi_cache) {
    using build::TaskId;
    using build::TaskKind;

    // Build a map: source path (string) → TaskId for Compile/CompileModule tasks.
    // We identify the task by matching its primary output path or its name against
    // source file paths in the inputs list.
    std::unordered_map<std::string, TaskId> source_to_task;
    for (const auto& node : graph.nodes()) {
        if (node.kind != TaskKind::Compile && node.kind != TaskKind::CompileModule)
            continue;
        for (const auto& inp : node.inputs) {
            source_to_task[inp.path.string()] = node.id;
        }
    }

    // Build a map: BMI path (string) → TaskId for CompileModule tasks.
    std::unordered_map<std::string, TaskId> bmi_to_task;
    for (const auto& node : graph.nodes()) {
        if (node.kind != TaskKind::CompileModule) continue;
        for (const auto& out : node.outputs) {
            bmi_to_task[out.path.string()] = node.id;
        }
    }

    // For each source that has imports, wire up the dependency edges.
    for (const auto& src_info : mod_graph.sources) {
        if (src_info.imports.empty()) continue;

        // Find the compile task for this source.
        auto task_it = source_to_task.find(src_info.source_file.string());
        if (task_it == source_to_task.end()) {
            // No compile task found for this source — skip gracefully.
            continue;
        }
        TaskId compile_task_id = task_it->second;

        build::TaskNode* compile_node = graph.find(compile_task_id);
        if (!compile_node) continue;

        for (const auto& dep : src_info.imports) {
            // Look up the BMI path in the cache.
            auto bmi_it = bmi_cache.bmi_by_module.find(dep.module_name);
            if (bmi_it == bmi_cache.bmi_by_module.end()) {
                // Module not in cache — skip (might be a stdlib module or external).
                continue;
            }
            const Path& bmi_path = bmi_it->second;

            // Find the CompileModule task that produces this BMI.
            auto bmi_task_it = bmi_to_task.find(bmi_path.string());
            if (bmi_task_it == bmi_to_task.end()) {
                // No producer task found — skip gracefully.
                continue;
            }
            TaskId bmi_producer_id = bmi_task_it->second;

            // Add the dependency edge: BMI must be built before this compile.
            // Ignore errors (e.g. duplicate edge).
            (void)graph.add_dep(bmi_producer_id, compile_task_id);

            // Inject -fmodule-file=<name>=<path> into the compile command args.
            if (compile_node->command.has_value()) {
                std::string flag = std::format("-fmodule-file={}={}",
                                               dep.module_name,
                                               bmi_path.string());
                // Only add if not already present.
                auto& args = compile_node->command->args;
                bool already_present = false;
                for (const auto& arg : args) {
                    if (arg == flag) { already_present = true; break; }
                }
                if (!already_present) {
                    args.push_back(std::move(flag));
                }
            }
        }
    }

    return {};
}

} // namespace rivet::modules
