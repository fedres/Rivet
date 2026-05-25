// runtime/toolchain/discovery.cpp — toolchain discovery implementation
#include "discovery.hpp"
#include "../../platform/interface/fs.hpp"
#include "../../platform/interface/env.hpp"

#include <format>
#include <fstream>
#include <algorithm>

namespace rivet::toolchain {

// ─── Helpers ─────────────────────────────────────────────────────────────────

static Result<std::string> read_active_version(const Path& rivet_home) {
    auto meta_file = rivet_home / "meta" / "toolchain.json";

    if (!rivet::fs::exists(meta_file).value_or(false))
        return make_error<std::string>("no active toolchain configured; run 'rivet toolchain install'");

    auto data = rivet::fs::read_file(meta_file);
    if (!data) return propagate<std::string>(data);

    // Simple key extraction: "active_version": "18.1.0"
    std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
    auto key = text.find("\"active_version\"");
    if (key == std::string_view::npos)
        return make_error<std::string>("toolchain.json missing 'active_version' key");

    auto colon  = text.find(':', key);
    auto qopen  = text.find('"', colon + 1);
    auto qclose = text.find('"', qopen + 1);

    if (colon == std::string_view::npos || qopen == std::string_view::npos ||
        qclose == std::string_view::npos)
        return make_error<std::string>("malformed toolchain.json");

    return std::string{text.substr(qopen + 1, qclose - qopen - 1)};
}

static ToolchainInfo make_info(const Path& rivet_home, const std::string& version) {
    ToolchainInfo info;
    info.version = version;
    info.root    = rivet_home / "toolchains" / version;
    info.triple  = Triple::host();
    return info;
}

// ─── find_active() ───────────────────────────────────────────────────────────

Result<ToolchainInfo> find_active(const Path& rivet_home) {
    auto ver = read_active_version(rivet_home);
    if (!ver) return propagate<ToolchainInfo>(ver);

    auto info = make_info(rivet_home, *ver);

    // Verify the bin/clang binary is present.
    if (!rivet::fs::exists(info.clang()).value_or(false))
        return make_error<ToolchainInfo>(std::format(
            "toolchain {} not found at {}\nRun 'rivet toolchain install {}' to fetch it.",
            *ver, info.root.string(), *ver));

    return info;
}

// ─── list_installed() ────────────────────────────────────────────────────────

Result<std::vector<ToolchainInfo>> list_installed(const Path& rivet_home) {
    auto tc_dir = rivet_home / "toolchains";

    if (!rivet::fs::exists(tc_dir).value_or(false))
        return std::vector<ToolchainInfo>{};

    auto entries = rivet::fs::list_dir(tc_dir);
    if (!entries) return propagate<std::vector<ToolchainInfo>>(entries);

    std::vector<ToolchainInfo> result;
    for (const auto& e : *entries) {
        auto clang = e / "bin" / "clang";
        if (rivet::fs::exists(clang).value_or(false)) {
            result.push_back(make_info(rivet_home, e.filename().string()));
        }
    }

    std::sort(result.begin(), result.end(), [](const ToolchainInfo& a, const ToolchainInfo& b) {
        return a.version > b.version;  // newest first
    });

    return result;
}

// ─── set_active() ────────────────────────────────────────────────────────────

Result<void> set_active(const Path& rivet_home, std::string_view version) {
    auto meta_dir = rivet_home / "meta";
    RIVET_TRY(rivet::fs::create_dirs(meta_dir));

    auto json = std::format("{{\"active_version\":\"{}\"}}\n", version);
    return rivet::fs::write_atomic(meta_dir / "toolchain.json",
        rivet::ByteSpan{reinterpret_cast<const std::byte*>(json.data()), json.size()});
}

} // namespace rivet::toolchain
