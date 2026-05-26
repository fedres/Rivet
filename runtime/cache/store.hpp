// runtime/cache/store.hpp — SQLite-backed local artifact cache
//
// Layout on disk:
//   ~/.rivet/cache/
//     objects/<xx>/<sha256>.zst    — compressed artifact blobs
//     meta/rivet_cache.db          — SQLite index
//
// All operations are crash-consistent:
//   - Artifacts are written atomically via write_atomic() before indexing.
//   - DB entries are inserted after the artifact file is durably written.
//   - On startup, any DB entry whose artifact file is missing is pruned.
#pragma once

#include "../build/ir.hpp"
#include "key.hpp"
#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Forward-declare sqlite3 to avoid requiring the header at include time.
struct sqlite3;

namespace rivet::cache {

struct CacheEntry {
    build::CacheKey key;
    Path            artifact_path;   // absolute path inside objects/
    int64_t         created_at;      // Unix epoch seconds
    int64_t         last_hit;        // Unix epoch seconds
    int64_t         size_bytes;
};

struct StoreStats {
    std::size_t entry_count = 0;
    int64_t     total_bytes = 0;
    int64_t     oldest_hit  = 0;   // Unix epoch of the least-recently-used entry
};

class Store {
public:
    /// Open (or create) a cache store rooted at `cache_dir`.
    /// Creates the directory structure and initialises the SQLite DB if needed.
    [[nodiscard]] static Result<Store> open(const Path& cache_dir);

    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    ~Store();

    // Non-copyable.
    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;

    // ─── Lookup ────────────────────────────────────────────────────────────

    /// Check whether an artifact exists for `key`.
    [[nodiscard]] bool has(const build::CacheKey& key) const noexcept;

    /// Fetch an artifact: copies it to `dest` and updates last_hit.
    [[nodiscard]] Result<void> fetch(const build::CacheKey& key, const Path& dest);

    // ─── Storage ───────────────────────────────────────────────────────────

    /// Store an artifact located at `source` under `key`.
    /// The source file is compressed (zstd) and written atomically.
    [[nodiscard]] Result<void> put(const build::CacheKey& key, const Path& source);

    // ─── Remote cache ──────────────────────────────────────────────────────────
    // URL is read from the RIVET_REMOTE_CACHE env var on open(), or set here.

    void set_remote_url(std::string url);

    /// Try to fetch artifact from remote cache; on success also stores it locally.
    [[nodiscard]] Result<void> fetch_remote(const build::CacheKey& key, const Path& dest);

    /// Upload artifact to remote cache (best-effort; never propagates errors).
    void push_remote(const build::CacheKey& key, const Path& artifact_path);

    // ─── Maintenance ───────────────────────────────────────────────────────

    [[nodiscard]] Result<StoreStats> stats() const;

    /// Remove entries older than `max_age_sec` seconds (0 = no-op).
    [[nodiscard]] Result<std::size_t> evict_older_than(int64_t max_age_sec);

    /// Remove entries until total_bytes ≤ max_bytes (LRU order).
    [[nodiscard]] Result<std::size_t> trim_to(int64_t max_bytes);

private:
    explicit Store(Path cache_dir, sqlite3* db);

    Path        cache_dir_;
    sqlite3*    db_         = nullptr;
    std::string remote_url_;        // empty = remote cache disabled

    [[nodiscard]] Path artifact_path(const build::CacheKey& key) const;
    [[nodiscard]] Result<void> init_schema();
    [[nodiscard]] Result<void> index_entry(const build::CacheKey& key,
                                            const Path& artifact,
                                            int64_t size_bytes);
};

} // namespace rivet::cache
