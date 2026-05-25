// platform/windows/env.cpp — Windows environment backend
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include "../interface/env.hpp"
#include "../interface/result.hpp"

namespace rivet::env {

namespace {
    std::string wide_to_utf8(const wchar_t* w) {
        if (!w) return {};
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string s(n - 1, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }
    std::wstring utf8_to_wide(std::string_view s) {
        if (s.empty()) return {};
        int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, 0);
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }
}

std::optional<std::string> get(std::string_view key) {
    auto wkey = utf8_to_wide(key);
    DWORD sz = ::GetEnvironmentVariableW(wkey.c_str(), nullptr, 0);
    if (sz == 0) return std::nullopt;
    std::wstring val(sz - 1, 0);
    ::GetEnvironmentVariableW(wkey.c_str(), val.data(), sz);
    return wide_to_utf8(val.c_str());
}

std::string get_or(std::string_view key, std::string_view fallback) {
    return get(key).value_or(std::string(fallback));
}

void set(std::string_view key, std::string_view value) {
    ::SetEnvironmentVariableW(utf8_to_wide(key).c_str(), utf8_to_wide(value).c_str());
}

void unset(std::string_view key) {
    ::SetEnvironmentVariableW(utf8_to_wide(key).c_str(), nullptr);
}

std::unordered_map<std::string, std::string> snapshot() {
    std::unordered_map<std::string, std::string> r;
    wchar_t* block = ::GetEnvironmentStringsW();
    if (!block) return r;
    for (wchar_t* p = block; *p; ) {
        std::wstring entry{p};
        auto eq = entry.find(L'=');
        if (eq != std::wstring::npos && eq > 0) {
            r.emplace(wide_to_utf8(entry.substr(0, eq).c_str()),
                      wide_to_utf8(entry.substr(eq + 1).c_str()));
        }
        p += entry.size() + 1;
    }
    ::FreeEnvironmentStringsW(block);
    return r;
}

Result<Path> home_dir() {
    if (auto h = get("USERPROFILE"); h) return Path{*h};
    wchar_t buf[MAX_PATH]{};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, buf)))
        return Path{buf};
    return make_error<Path>("home_dir: cannot determine home directory");
}

Result<Path> rivet_home() {
    if (auto h = get("RIVET_HOME"); h) return Path{*h};
    if (auto h = get("APPDATA"); h) return Path{*h} / "rivet";
    auto home = home_dir();
    if (!home) return home;
    return *home / "AppData" / "Roaming" / "rivet";
}

Result<Path> cache_dir() {
    if (auto lad = get("LOCALAPPDATA"); lad) return Path{*lad} / "rivet" / "cache";
    auto home = home_dir();
    if (!home) return home;
    return *home / "AppData" / "Local" / "rivet" / "cache";
}

Result<Path> temp_dir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = ::GetTempPathW(MAX_PATH, buf);
    if (n == 0) return make_error<Path>("GetTempPathW");
    return Path{buf};
}

Arch arch() {
    SYSTEM_INFO si{}; ::GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return Arch::X64;
        case PROCESSOR_ARCHITECTURE_ARM64: return Arch::Arm64;
        default: return Arch::Unknown;
    }
}
Os   os()   { return Os::Windows; }
LibC libc() { return LibC::MSVC; }

std::string host_triple() {
    switch (arch()) {
        case Arch::X64:   return "x86_64-pc-windows-msvc";
        case Arch::Arm64: return "aarch64-pc-windows-msvc";
        default:          return "unknown-pc-windows-msvc";
    }
}

} // namespace rivet::env
