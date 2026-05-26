// runtime/package/source.cpp — PackageSource registry + SemVer helpers
#include "source.hpp"

#include <cctype>
#include <charconv>
#include <format>
#include <functional>

namespace rivet::pkg {

// ─── SemVer ──────────────────────────────────────────────────────────────────

std::string SemVer::to_string() const {
    auto s = std::format("{}.{}.{}", major, minor, patch);
    if (!prerelease.empty()) s += "-" + prerelease;
    return s;
}

Result<SemVer> parse_semver(std::string_view s) {
    // Strip leading 'v' if present.
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V'))
        s.remove_prefix(1);

    auto take_int = [](std::string_view& sv, int& out) -> bool {
        if (sv.empty() || !std::isdigit(static_cast<unsigned char>(sv.front())))
            return false;
        const char* begin = sv.data();
        const char* end   = sv.data() + sv.size();
        auto [ptr, ec] = std::from_chars(begin, end, out);
        if (ec != std::errc{}) return false;
        sv.remove_prefix(ptr - begin);
        return true;
    };

    SemVer v;
    if (!take_int(s, v.major))
        return make_error<SemVer>(std::string("semver: expected major in '") + std::string(s) + "'");
    // .minor.patch are optional — vcpkg often uses "1.2" or just "1".
    if (!s.empty() && s.front() == '.') {
        s.remove_prefix(1);
        if (!take_int(s, v.minor))
            return make_error<SemVer>("semver: expected minor");
    }
    if (!s.empty() && s.front() == '.') {
        s.remove_prefix(1);
        if (!take_int(s, v.patch))
            return make_error<SemVer>("semver: expected patch");
    }
    if (!s.empty() && s.front() == '-') {
        s.remove_prefix(1);
        auto end = s.find_first_of("+ ");
        v.prerelease = std::string(end == std::string_view::npos ? s : s.substr(0, end));
    }
    return v;
}

namespace {

// Strip whitespace.
std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// Match a single sub-constraint: "^1.2", ">=1.0", "<3", "=2.3.4", "1.2.3", "*".
bool match_one(std::string_view c, const SemVer& v) {
    c = trim(c);
    if (c.empty() || c == "*" || c == "any") return true;

    auto comp = [&](std::string_view rest, auto pred) -> bool {
        auto ver = parse_semver(trim(rest));
        if (!ver) return false;
        return pred(v, *ver);
    };

    if (c.starts_with(">=")) return comp(c.substr(2), std::greater_equal<SemVer>{});
    if (c.starts_with("<=")) return comp(c.substr(2), std::less_equal<SemVer>{});
    if (c.starts_with(">"))  return comp(c.substr(1), std::greater<SemVer>{});
    if (c.starts_with("<"))  return comp(c.substr(1), std::less<SemVer>{});
    if (c.starts_with("="))  return comp(c.substr(1), std::equal_to<SemVer>{});

    if (c.starts_with("^")) {
        // Caret: compatible up to next major. ^1.2.3 → >=1.2.3, <2.0.0
        auto ver = parse_semver(c.substr(1));
        if (!ver) return false;
        if (v < *ver) return false;
        SemVer upper = *ver;
        if (upper.major > 0)       { upper.major += 1; upper.minor = 0; upper.patch = 0; }
        else if (upper.minor > 0)  { upper.minor += 1; upper.patch = 0; }
        else                       { upper.patch += 1; }
        upper.prerelease.clear();
        return v < upper;
    }
    if (c.starts_with("~")) {
        // Tilde: patch-level. ~1.2.3 → >=1.2.3, <1.3.0
        auto ver = parse_semver(c.substr(1));
        if (!ver) return false;
        if (v < *ver) return false;
        SemVer upper = *ver;
        upper.minor += 1;
        upper.patch  = 0;
        upper.prerelease.clear();
        return v < upper;
    }

    // Bare version: exact match on what's specified. "1.2" matches 1.2.x.
    auto ver = parse_semver(c);
    if (!ver) return false;
    if (v.major != ver->major) return false;
    // If user wrote "1.2", we match any patch under 1.2.
    if (c.find('.') == std::string_view::npos) return true;
    if (v.minor != ver->minor) return false;
    auto second_dot = c.find('.', c.find('.') + 1);
    if (second_dot == std::string_view::npos) return true;
    return v.patch == ver->patch;
}

} // namespace

bool satisfies(std::string_view constraint, const SemVer& v) {
    constraint = trim(constraint);
    if (constraint.empty()) return true;

    // Space-separated AND list ("'>=1 <3'") — all must match.
    auto sp = constraint.find(' ');
    while (sp != std::string_view::npos) {
        if (!match_one(constraint.substr(0, sp), v)) return false;
        constraint.remove_prefix(sp + 1);
        constraint = trim(constraint);
        sp = constraint.find(' ');
    }
    return match_one(constraint, v);
}

// ─── SourceRegistry ──────────────────────────────────────────────────────────

void SourceRegistry::add(std::unique_ptr<PackageSource> src) {
    sources_.push_back(std::move(src));
}

PackageSource* SourceRegistry::find(std::string_view id) const {
    for (const auto& s : sources_)
        if (s->id() == id) return s.get();
    return nullptr;
}

Result<ResolvedPackage> SourceRegistry::resolve(const PackageRef& ref) const {
    if (ref.source_id) {
        auto* s = find(*ref.source_id);
        if (!s) return make_error<ResolvedPackage>(
            std::string("source '") + *ref.source_id + "' not configured");
        return s->resolve(ref);
    }
    std::string last_err;
    for (const auto& s : sources_) {
        if (!s->handles(ref)) continue;
        auto r = s->resolve(ref);
        if (r) return r;
        last_err = r.error().message;
    }
    return make_error<ResolvedPackage>(
        std::string("no source could resolve '") + ref.name +
        "': " + (last_err.empty() ? "no candidate sources" : last_err));
}

} // namespace rivet::pkg
