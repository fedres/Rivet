// runtime/package/manifest.cpp — rivet.toml parser
//
// Parses TOML using toml++ (vendored at vendor/tomlplusplus/toml.hpp).
// Until the vendor directory is populated, a minimal hand-rolled parser
// handles the subset of TOML required for rivet.toml.
#include "manifest.hpp"
#include "../../platform/interface/fs.hpp"
#include "../../platform/interface/env.hpp"

#include <algorithm>
#include <format>
#include <sstream>

// ─── toml++ availability guard ────────────────────────────────────────────────
#if __has_include("../../vendor/tomlplusplus/toml.hpp")
#  include "../../vendor/tomlplusplus/toml.hpp"
#  define RIVET_HAVE_TOMLPP 1
#else
#  define RIVET_HAVE_TOMLPP 0
#endif

namespace rivet::pkg {

#if !RIVET_HAVE_TOMLPP
// ─── Minimal TOML stub parser ─────────────────────────────────────────────────
// Handles only what is strictly required for rivet.toml — used only when the
// vendored toml++ is absent (RIVET_VENDOR_FETCH=OFF build path).
namespace detail {

using FlatMap = std::unordered_map<std::string, std::string>;

static FlatMap parse_flat(std::string_view text) {
    FlatMap result;
    std::string section;
    std::istringstream ss{std::string(text)};
    std::string line;

    while (std::getline(ss, line)) {
        // Strip comments and whitespace.
        auto comment = line.find('#');
        if (comment != std::string::npos) line = line.substr(0, comment);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))  line.pop_back();
        if (line.empty()) continue;

        // Section headers: [name] or [[array]]
        if (line.front() == '[') {
            auto close = line.find(']');
            if (close == std::string::npos) continue;
            bool arr = (line.size() > 1 && line[1] == '[');
            auto inner = arr ? line.substr(2, line.rfind(']') - 2)
                             : line.substr(1, close - 1);
            // Trim.
            while (!inner.empty() && std::isspace(static_cast<unsigned char>(inner.front()))) inner.erase(inner.begin());
            while (!inner.empty() && std::isspace(static_cast<unsigned char>(inner.back()))) inner.pop_back();
            section = inner;
            continue;
        }

        // Key = value.
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);

        // Trim key.
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(key.begin());
        // Trim val.
        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) val.pop_back();

        // Strip quotes from string values.
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);

        auto full_key = section.empty() ? key : section + "." + key;
        result[full_key] = val;
    }
    return result;
}

static std::string get(const FlatMap& m, const std::string& key,
                        const std::string& fallback = {}) {
    auto it = m.find(key);
    return it != m.end() ? it->second : fallback;
}

} // namespace detail
#endif // !RIVET_HAVE_TOMLPP

// ─── parse_manifest() ─────────────────────────────────────────────────────────

Result<Manifest> parse_manifest(const Path& path) {
    auto data = rivet::fs::read_file(path);
    if (!data) return propagate<Manifest>(data);

    std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());

