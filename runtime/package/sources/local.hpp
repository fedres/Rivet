// runtime/package/sources/local.hpp — `path = "../foo"` dependencies
//
// Resolves a dependency declared with a local filesystem path. Useful for
// monorepo workflows, dev overrides, and bootstrapping new packages.
#pragma once

#include "../source.hpp"

namespace rivet::pkg {

class LocalSource final : public PackageSource {
public:
    [[nodiscard]] std::string id() const override { return "path"; }
    [[nodiscard]] bool handles(const PackageRef& ref) const override;
    [[nodiscard]] Result<ResolvedPackage> resolve(const PackageRef& ref) override;
    [[nodiscard]] Result<PortRecipe>      fetch(const ResolvedPackage& pkg,
                                                const Path& cache_dir) override;
};

} // namespace rivet::pkg
