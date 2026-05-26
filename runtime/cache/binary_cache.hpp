// runtime/cache/binary_cache.hpp — Content-addressed binary package cache.
//
// The killer feature: turn "build boost from source for 20 minutes" into
// "download the prebuilt artifact in 2 seconds" by deduplicating across
// every machine that ever built it with the same inputs.
//
// Cache key = SHA-256 of canonical (name, version, triple, toolchain_id,
//             features-sorted, deps_hash) where deps_hash recursively rolls
//             in every transitive dep's own cache key. Two builds that share
//             every input are guaranteed to share the same key; otherwise
//             they get distinct keys.
//
// Layout on disk:
//   <root>/binary/<dd>/<digest>.tar.zst   — install-tree tarball
//   <root>/binary/<dd>/<digest>.meta      — sidecar with key material
//
// Remote backends (HTTP/S3) come later; this header only defines the local
// filesystem store. A remote layer plugs in via the BinaryCacheBackend
// interface at the bottom of the file.
#pragma once

#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <string>
#include <vector>

namespace rivet::cache {

// ─── Key material ────────────────────────────────────────────────────────────

struct PackageCacheKey {
    std::string name;
    std::string version;
    std::string triple;          // "x86_64-linux-gnu", "arm64-apple-macos", etc.
    std::string toolchain_id;    // bundled clang version + build flags hash
    std::vector<std::string> features;  // alphabetical at digest time
    std::string deps_hash;       // recursive sha256 over sorted "name=ver=key" lines

    // Compute the canonical 64-char hex digest used as the cache lookup key.
    // Deterministic: same fields in any order produce the same digest.
    [[nodiscard]] std::string digest() const;
};

// Roll multiple dep digests into a single deps_hash. Order-independent.
[[nodiscard]] std::string roll_deps_hash(const std::vector<std::string>& dep_digests);

// ─── Local filesystem store ──────────────────────────────────────────────────

class BinaryCache {
public:
    // Root directory; typically ~/.rivet/cache.
    explicit BinaryCache(Path root);

    // Whether an artifact exists for this key.
    [[nodiscard]] bool has(const PackageCacheKey& key) const;

    // Return the absolute path to the cached artifact (a .tar.zst), or
    // NotFound. Caller is responsible for extracting it.
    [[nodiscard]] Result<Path> lookup(const PackageCacheKey& key) const;

    // Insert an artifact. `source_tarball` is moved into the cache atomically.
    [[nodiscard]] Result<void> store(const PackageCacheKey& key,
                                      const Path& source_tarball);

    // Path layout helpers (also useful for callers wanting to stream the
    // artifact into place without copying).
    [[nodiscard]] Path artifact_path(const PackageCacheKey& key) const;
    [[nodiscard]] Path metadata_path(const PackageCacheKey& key) const;

private:
    Path root_;
};

} // namespace rivet::cache
