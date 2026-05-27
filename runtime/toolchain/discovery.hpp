// runtime/toolchain/discovery.hpp — locate the bundled LLVM toolchain
//
// The bundled toolchain lives at:
//   <rivet_home>/toolchains/<version>/bin/
//
// Multiple toolchain versions may coexist. The "active" one is recorded in
//   <rivet_home>/meta/toolchain.json
#pragma once

#include "triple.hpp"
#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rivet::toolchain {

// ─── Toolchain descriptor ─────────────────────────────────────────────────────

struct ToolchainInfo {
    std::string version;     // e.g. "18.1.0"
    Path        root;        // absolute path: <rivet_home>/toolchains/18.1.0/
    Triple      triple;      // target triple this toolchain was built for

    // Binary suffix — `.exe` on Windows, empty everywhere else. We use a
    // build-time constant rather than runtime triple inspection because
    // ToolchainInfo paths must resolve at compile/link time even before
    // `triple` is fully populated, and a host toolchain is always the
    // host's binary format.
#if defined(_WIN32)
    static constexpr const char* exe_suffix = ".exe";
#else
    static constexpr const char* exe_suffix = "";
#endif

    [[nodiscard]] Path clang()       const { return root / "bin" / (std::string("clang") + exe_suffix); }
    [[nodiscard]] Path clangpp()     const { return root / "bin" / (std::string("clang++") + exe_suffix); }
    [[nodiscard]] Path clang_cl()    const { return root / "bin" / (std::string("clang-cl") + exe_suffix); }
    [[nodiscard]] Path lld()         const { return root / "bin" / (std::string("lld") + exe_suffix); }
    [[nodiscard]] Path lld_link()    const { return root / "bin" / (std::string("lld-link") + exe_suffix); }
    [[nodiscard]] Path llvm_ar()     const { return root / "bin" / (std::string("llvm-ar") + exe_suffix); }
    [[nodiscard]] Path llvm_ranlib() const { return root / "bin" / (std::string("llvm-ranlib") + exe_suffix); }
    [[nodiscard]] Path llvm_lib()    const { return root / "bin" / (std::string("llvm-lib") + exe_suffix); }
    [[nodiscard]] Path llvm_strip()  const { return root / "bin" / (std::string("llvm-strip") + exe_suffix); }
    [[nodiscard]] Path scan_deps()   const { return root / "bin" / (std::string("clang-scan-deps") + exe_suffix); }

    [[nodiscard]] Path compiler_rt_dir() const {
        return root / "lib" / "clang" / version / "lib";
    }
    [[nodiscard]] Path libcxx_include() const {
        return root / "include" / "c++" / "v1";
    }
};

// ─── Discovery ────────────────────────────────────────────────────────────────

/// Find the active (default) toolchain under rivet_home.
[[nodiscard]] Result<ToolchainInfo> find_active(const Path& rivet_home);

/// List all installed toolchain versions under rivet_home.
[[nodiscard]] Result<std::vector<ToolchainInfo>> list_installed(const Path& rivet_home);

/// Set the active toolchain version.
[[nodiscard]] Result<void> set_active(const Path& rivet_home, std::string_view version);

} // namespace rivet::toolchain
