// rivet/platform/interface/env.hpp
// Environment and well-known directory Platform Abstraction Layer.
//
// Platform implementations:
//   platform/linux/env.cpp
//   platform/macos/env.cpp
//   platform/windows/env.cpp
#pragma once

#include "result.hpp"
#include "types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rivet::env {

// ─── Environment variables ───────────────────────────────────────────────────

[[nodiscard]] std::optional<std::string> get(std::string_view key);
[[nodiscard]] std::string                get_or(std::string_view key,
                                                std::string_view fallback);
void                                     set(std::string_view key,
                                             std::string_view value);
void                                     unset(std::string_view key);

// Returns a snapshot of the complete current environment.
[[nodiscard]] std::unordered_map<std::string, std::string> snapshot();

// ─── Well-known directories ──────────────────────────────────────────────────

// The user's home directory.
//   Linux/macOS: $HOME or password database
//   Windows:     %USERPROFILE%
[[nodiscard]] Result<Path> home_dir();

// Rivet's own data root (where it is installed).
//   Linux/macOS: ~/.rivet  (or $RIVET_HOME)
//   Windows:     %APPDATA%\rivet  (or %RIVET_HOME%)
[[nodiscard]] Result<Path> rivet_home();

// Per-platform cache directory (user-scoped, may be cleared by OS).
//   Linux:   $XDG_CACHE_HOME/rivet or ~/.cache/rivet
//   macOS:   ~/Library/Caches/rivet
//   Windows: %LOCALAPPDATA%\rivet\cache
[[nodiscard]] Result<Path> cache_dir();

// System-appropriate temp directory.
[[nodiscard]] Result<Path> temp_dir();

// ─── Architecture / platform ─────────────────────────────────────────────────

enum class Arch   { X64, Arm64, Unknown };
enum class Os     { Linux, macOS, Windows, Unknown };
enum class LibC   { Glibc, Musl, MSVC, Unknown };

[[nodiscard]] Arch arch();
[[nodiscard]] Os   os();
[[nodiscard]] LibC libc();

// Returns the canonical target triple for the bundled toolchain.
// e.g. "x86_64-linux-gnu", "arm64-apple-macos11", "x86_64-pc-windows-msvc"
[[nodiscard]] std::string host_triple();

} // namespace rivet::env