#if RIVET_HAVE_TOMLPP
    // Full toml++ parse path.
    toml::table tbl;
    try {
        tbl = toml::parse(text);
    } catch (const toml::parse_error& e) {
        return make_error<Manifest>(std::format("rivet.toml parse error: {}", e.what()));
    }

    Manifest m;
    m.root_dir = path.parent_path();

    auto str_of = [](const toml::table* t, const char* k,
                     const std::string& def = {}) -> std::string {
        if (!t) return def;
        if (auto* v = t->get_as<std::string>(k)) return v->get();
        return def;
    };

    if (auto* pkg = tbl.get_as<toml::table>("package")) {
        m.name        = str_of(pkg, "name");
        m.version     = str_of(pkg, "version");
        m.description = str_of(pkg, "description");
        m.license     = str_of(pkg, "license");
        m.homepage    = str_of(pkg, "homepage");
        m.repository  = str_of(pkg, "repository");
        m.readme      = str_of(pkg, "readme");
        if (auto* a = pkg->get_as<toml::array>("authors")) {
            for (const auto& v : *a)
                if (auto s = v.value<std::string>()) m.authors.push_back(*s);
        }
        if (auto* a = pkg->get_as<toml::array>("keywords")) {
            for (const auto& v : *a)
                if (auto s = v.value<std::string>()) m.keywords.push_back(*s);
        }
    }

    if (auto* build = tbl.get_as<toml::table>("build")) {
        auto sys = str_of(build, "system", "rivet");
        if      (sys == "cmake")    m.build.system = BuildSystem::CMake;
        else if (sys == "make")     m.build.system = BuildSystem::Make;
        else if (sys == "autoconf") m.build.system = BuildSystem::Autoconf;
        else if (sys == "meson")    m.build.system = BuildSystem::Meson;
        else if (sys == "custom")   m.build.system = BuildSystem::Custom;
        else                        m.build.system = BuildSystem::Rivet;
        m.build.cxx_std = str_of(build, "cxx_std", "c++23");
        if (auto* a = build->get_as<toml::array>("targets")) {
            for (const auto& v : *a)
                if (auto s = v.value<std::string>()) m.build.targets.push_back(*s);
        }
        if (auto* a = build->get_as<toml::array>("extra_flags")) {
            for (const auto& v : *a)
                if (auto s = v.value<std::string>()) m.build.extra_flags.push_back(*s);
        }
    }

    auto parse_deps = [&](const toml::table* deps, auto& dest) {
        if (!deps) return;
        for (const auto& [k, v] : *deps) {
            DepSpec spec;
            std::string name{k.str()};
            if (auto s = v.value<std::string>()) {
                // Shorthand: dep = "1.2"
                spec.kind    = DepKind::Registry;
                spec.version = *s;
            } else if (auto* t = v.as_table()) {
                spec.version = str_of(t, "version");
                if (auto p = str_of(t, "path"); !p.empty()) {
                    spec.kind = DepKind::Path;
                    spec.local_path = Path{p};
                } else if (auto g = str_of(t, "git"); !g.empty()) {
                    spec.kind    = DepKind::Git;
                    spec.git_url = g;
                    spec.git_ref = str_of(t, "rev");
                    if (spec.git_ref.empty()) spec.git_ref = str_of(t, "branch");
                    if (spec.git_ref.empty()) spec.git_ref = str_of(t, "tag");
                } else {
                    spec.kind = DepKind::Registry;
                }
                if (auto* fa = t->get_as<toml::array>("features")) {
                    for (const auto& fv : *fa)
                        if (auto fs = fv.value<std::string>()) spec.features.push_back(*fs);
                }
                if (auto b = t->get_as<bool>("static")) spec.static_link = b->get();
                if (auto b = t->get_as<bool>("workspace")) spec.inherit_workspace = b->get();
            }
            dest[name] = std::move(spec);
        }
    };
    parse_deps(tbl.get_as<toml::table>("dependencies"),     m.dependencies);
    parse_deps(tbl.get_as<toml::table>("dev-dependencies"), m.dev_dependencies);

    // [workspace] — optional. Cargo-style monorepo declaration.
    if (auto* ws = tbl.get_as<toml::table>("workspace")) {
        WorkspaceSection w;
        if (auto* a = ws->get_as<toml::array>("members")) {
            for (const auto& v : *a)
                if (auto s = v.value<std::string>()) w.members.push_back(*s);
        }
        if (auto* a = ws->get_as<toml::array>("exclude")) {
            for (const auto& v : *a)
                if (auto s = v.value<std::string>()) w.exclude.push_back(*s);
        }
        // [workspace.dependencies] uses the same shape as [dependencies].
        parse_deps(ws->get_as<toml::table>("dependencies"), w.dependencies);
        m.workspace = std::move(w);
    }

    if (auto* scripts = tbl.get_as<toml::table>("scripts")) {
        for (const auto& [k, v] : *scripts)
            if (auto s = v.value<std::string>())
                m.scripts[std::string{k.str()}] = *s;
    }

    // ─── [[lib]], [[bin]], [[test]], [[vendor]] — multi-target schema ──────
    //
    // Each is an array-of-tables with the same shape:
    //   name              required for libs/bins/tests; defaults to basename(path) for bins
    //   path              entry-point source (bins/tests only)
    //   sources           explicit list of source files (libs/vendor; optional for bins)
    //   include_dirs      compile -I paths, evaluated relative to root_dir
    //   depends_on        list of intra-manifest target names
    //   compile_flags     extra clang flags for every source in this target
    //   link_libs         extra raw link tokens (-l..., framework names, etc.)
    //   defines           list of "FOO=1" tokens → emitted as -DFOO=1
    //   per_source_flags  table mapping path/glob → list of clang flags
    //   cfg               array of per-platform overrides — see CfgPredicate
    auto parse_string_array = [](const toml::table& t, const char* key,
                                  std::vector<std::string>& out) {
        if (auto* a = t.get_as<toml::array>(key)) {
            for (const auto& v : *a)
                if (auto s = v.value<std::string>()) out.push_back(*s);
        }
    };
    auto parse_target = [&](const toml::table& t, TargetKind kind) -> Target {
        Target tgt;
        tgt.kind = kind;
        tgt.name = str_of(&t, "name");
        tgt.path = str_of(&t, "path");
        if (tgt.name.empty() && !tgt.path.empty()) {
            auto stem = Path{tgt.path}.stem().string();
            if (!stem.empty()) tgt.name = stem;
        }
        parse_string_array(t, "sources",       tgt.sources);
        parse_string_array(t, "include_dirs",  tgt.include_dirs);
        parse_string_array(t, "depends_on",    tgt.depends_on);
        parse_string_array(t, "compile_flags", tgt.compile_flags);
        parse_string_array(t, "link_libs",     tgt.link_libs);
        parse_string_array(t, "defines",       tgt.defines);

        if (auto* psf = t.get_as<toml::table>("per_source_flags")) {
            for (const auto& [k, v] : *psf) {
                std::vector<std::string> flags;
                if (auto* arr = v.as_array()) {
                    for (const auto& fv : *arr)
                        if (auto s = fv.value<std::string>()) flags.push_back(*s);
                } else if (auto s = v.value<std::string>()) {
                    flags.push_back(*s);
                }
                tgt.per_source_flags[std::string{k.str()}] = std::move(flags);
            }
        }

        // [target.<name>.cfg] is conceptually a list of override tables.
        // We support both inline arrays and the more readable per-section
        // form via `[[<kind>.cfg]]` style.
        if (auto* cfgs = t.get_as<toml::array>("cfg")) {
            for (const auto& cv : *cfgs) {
                auto* ct = cv.as_table();
                if (!ct) continue;
                TargetCfgOverride ov;
                if (auto os_str = str_of(ct, "os"); !os_str.empty()) ov.cfg.os = os_str;
                parse_string_array(*ct, "sources",       ov.extra_sources);
                parse_string_array(*ct, "link_libs",     ov.extra_link_libs);
                parse_string_array(*ct, "compile_flags", ov.extra_compile_flags);
                tgt.cfg_overrides.push_back(std::move(ov));
            }
        }
        return tgt;
    };
    auto parse_target_array = [&](const char* key, TargetKind kind) {
        if (auto* arr = tbl.get_as<toml::array>(key)) {
            for (const auto& v : *arr) {
                if (auto* t = v.as_table())
                    m.targets.push_back(parse_target(*t, kind));
            }
        }
    };
    parse_target_array("lib",    TargetKind::Lib);
    parse_target_array("bin",    TargetKind::Bin);
    parse_target_array("test",   TargetKind::Test);
    parse_target_array("vendor", TargetKind::Vendor);

    if (auto* profiles = tbl.get_as<toml::table>("profiles")) {
        for (const auto& [k, v] : *profiles) {
            auto* prof_tbl = v.as_table();
            if (!prof_tbl) continue;
            Profile prof;
            prof.name = std::string{k.str()};
            if (auto opt = prof_tbl->get_as<int64_t>("opt_level"))
                prof.opt_level = static_cast<int>(opt->get());
            if (auto d = prof_tbl->get_as<bool>("debug")) prof.debug = d->get();
            if (auto l = prof_tbl->get_as<bool>("lto"))   prof.lto   = l->get();
            if (auto* a = prof_tbl->get_as<toml::array>("sanitizers")) {
                for (const auto& sv : *a)
                    if (auto s = sv.value<std::string>()) prof.sanitizers.push_back(*s);
            }
            if (auto* a = prof_tbl->get_as<toml::array>("extra_flags")) {
                for (const auto& sv : *a)
                    if (auto s = sv.value<std::string>()) prof.extra_flags.push_back(*s);
            }
            m.profiles[prof.name] = std::move(prof);
        }
    }

    return m;

