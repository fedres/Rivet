// runtime/toolchain/sdk.hpp — host SDK detection (Xcode / Windows SDK)
//
// Rivet ships its own clang and libc++, but it cannot ship the platform
// SDK (Apple frameworks, Windows Kits headers). Those are governed by
// EULAs that forbid redistribution. So like cargo / zig / bun before us,
// we *detect* whether the user has the SDK installed and surface a clear
// install hint when they don't.
//
// On Linux we currently assume the system headers are always present
// (glibc/musl + kernel headers ship by default on every distro that has
// gcc/clang installed, and Rivet doesn't ship libc at all). Once we
// support fully hermetic Linux builds (Phase D / Nix-style closures),
// this contract will tighten.
#pragma once

#include <string>

namespace rivet::toolchain {

struct SdkInfo {
    bool        present = false;
    std::string path;    // SDK root if present (xcrun output / Windows Kits dir)
    std::string hint;    // cargo-style install instruction shown on failure
};

// Detect the host platform SDK. Cached on first call.
[[nodiscard]] const SdkInfo& detect_host_sdk();

} // namespace rivet::toolchain
