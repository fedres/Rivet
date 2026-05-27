// runtime/version.hpp — version literals.
//
// CMake's build path templates version.hpp.in into <BINARY_DIR>/runtime/
// version.hpp with git-hash + target-triple injected at configure time.
// rivet's self-build path (M1) doesn't run cmake, so it consumes this
// statically-checked-in copy instead.
//
// The source-tree include path takes precedence over the binary-dir
// path in target_include_directories order, so CMake builds also resolve
// to this file in practice. (We keep the .in around for now to avoid
// breaking the cmake configure step; the templated binary-dir output is
// shadowed.) Defines can be overridden via `-DRIVET_VERSION_STR=...`.
#pragma once

#include <string_view>

namespace rivet {

#ifndef RIVET_VERSION_STR
#  define RIVET_VERSION_STR "0.1.0"
#endif
#ifndef RIVET_VERSION_MAJOR
#  define RIVET_VERSION_MAJOR 0
#endif
#ifndef RIVET_VERSION_MINOR
#  define RIVET_VERSION_MINOR 1
#endif
#ifndef RIVET_VERSION_PATCH
#  define RIVET_VERSION_PATCH 0
#endif
#ifndef RIVET_GIT_HASH_STR
#  define RIVET_GIT_HASH_STR ""
#endif
#ifndef RIVET_TARGET_TRIPLE_STR
#  define RIVET_TARGET_TRIPLE_STR ""
#endif

inline constexpr std::string_view kVersion      = RIVET_VERSION_STR;
inline constexpr std::string_view kVersionFull  = "rivet " RIVET_VERSION_STR;
inline constexpr int kVersionMajor              = RIVET_VERSION_MAJOR;
inline constexpr int kVersionMinor              = RIVET_VERSION_MINOR;
inline constexpr int kVersionPatch              = RIVET_VERSION_PATCH;
inline constexpr std::string_view kGitHash      = RIVET_GIT_HASH_STR;
inline constexpr std::string_view kTargetTriple = RIVET_TARGET_TRIPLE_STR;

} // namespace rivet
