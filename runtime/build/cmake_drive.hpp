// runtime/build/cmake_drive.hpp — drive cmake projects with rivet's toolchain
//
// M2 first cut: when `rivet build` runs in a directory that has a
// CMakeLists.txt but no rivet.toml, we generate a RivetToolchain.cmake
// pointing at the bundled clang/clang++/lld/llvm-rc + bundled ninja,
// then invoke cmake configure + cmake --build under .rivet/cmake/.
//
// Users get the same bundled-toolchain story they would from rivet's
// own multi-target build, without rewriting their build system. Any
// `find_package(...)` calls resolve against the per-project install
// tree that `rivet add ...` populates (CMAKE_PREFIX_PATH points there).
//
// Future iterations will layer a `find_package` interception macro on
// top of this toolchain file to log unresolved packages and suggest
// `rivet add <pkg>`. Not in this commit — the simple version above
// already handles most well-behaved projects (the ones whose
// CMakeLists call `find_package(X CONFIG REQUIRED)` and trust the
// prefix path to deliver).
#pragma once

#include "../toolchain/discovery.hpp"
#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

namespace rivet::build {

struct CmakeDriveOptions {
    Path                     project_dir;     // dir containing CMakeLists.txt
    Path                     toolchain_root;  // <rivet_home>/toolchains/<ver>
    Path                     extra_prefix;    // CMAKE_PREFIX_PATH addition (per-project vcpkg-installed tree)
    std::string              profile;         // "debug" / "release" / ...
    std::vector<std::string> defines;         // extra -D entries forwarded to cmake
};

[[nodiscard]] Result<void>
build_via_cmake(const CmakeDriveOptions& opts, const toolchain::ToolchainInfo& tc);

} // namespace rivet::build
