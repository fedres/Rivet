// runtime/toolchain/triple.hpp — target triple naming and host detection
#pragma once

#include "../../platform/interface/env.hpp"
#include <string>

namespace rivet::toolchain {

/// A parsed target triple, e.g. "x86_64-linux-gnu".
struct Triple {
    std::string arch;       // "x86_64" | "arm64" | "aarch64"
    std::string os;         // "linux"  | "macos"  | "windows"
    std::string abi;        // "gnu"    | "musl"   | "msvc"    | ""

    [[nodiscard]] std::string to_string() const {
        if (abi.empty()) return arch + '-' + os;
        return arch + '-' + os + '-' + abi;
    }

    [[nodiscard]] static Triple from_string(std::string_view s);
    [[nodiscard]] static Triple host();
};

// ─── Inline implementations ──────────────────────────────────────────────────

inline Triple Triple::from_string(std::string_view s) {
    Triple t;
    std::string str{s};
    auto first  = str.find('-');
    auto second = (first == std::string::npos) ? std::string::npos : str.find('-', first + 1);

    t.arch = (first != std::string::npos)  ? str.substr(0, first)  : str;
    if (first != std::string::npos) {
        t.os = (second != std::string::npos) ? str.substr(first + 1, second - first - 1)
                                              : str.substr(first + 1);
        if (second != std::string::npos)
            t.abi = str.substr(second + 1);
    }
    return t;
}

inline Triple Triple::host() {
    // Delegate to rivet::env::host_triple() (implemented per platform in PAL).
    auto ht = rivet::env::host_triple();
    return Triple::from_string(ht);
}

} // namespace rivet::toolchain
