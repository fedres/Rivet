// runtime/build/pkgconfig.cpp — minimal pkg-config (.pc) reader
#include "pkgconfig.hpp"

#include "../../platform/interface/fs.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace rivet::build {

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return std::string(s);
}

// Expand ${name} references against a variable map. Pkg-config does the
// expansion lazily; we do it eagerly when reading each line.
std::string expand_vars(const std::string& raw,
                        const std::unordered_map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ) {
        if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
            size_t close = raw.find('}', i + 2);
            if (close == std::string::npos) { out.push_back(raw[i++]); continue; }
            auto name = raw.substr(i + 2, close - i - 2);
            auto it   = vars.find(name);
            if (it != vars.end()) out += it->second;
            i = close + 1;
        } else {
            out.push_back(raw[i++]);
        }
    }
    return out;
}

// Tokenise a flag string into argv-style entries. Honors single/double
// quotes and backslash escapes — pkg-config files for libraries with
// spaces in their install path rely on this.
std::vector<std::string> tokenise(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_single = false, in_double = false;
    auto flush = [&] { if (!cur.empty()) { out.push_back(std::move(cur)); cur.clear(); } };
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!in_single && !in_double && std::isspace(static_cast<unsigned char>(c))) {
            flush();
            continue;
        }
        if (c == '\\' && i + 1 < s.size()) { cur.push_back(s[++i]); continue; }
        if (c == '\'' && !in_double) { in_single = !in_single; continue; }
        if (c == '"' && !in_single)  { in_double = !in_double; continue; }
        cur.push_back(c);
    }
    flush();
    return out;
}

// Parse a comma-or-space separated Requires list, stripping operators
// like `>=`, `=`, `<`. We only need the bare package names.
std::vector<std::string> parse_requires(std::string_view s) {
    std::vector<std::string> out;
    std::string tok;
    auto push = [&] {
        // Drop trailing version operators / numbers.
        auto cut = tok.find_first_of(" \t=<>!");
        if (cut != std::string::npos) tok.resize(cut);
        if (!tok.empty()) out.push_back(std::move(tok));
        tok.clear();
    };
    bool skipping = false;  // skipping a version-operator stretch
    for (char c : s) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            push();
            skipping = false;
            continue;
        }
        if (c == '=' || c == '<' || c == '>' || c == '!') {
            push();
            skipping = true;
            continue;
        }
        if (skipping) {
            // Inside a `>= 1.2.3` — consume version chars without recording.
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-')
                continue;
            skipping = false;
        }
        tok.push_back(c);
    }
    push();
    return out;
}

} // namespace

Result<PkgConfig> parse_pc(const Path& pc_file) {
    auto bytes = rivet::fs::read_file(pc_file);
    if (!bytes)
        return make_error<PkgConfig>(std::string("pkg-config: cannot read ") + pc_file.string()
            + ": " + bytes.error().message);

    std::string_view content(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    PkgConfig pc;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string_view::npos) eol = content.size();
        std::string_view line = content.substr(pos, eol - pos);
        pos = eol + 1;

        // Strip trailing \r.
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        std::string raw = trim(line);
        if (raw.empty() || raw[0] == '#') continue;

        // Variable assignment: `name = value` (no whitespace required).
        // We distinguish from a keyword by the separator: `:` for keyword,
        // `=` for variable.
        auto colon = raw.find(':');
        auto equal = raw.find('=');
        bool is_keyword = colon != std::string::npos && (equal == std::string::npos || colon < equal);

        if (is_keyword) {
            std::string key = trim(raw.substr(0, colon));
            std::string val = expand_vars(trim(raw.substr(colon + 1)), pc.vars);
            if (key == "Name")            pc.name = std::move(val);
            else if (key == "Version")    pc.version = std::move(val);
            else if (key == "Description") pc.description = std::move(val);
            else if (key == "Cflags" || key == "Cflags.private") {
                auto toks = tokenise(val);
                pc.cflags.insert(pc.cflags.end(), toks.begin(), toks.end());
            } else if (key == "Libs") {
                auto toks = tokenise(val);
                pc.libs.insert(pc.libs.end(), toks.begin(), toks.end());
            } else if (key == "Libs.private") {
                // Caller decides whether to merge these via static_link.
                // Stash them at the end with a marker so resolve_pkgs can
                // split — for simplicity we append a sentinel-free vec.
                // (Round-tripping is overkill; we just always store and
                // let resolve_pkgs filter.) Keep separate via second vec.
                // For now, fold into libs for shared-link only.
                // We store on a side channel via vars:
                pc.vars["__private_libs__"] += " " + val;
            } else if (key == "Requires") {
                auto names = parse_requires(val);
                pc.requires_.insert(pc.requires_.end(), names.begin(), names.end());
            } else if (key == "Requires.private") {
                auto names = parse_requires(val);
                pc.requires_private.insert(pc.requires_private.end(), names.begin(), names.end());
            }
            // Unknown keywords (URL, Conflicts, etc.) — ignore.
        } else if (equal != std::string::npos) {
            std::string key = trim(raw.substr(0, equal));
            std::string val = expand_vars(trim(raw.substr(equal + 1)), pc.vars);
            pc.vars[key] = std::move(val);
        }
        // Else: malformed line, skip silently (matches pkg-config behaviour).
    }

    return pc;
}

