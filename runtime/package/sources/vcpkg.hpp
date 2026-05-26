// runtime/package/sources/vcpkg.hpp — vcpkg as a package source backend.
//
// This is the bridge that lets users say `rivet add fmt` and pull from the
// existing vcpkg port ecosystem without ever invoking vcpkg directly.
//
// Architecture:
//   1. On first use, clone microsoft/vcpkg to <vcpkg_root>/ (default
//      ~/.rivet/sources/vcpkg/), pinned to a known baseline commit.
//   2. For each requested port name, read ports/<name>/vcpkg.json to extract
//      name, version, and transitive dependencies.
//   3. fetch() will (in B4) generate a custom rivet triplet that forces the
//      bundled clang, then dispatch vcpkg's own machinery to download the
//      upstream source archive and apply the port's portfile.cmake.
//
// The user never sees vcpkg directly — it is purely a backend.
#pragma once

#include "../source.hpp"

namespace rivet::pkg {

// Compute the overlay-triplet name vcpkg uses for the *host* (the only
// supported target at the moment). E.g. "x64-linux-rivet". Public so the
// build engine can find the artifact tree at <install_root>/<triplet>/.
[[nodiscard]] std::string host_vcpkg_triplet();

struct VcpkgConfig {
    // Local clone of microsoft/vcpkg. Created on first use.
    Path vcpkg_root;
    // Pinned commit SHA (the "baseline"). Used for deterministic resolution.
    // Empty = use whatever HEAD the local clone is at.
    std::string baseline;
    // Path to <rivet_home>. fetch() looks up the active bundled toolchain
    // under this root to pin vcpkg's compiler via a custom triplet.
    Path rivet_home;
};

class VcpkgSource final : public PackageSource {
public:
    explicit VcpkgSource(VcpkgConfig cfg);

    [[nodiscard]] std::string id() const override { return "vcpkg"; }
    [[nodiscard]] bool handles(const PackageRef& ref) const override;
    [[nodiscard]] Result<ResolvedPackage> resolve(const PackageRef& ref) override;
    [[nodiscard]] Result<PortRecipe>      fetch(const ResolvedPackage& pkg,
                                                const Path& cache_dir) override;

    // Bootstrap the local vcpkg clone if it doesn't exist. Safe to call
    // repeatedly. Returns the resolved root path.
    [[nodiscard]] Result<Path> ensure_root();

private:
    VcpkgConfig cfg_;
};

} // namespace rivet::pkg
