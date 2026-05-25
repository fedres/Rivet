// platform/macos/env.cpp — macOS environment backend
#include "../interface/env.hpp"
#include "../interface/result.hpp"

#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

extern char** environ;

namespace rivet::env {

std::optional<std::string> get(std::string_view key) {
    const char* v = ::getenv(std::string(key).c_str());
    return v ? std::optional{std::string(v)} : std::nullopt;
}
std::string get_or(std::string_view key, std::string_view fallback) {
    return get(key).value_or(std::string(fallback));
}
void set(std::string_view key, std::string_view value) {
    ::setenv(std::string(key).c_str(), std::string(value).c_str(), 1);
}
void unset(std::string_view key) { ::unsetenv(std::string(key).c_str()); }
std::unordered_map<std::string, std::string> snapshot() {
    std::unordered_map<std::string, std::string> r;
    for (char** e = environ; *e; ++e) {
        std::string_view entry{*e};
        auto eq = entry.find('=');
        if (eq == std::string_view::npos) continue;
        r.emplace(entry.substr(0, eq), entry.substr(eq + 1));
    }
    return r;
}

Result<Path> home_dir() {
    if (auto h = get("HOME"); h) return Path{*h};
    struct passwd* pw = ::getpwuid(::getuid());
    if (!pw) return make_error<Path>("home_dir: cannot determine home");
    return Path{pw->pw_dir};
}
Result<Path> rivet_home() {
    if (auto h = get("RIVET_HOME"); h) return Path{*h};
    auto home = home_dir();
    if (!home) return home;
    return *home / ".rivet";
}
Result<Path> cache_dir() {
    auto home = home_dir();
    if (!home) return home;
    return *home / "Library" / "Caches" / "rivet";
}
Result<Path> temp_dir() {
    if (auto t = get("TMPDIR"); t) return Path{*t};
    return Path{"/tmp"};
}

Arch arch() {
    struct utsname u{}; ::uname(&u);
    std::string_view m{u.machine};
    if (m == "x86_64") return Arch::X64;
    if (m == "arm64")  return Arch::Arm64;
    return Arch::Unknown;
}
Os   os()   { return Os::macOS; }
LibC libc() { return LibC::Unknown; } // macOS uses libSystem, not glibc/musl

std::string host_triple() {
    switch (arch()) {
        case Arch::X64:   return "x86_64-apple-macos10.15";
        case Arch::Arm64: return "arm64-apple-macos11";
        default:          return "unknown-apple-macos";
    }
}

} // namespace rivet::env
