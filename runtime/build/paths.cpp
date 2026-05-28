// runtime/build/paths.cpp — derived build paths
#include "paths.hpp"

#include <cstdlib>
#include <filesystem>

namespace rivet::build {

Path build_root_for(const Path& project_root) {
    if (const char* env = std::getenv("RIVET_TARGET_DIR"); env && *env) {
        Path p{env};
        if (p.is_relative()) p = std::filesystem::current_path() / p;
        return p;
    }
    return project_root / ".rivet" / "build";
}

} // namespace rivet::build
