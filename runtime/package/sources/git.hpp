// runtime/package/sources/git.hpp — git URL + revision dependencies
//
// Resolves dependencies declared as `{ git = "...", rev = "..." }`. Clones
// the repository to the cache directory on fetch.
#pragma once

#include "../source.hpp"

namespace rivet::pkg {

class GitSource final : public PackageSource {
public:
    [[nodiscard]] std::string id() const override { return "git"; }
    [[nodiscard]] bool handles(const PackageRef& ref) const override;
    [[nodiscard]] Result<ResolvedPackage> resolve(const PackageRef& ref) override;
    [[nodiscard]] Result<PortRecipe>      fetch(const ResolvedPackage& pkg,
                                                const Path& cache_dir) override;
};

} // namespace rivet::pkg