#else
    // Minimal stub parse path — covers the common happy path.
    auto flat = detail::parse_flat(text);

    Manifest m;
    m.root_dir    = path.parent_path();
    m.name        = detail::get(flat, "package.name");
    m.version     = detail::get(flat, "package.version");
    m.description = detail::get(flat, "package.description");
    m.license     = detail::get(flat, "package.license");
    m.homepage    = detail::get(flat, "package.homepage");
    m.repository  = detail::get(flat, "package.repository");

    // Build section.
    auto build_sys = detail::get(flat, "build.system", "rivet");
    if      (build_sys == "cmake")    m.build.system = BuildSystem::CMake;
    else if (build_sys == "make")     m.build.system = BuildSystem::Make;
    else if (build_sys == "autoconf") m.build.system = BuildSystem::Autoconf;
    else if (build_sys == "meson")    m.build.system = BuildSystem::Meson;
    else                              m.build.system = BuildSystem::Rivet;

    m.build.cxx_std = detail::get(flat, "build.cxx_std", "c++23");

    // [dependencies]
    for (const auto& [k, v] : flat) {
        const std::string prefix = "dependencies.";
        if (k.size() > prefix.size() && k.compare(0, prefix.size(), prefix) == 0) {
            std::string dep_name = k.substr(prefix.size());
            rivet::pkg::DepSpec spec;
            spec.kind    = rivet::pkg::DepKind::Registry;
            spec.version = v;
            m.dependencies[dep_name] = std::move(spec);
        }
    }

    // [profiles.<name>] — e.g. [profiles.asan]
    for (const auto& [k, v] : flat) {
        const std::string prefix = "profiles.";
        if (k.size() <= prefix.size() || k.compare(0, prefix.size(), prefix) != 0) continue;
        std::string rest = k.substr(prefix.size());
        auto dot = rest.find('.');
        if (dot == std::string::npos) continue;
        std::string prof_name  = rest.substr(0, dot);
        std::string field_name = rest.substr(dot + 1);

        auto& prof = m.profiles[prof_name];
        prof.name  = prof_name;

        if (field_name == "opt_level") {
            try { prof.opt_level = std::stoi(v); } catch (...) {}
        } else if (field_name == "debug") {
            prof.debug = (v == "true");
        } else if (field_name == "lto") {
            prof.lto = (v == "true");
        } else if (field_name == "sanitizers") {
            // Parse inline TOML array: ["address", "undefined"]
            std::string arr = v;
            if (!arr.empty() && arr.front() == '[') arr.erase(arr.begin());
            if (!arr.empty() && arr.back()  == ']') arr.pop_back();
            std::istringstream arr_ss(arr);
            std::string item;
            while (std::getline(arr_ss, item, ',')) {
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back())))  item.pop_back();
                if (item.size() >= 2 && item.front() == '"' && item.back() == '"')
                    item = item.substr(1, item.size() - 2);
                if (!item.empty()) prof.sanitizers.push_back(item);
            }
        }
    }
    return m;
