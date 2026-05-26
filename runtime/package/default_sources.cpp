// runtime/package/default_sources.cpp — SourceRegistry factory
#include "default_sources.hpp"
#include "sources/local.hpp"
#include "sources/git.hpp"
#include "sources/vcpkg.hpp"
#include "../../platform/interface/env.hpp"

namespace rivet::pkg {

SourceRegistry make_default_registry(const Path& rivet_home) {
    SourceRegistry reg;
    reg.add(std::make_unique<LocalSource>());
    reg.add(std::make_unique<GitSource>());

    VcpkgConfig vcpkg_cfg;
    vcpkg_cfg.vcpkg_root = rivet_home / "sources" / "vcpkg";
    // Allow override of the pinned baseline via env var so users can pin to a
    // specific catalog commit without rebuilding rivet.
    if (auto bl = rivet::env::get("RIVET_VCPKG_BASELINE"))
        vcpkg_cfg.baseline = *bl;
    reg.add(std::make_unique<VcpkgSource>(std::move(vcpkg_cfg)));

    return reg;
}

} // namespace rivet::pkg
