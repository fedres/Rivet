// runtime/archive/tar_zst.hpp — in-process tar.zst extraction
//
// Replaces the `tar --zstd -xf <archive> -C <dest> --strip-components=N`
// shell-out we used to do for toolchain installs and self-updates. The
// host `zstd` binary isn't guaranteed to be on PATH on fresh distros
// (we hit exit 127 on a clean ubuntu-22.04 GitHub runner the very first
// time the nightly smoke ran).
//
// Vendored libzstd gives us in-process decompression; the tar reader
// below handles the ustar / POSIX-1003.1-1990 subset that all modern
// `tar -cf` writers produce. We deliberately do NOT handle:
//   * symlinks (Windows can't always create them; bundled toolchains use
//     `cp -aL` so dereferencing already happened at publish time)
//   * device nodes / pipes
//   * extended pax headers (vcpkg / our own bundles don't emit them)
// If we encounter one, we skip the entry and continue — same fallback
// behaviour as if the symlink was missing on disk.
#pragma once

#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

namespace rivet::archive {

struct ExtractOptions {
    int strip_components = 0;  // drop this many leading path components
                                // (matches `tar --strip-components=N`)
};

// Extract a `.tar.zst` archive into `dest_dir`. `dest_dir` must already
// exist. Files are created with mode 0644, directories with mode 0755;
// the executable bit is preserved from the tar header so binaries stay
// runnable.
[[nodiscard]] Result<void> extract_tar_zst(const Path& archive,
                                            const Path& dest_dir,
                                            ExtractOptions opts = {});

} // namespace rivet::archive