#endif
}

// ─── find_and_parse() ─────────────────────────────────────────────────────────

// Walk up from `member_dir` looking for a parent rivet.toml that declares
// a [workspace] containing `member_dir` in its members[]. Returns the
// workspace root dir + parsed root manifest if found.
namespace {

struct WorkspaceRootInfo {
    Path     root_dir;
    Manifest root_manifest;
};

std::optional<WorkspaceRootInfo>
find_workspace_root_for(const Path& member_dir) {
    auto norm = [](const Path& p) {
        // weakly_canonical may touch the filesystem; fall back to
        // lexically_normal on failure so missing-dir cases still work.
        std::error_code ec;
        Path c = std::filesystem::weakly_canonical(p, ec);
        if (ec) return p.lexically_normal();
        return c;
    };
    Path target = norm(member_dir);

    Path dir = member_dir.parent_path();
    while (true) {
        Path candidate = dir / "rivet.toml";
        if (rivet::fs::exists(candidate).value_or(false)) {
            auto r = parse_manifest(candidate);
            if (r && r->workspace.has_value()) {
                for (const auto& m : r->workspace->members) {
                    if (norm(dir / m) == target) {
                        return WorkspaceRootInfo{dir, std::move(*r)};
                    }
                }
            }
        }
        Path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return std::nullopt;
}

// Substitute workspace-inherited deps in `m` against `ws`'s
// [workspace.dependencies]. Returns an error if any inherited dep is not
// declared at the workspace root (matches cargo's strict mode).
Result<void> apply_workspace_inheritance(Manifest& m, const Manifest& ws) {
    if (!ws.workspace.has_value()) return {};
    const auto& wsdeps = ws.workspace->dependencies;
    for (auto& [name, spec] : m.dependencies) {
        if (!spec.inherit_workspace) continue;
        auto it = wsdeps.find(name);
        if (it == wsdeps.end()) {
            return make_error<void>(std::format(
                "dependency '{}' uses `workspace = true` but the workspace "
                "root has no [workspace.dependencies].{} entry", name, name));
        }
        // Keep inherit_workspace true so serialize() round-trips the form.
        // Pull every other field from the workspace pin.
        bool keep_flag = true;
        DepSpec merged = it->second;
        if (!spec.features.empty()) {
            // Member-level features extend the workspace's features.
            for (const auto& f : spec.features) merged.features.push_back(f);
        }
        merged.inherit_workspace = keep_flag;
        spec = std::move(merged);
    }
    return {};
}

} // namespace

Result<Manifest> find_and_parse(const Path& start_dir) {
    Path dir = start_dir;

    while (true) {
        auto candidate = dir / "rivet.toml";
        if (rivet::fs::exists(candidate).value_or(false)) {
            auto r = parse_manifest(candidate);
            if (!r) return r;

            // If this manifest is itself a workspace root (has [workspace],
            // no [package]) or a standalone package, no inheritance to do.
            // Otherwise walk up to find a workspace that contains us.
            if (auto ws = find_workspace_root_for(r->root_dir)) {
                r->workspace_root = ws->root_dir;
                if (auto inh = apply_workspace_inheritance(*r, ws->root_manifest);
                    !inh) {
                    return make_error<Manifest>(inh.error().message);
                }
            }
            return r;
        }

        auto parent = dir.parent_path();
        if (parent == dir)
            return make_error<Manifest>("rivet.toml not found (searched up to filesystem root)");
        dir = parent;
    }
}

Path lockfile_path_for(const Manifest& m) {
    return (m.workspace_root.empty() ? m.root_dir : m.workspace_root) / "rivet.lock";
}

// ─── validate() ──────────────────────────────────────────────────────────────

Result<void> validate(const Manifest& m) {
    if (m.name.empty())
        return make_error<void>("manifest missing required field: [package].name");
    if (m.version.empty())
        return make_error<void>("manifest missing required field: [package].version");

    // Basic semver: must contain at least one dot.
    if (m.version.find('.') == std::string::npos)
        return make_error<void>(std::format(
            "invalid version '{}': must be SemVer (e.g. 0.1.0)", m.version));

    // Name must be [a-z0-9_-] only.
    for (char c : m.name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return make_error<void>(std::format(
                "invalid package name '{}': only a-z, 0-9, _, - allowed", m.name));
        }
    }
    return {};
}

