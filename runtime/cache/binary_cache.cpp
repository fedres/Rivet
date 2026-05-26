// runtime/cache/binary_cache.cpp — content-addressed binary cache
#include "binary_cache.hpp"
#include "key.hpp"
#include "../../platform/interface/fs.hpp"

#include <algorithm>
#include <format>

namespace rivet::cache {

// ─── Canonical key serialization ────────────────────────────────────────────

namespace {

// Compose the canonical key blob: stable field ordering, sorted features.
// Whatever shape this takes, two PackageCacheKeys that should be equivalent
// must produce identical bytes here.
std::string canonical_key_bytes(const PackageCacheKey& k) {
    std::vector<std::string> features = k.features;
    std::sort(features.begin(), features.end());

    std::string out;
    out.reserve(256);
    out += "v1\n";                                  // serializer version tag
    out += "name="     + k.name         + "\n";
    out += "version="  + k.version      + "\n";
    out += "triple="   + k.triple       + "\n";
    out += "toolchain="+ k.toolchain_id + "\n";
    out += "features=";
    for (size_t i = 0; i < features.size(); ++i) {
        if (i) out += ",";
        out += features[i];
    }
    out += "\n";
    out += "deps="     + k.deps_hash    + "\n";
    return out;
}

} // namespace

std::string PackageCacheKey::digest() const {
    auto blob = canonical_key_bytes(*this);
    auto hex  = sha256_string(blob);
    return hex ? *hex : std::string{};
}

std::string roll_deps_hash(const std::vector<std::string>& dep_digests) {
    std::vector<std::string> sorted = dep_digests;
    std::sort(sorted.begin(), sorted.end());
    std::string blob;
    for (const auto& d : sorted) { blob += d; blob += '\n'; }
    auto hex = sha256_string(blob);
    return hex ? *hex : std::string{};
}

// ─── Filesystem store ────────────────────────────────────────────────────────

BinaryCache::BinaryCache(Path root) : root_(std::move(root)) {}

Path BinaryCache::artifact_path(const PackageCacheKey& key) const {
    auto d = key.digest();
    std::string prefix = d.empty() ? "00" : d.substr(0, 2);
    return root_ / "binary" / prefix / (d + ".tar.zst");
}

Path BinaryCache::metadata_path(const PackageCacheKey& key) const {
    auto d = key.digest();
    std::string prefix = d.empty() ? "00" : d.substr(0, 2);
    return root_ / "binary" / prefix / (d + ".meta");
}

bool BinaryCache::has(const PackageCacheKey& key) const {
    auto r = rivet::fs::exists(artifact_path(key));
    return r && *r;
}

Result<Path> BinaryCache::lookup(const PackageCacheKey& key) const {
    auto p = artifact_path(key);
    auto ex = rivet::fs::exists(p);
    if (!ex || !*ex)
        return make_error<Path>(std::format(
            "binary_cache: miss for {} {} (digest {})",
            key.name, key.version, key.digest()));
    return p;
}

Result<void> BinaryCache::store(const PackageCacheKey& key,
                                 const Path& source_tarball) {
    auto dest = artifact_path(key);
    if (auto r = rivet::fs::create_dirs(dest.parent_path()); !r) return r;

    auto data = rivet::fs::read_file(source_tarball);
    if (!data) return propagate<void>(data);

    rivet::ByteSpan span{data->data(), data->size()};
    if (auto r = rivet::fs::write_atomic(dest, span); !r) return r;

    auto meta_str = canonical_key_bytes(key);
    rivet::ByteSpan meta_span{
        reinterpret_cast<const std::byte*>(meta_str.data()), meta_str.size()};
    if (auto r = rivet::fs::write_atomic(metadata_path(key), meta_span); !r)
        return r;

    return {};
}

} // namespace rivet::cache
