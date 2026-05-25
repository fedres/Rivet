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

// ─── Minimal TOML stub parser ─────────────────────────────────────────────────
// Handles only what is strictly required for rivet.toml — a proper toml++
// implementation replaces this entirely once the vendor is populated.
namespace detail {

struct TomlValue {
    enum class Kind { String, Integer, Bool, Array, Table };

    Kind kind = Kind::String;
    std::string                             string_val;
    int64_t                                 int_val = 0;
    bool                                    bool_val = false;
    std::vector<TomlValue>                  array_val;
    std::unordered_map<std::string, TomlValue> table_val;
};

// Very small TOML key = string-value / array parser (sufficient for our schema).
// Returns a flat key→value map (dotted keys flattened by section prefix).
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

    if (auto* pkg = tbl.get_as<toml::table>("package")) {
        auto str = [&](const char* k, const std::string& def = {}) -> std::string {
            if (auto* v = pkg->get_as<std::string>(k)) return v->get();
            return def;
        };
        m.name        = str("name");
        m.version     = str("version");
        m.description = str("description");
        m.license     = str("license");
        m.homepage    = str("homepage");
        m.repository  = str("repository");
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
    return m;
#endif
}

// ─── find_and_parse() ─────────────────────────────────────────────────────────

Result<Manifest> find_and_parse(const Path& start_dir) {
    Path dir = start_dir;

    while (true) {
        auto candidate = dir / "rivet.toml";
        if (rivet::fs::exists(candidate).value_or(false)) {
            return parse_manifest(candidate);
        }

        auto parent = dir.parent_path();
        if (parent == dir)
            return make_error<Manifest>("rivet.toml not found (searched up to filesystem root)");
        dir = parent;
    }
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
    out += std::format("cxx_std = \"{}\"\n", m.build.cxx_std);
    if (!m.dependencies.empty()) {
        out += "\n[dependencies]\n";
        for (const auto& [name, spec] : m.dependencies)
            out += std::format("{} = \"{}\"\n", name, spec.version);
    }
    return out;
}

} // namespace rivet::pkg