// ─── serialize() ─────────────────────────────────────────────────────────────

std::string serialize(const Manifest& m) {
    std::string out;
    out += "[package]\n";
    out += std::format("name        = \"{}\"\n", m.name);
    out += std::format("version     = \"{}\"\n", m.version);
    if (!m.description.empty())
        out += std::format("description = \"{}\"\n", m.description);
    if (!m.license.empty())
        out += std::format("license     = \"{}\"\n", m.license);
    out += "\n[build]\n";
    // Preserve the build-system marker — losing this on a re-serialize would
    // silently flip a cmake-drive project back to rivet's native build engine.
    const char* sys_str = nullptr;
    switch (m.build.system) {
        case BuildSystem::Rivet:    sys_str = nullptr;     break;
        case BuildSystem::CMake:    sys_str = "cmake";     break;
        case BuildSystem::Make:     sys_str = "make";      break;
        case BuildSystem::Autoconf: sys_str = "autoconf";  break;
        case BuildSystem::Meson:    sys_str = "meson";     break;
        case BuildSystem::Custom:   sys_str = "custom";    break;
    }
    if (sys_str) out += std::format("system  = \"{}\"\n", sys_str);
    out += std::format("cxx_std = \"{}\"\n", m.build.cxx_std);
    if (!m.dependencies.empty()) {
        out += "\n[dependencies]\n";
        for (const auto& [name, spec] : m.dependencies) {
            if (spec.inherit_workspace) {
                // Round-trip the cargo-style inheritance form. The version
                // field has been substituted by find_and_parse but we keep
                // the inherit marker so re-serialize is faithful.
                out += std::format("{} = {{ workspace = true }}\n", name);
            } else {
                out += std::format("{} = \"{}\"\n", name, spec.version);
            }
        }
    }
    return out;
}

} // namespace rivet::pkg