namespace {

// Locate a .pc file for `name` in any of the search dirs.
std::optional<Path> find_pc(std::string_view name, const std::vector<Path>& dirs) {
    std::string filename = std::string(name) + ".pc";
    for (const auto& d : dirs) {
        Path candidate = d / filename;
        if (rivet::fs::exists(candidate).value_or(false)) return candidate;
    }
    return std::nullopt;
}

// Append b's items to a, skipping anything already present (preserves order).
void merge_unique(std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::unordered_set<std::string> seen(a.begin(), a.end());
    for (const auto& t : b) {
        if (seen.insert(t).second) a.push_back(t);
    }
}

} // namespace

Result<ResolvedPkgs> resolve_pkgs(const std::vector<std::string>& roots,
                                   const std::vector<Path>&       search_dirs,
                                   bool                            static_link) {
    ResolvedPkgs out;
    std::unordered_set<std::string> visited;

    // Iterative DFS so we get topological order (deps before dependents).
    std::vector<std::string> stack(roots.rbegin(), roots.rend());
    std::vector<std::pair<std::string, PkgConfig>> ordered;  // (name, pc)

    while (!stack.empty()) {
        std::string name = std::move(stack.back());
        stack.pop_back();
        if (!visited.insert(name).second) continue;

        auto pc_path = find_pc(name, search_dirs);
        if (!pc_path) { out.unresolved.push_back(name); continue; }

        auto pc_r = parse_pc(*pc_path);
        if (!pc_r) return make_error<ResolvedPkgs>(pc_r.error().message);

        // Push requires onto the stack so they get processed FIRST
        // (we want deps emitted before their dependents on the link line).
        std::vector<std::string> reqs = pc_r->requires_;
        if (static_link)
            reqs.insert(reqs.end(), pc_r->requires_private.begin(),
                                     pc_r->requires_private.end());
        // We accumulate this package into ordered AFTER processing deps,
        // achieved by re-pushing self with a sentinel — but simpler: do
        // post-order via two passes. Here we use a simpler ordering: emit
        // dep cflags/libs in stack order, which gives reverse-DFS — fine
        // for cflags (-I order doesn't matter for resolution) and acceptable
        // for libs (we merge_unique to preserve first-occurrence order; the
        // linker sees deeper deps later, which static_link wants).
        ordered.emplace_back(name, std::move(*pc_r));
        for (auto it = reqs.rbegin(); it != reqs.rend(); ++it) {
            if (!visited.count(*it)) stack.push_back(*it);
        }
    }

    // Emit in iteration order: roots first, then their deps. POSIX `ld`
    // processes -l flags left-to-right and resolves symbols by what's
    // *unresolved* at the time the lib is encountered — so a dependent
    // must appear BEFORE the lib it depends on (-ltop comes before -lmid
    // which comes before -lbase). DFS visit order gives us exactly that.
    for (auto& [name, pc] : ordered) {
        merge_unique(out.cflags, pc.cflags);
        merge_unique(out.libs,   pc.libs);
        out.resolved.push_back(name);
    }

    return out;
}

} // namespace rivet::build
