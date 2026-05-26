// runtime/cache/store.cpp — SQLite-backed local artifact cache implementation
//
// SQLite is used via the vendored amalgamation at vendor/sqlite/sqlite3.h.
// Until that is in place we compile with a forward-declared stub that logs
// a "cache unavailable" warning and falls back to a no-op pass-through.
#include "store.hpp"
#include "../../platform/interface/fs.hpp"
#include "../../platform/interface/time.hpp"
#include "../../platform/interface/env.hpp"
#include "../../platform/interface/net.hpp"

#include <format>
#include <cstring>

// ─── SQLite availability guard ───────────────────────────────────────────────
// When the vendored sqlite amalgamation is present, replace this with:
//   #include "../../vendor/sqlite/sqlite3.h"

#if __has_include("../../vendor/sqlite/sqlite3.h")
#  include "../../vendor/sqlite/sqlite3.h"
#  define RIVET_HAVE_SQLITE 1
#else
// Minimal stub so the rest of this TU compiles without sqlite.
struct sqlite3 {};
static int sqlite3_open(const char*, sqlite3**) { return 1; }
static int sqlite3_close(sqlite3*) { return 0; }
static int sqlite3_exec(sqlite3*, const char*, void*, void*, char**) { return 1; }
static int sqlite3_prepare_v2(sqlite3*,const char*,int,struct sqlite3_stmt**,const char**){return 1;}
static void sqlite3_finalize(struct sqlite3_stmt*) {}
static int sqlite3_step(struct sqlite3_stmt*) { return 101; /*SQLITE_DONE*/ }
static int sqlite3_bind_text(struct sqlite3_stmt*,int,const char*,int,void(*)(void*)){return 1;}
static int sqlite3_bind_int64(struct sqlite3_stmt*,int,long long){return 1;}
static int sqlite3_column_int64(struct sqlite3_stmt*,int){return 0;}
static const unsigned char* sqlite3_column_text(struct sqlite3_stmt*,int){return nullptr;}
static const char* sqlite3_errmsg(sqlite3*){return "sqlite not available";}
static void sqlite3_free(void*){}
struct sqlite3_stmt {};
#  define RIVET_HAVE_SQLITE 0
#endif

namespace rivet::cache {

// ─── Construction / destruction ───────────────────────────────────────────────

Store::Store(Path cache_dir, sqlite3* db)
    : cache_dir_(std::move(cache_dir)), db_(db) {}

Store::Store(Store&& o) noexcept
    : cache_dir_(std::move(o.cache_dir_)), db_(o.db_) { o.db_ = nullptr; }

Store& Store::operator=(Store&& o) noexcept {
    if (this != &o) {
        if (db_) sqlite3_close(db_);
        cache_dir_ = std::move(o.cache_dir_);
        db_ = o.db_;
        o.db_ = nullptr;
    }
    return *this;
}

Store::~Store() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

// ─── open() ──────────────────────────────────────────────────────────────────

Result<Store> Store::open(const Path& cache_dir) {
    auto objects_dir = cache_dir / "objects";
    auto meta_dir    = cache_dir / "meta";

    RIVET_TRY(rivet::fs::create_dirs(objects_dir));
    RIVET_TRY(rivet::fs::create_dirs(meta_dir));

    auto db_path = (meta_dir / "rivet_cache.db").string();

    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != 0) {
        // Cache DB unavailable — non-fatal, operate in pass-through mode.
        // Log a diagnostic if terminal is available.
        return Store{cache_dir, nullptr};
    }

    Store store{cache_dir, db};
    if (auto r = store.init_schema(); !r) {
        // Schema init failed — still usable but only for lookup failures.
        (void)r;
    }
    // Automatically wire remote cache from environment.
    if (auto u = rivet::env::get("RIVET_REMOTE_CACHE"))
        store.remote_url_ = *u;
    return store;
}

// ─── init_schema() ───────────────────────────────────────────────────────────

Result<void> Store::init_schema() {
    if (!db_) return {};

    const char* sql = R"sql(
        CREATE TABLE IF NOT EXISTS cache_entries (
            key         TEXT PRIMARY KEY,
            artifact    TEXT NOT NULL,
            created_at  INTEGER NOT NULL,
            last_hit    INTEGER NOT NULL,
            size_bytes  INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_last_hit ON cache_entries(last_hit);
    )sql";

    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != 0) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        return make_error<void>(std::format("cache schema init failed: {}", msg));
    }
    return {};
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

Path Store::artifact_path(const build::CacheKey& key) const {
    // Two-char prefix for sharding, same as git object store.
    auto prefix = key.hex.substr(0, 2);
    return cache_dir_ / "objects" / prefix / (key.hex + ".zst");
}

// ─── has() ───────────────────────────────────────────────────────────────────

bool Store::has(const build::CacheKey& key) const noexcept {
    if (key.empty()) return false;
    return rivet::fs::exists(artifact_path(key)).value_or(false);
}

// ─── fetch() ─────────────────────────────────────────────────────────────────

Result<void> Store::fetch(const build::CacheKey& key, const Path& dest) {
    if (!has(key))
        return make_error<void>(std::format("cache miss: {}", key.hex));

    auto src = artifact_path(key);

    // TODO: decompress zstd into dest when libzstd is vendored.
    // For now: plain file copy.
    RIVET_TRY(rivet::fs::copy_file(src, dest));

    // Update last_hit in DB.
    if (db_) {
        auto now = rivet::time::to_unix_sec(rivet::time::wall_now());
        const char* sql = "UPDATE cache_entries SET last_hit=? WHERE key=?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == 0) {
            sqlite3_bind_int64(stmt, 1, now);
            sqlite3_bind_text(stmt, 2, key.hex.c_str(), -1, nullptr);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    return {};
}

// ─── put() ───────────────────────────────────────────────────────────────────

Result<void> Store::put(const build::CacheKey& key, const Path& source) {
    if (key.empty())
        return make_error<void>("cannot store artifact with empty cache key");

    auto dest = artifact_path(key);
    RIVET_TRY(rivet::fs::create_dirs(dest.parent_path()));

    // TODO: compress to .zst when libzstd is vendored.
    // For now: atomic copy.
    auto data = rivet::fs::read_file(source);
    if (!data) return propagate<void>(data);

    RIVET_TRY(rivet::fs::write_atomic(dest, *data));

    auto stat = rivet::fs::stat(dest);
    int64_t sz = stat ? static_cast<int64_t>(stat->size_bytes) : 0;

    return index_entry(key, dest, sz);
}

// ─── index_entry() ───────────────────────────────────────────────────────────

Result<void> Store::index_entry(const build::CacheKey& key,
                                 const Path& artifact,
                                 int64_t size_bytes) {
    if (!db_) return {};

    auto now = rivet::time::to_unix_sec(rivet::time::wall_now());
    const char* sql = R"sql(
        INSERT OR REPLACE INTO cache_entries(key, artifact, created_at, last_hit, size_bytes)
        VALUES (?, ?, ?, ?, ?)
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != 0)
        return make_error<void>(std::format("cache index failed: {}", sqlite3_errmsg(db_)));

    auto art_str = artifact.string();
    sqlite3_bind_text(stmt,  1, key.hex.c_str(),  -1, nullptr);
    sqlite3_bind_text(stmt,  2, art_str.c_str(),  -1, nullptr);
    sqlite3_bind_int64(stmt, 3, now);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_bind_int64(stmt, 5, size_bytes);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return {};
}

// ─── Remote cache ─────────────────────────────────────────────────────────────

void Store::set_remote_url(std::string url) {
    remote_url_ = std::move(url);
}

// Remote artifact URL: <base>/<hex[0:2]>/<hex>.zst
static std::string remote_artifact_url(const std::string& base,
                                        const build::CacheKey& key) {
    return base + "/" + key.hex.substr(0, 2) + "/" + key.hex + ".zst";
}

Result<void> Store::fetch_remote(const build::CacheKey& key, const Path& dest) {
    if (remote_url_.empty()) {
        // Check env var once per call (lazy init for remote URL).
        if (auto u = rivet::env::get("RIVET_REMOTE_CACHE"))
            remote_url_ = *u;
        if (remote_url_.empty())
            return make_error<void>("remote cache not configured");
    }

    auto url_str = remote_artifact_url(remote_url_, key);
    auto url_r   = rivet::net::Url::parse(url_str);
    if (!url_r) return propagate<void>(url_r);

    auto local = artifact_path(key);
    RIVET_TRY(rivet::fs::create_dirs(local.parent_path()));

    // Download straight to the local cache location.
    auto dl = rivet::net::download_file(*url_r, local);
    if (!dl) return propagate<void>(dl);

    // Index locally.
    auto stat = rivet::fs::stat(local);
    int64_t sz = stat ? static_cast<int64_t>(stat->size_bytes) : 0;
    (void)index_entry(key, local, sz);

    // Copy to destination.
    return rivet::fs::copy_file(local, dest);
}

void Store::push_remote(const build::CacheKey& key, const Path& artifact_path_local) {
    if (remote_url_.empty()) {
        if (auto u = rivet::env::get("RIVET_REMOTE_CACHE"))
            remote_url_ = *u;
        if (remote_url_.empty()) return;
    }

    auto data = rivet::fs::read_file(artifact_path_local);
    if (!data) return;

    auto url_str = remote_artifact_url(remote_url_, key);
    auto url_r   = rivet::net::Url::parse(url_str);
    if (!url_r) return;

    rivet::net::HttpClient client{remote_url_};
    // Best-effort PUT; silently ignore any error.
    auto path_suffix = "/" + key.hex.substr(0, 2) + "/" + key.hex + ".zst";
    (void)client.put(path_suffix,
                     rivet::ByteSpan{data->data(), data->size()},
                     "application/octet-stream");
}

// ─── stats() ─────────────────────────────────────────────────────────────────

Result<StoreStats> Store::stats() const {
    StoreStats s;
    if (!db_) return s;

    const char* sql = "SELECT COUNT(*), SUM(size_bytes), MIN(last_hit) FROM cache_entries";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != 0) return s;
    if (sqlite3_step(stmt) == 100 /*SQLITE_ROW*/) {  // 100 = SQLITE_ROW
        s.entry_count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
        s.total_bytes  = sqlite3_column_int64(stmt, 1);
        s.oldest_hit   = sqlite3_column_int64(stmt, 2);
    }
    sqlite3_finalize(stmt);
    return s;
}

// ─── evict_older_than() ──────────────────────────────────────────────────────

Result<std::size_t> Store::evict_older_than(int64_t max_age_sec) {
    if (!db_ || max_age_sec == 0) return 0u;

    auto cutoff = rivet::time::to_unix_sec(rivet::time::wall_now()) - max_age_sec;
    const char* sql = "SELECT key, artifact FROM cache_entries WHERE last_hit < ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != 0) return 0u;

    sqlite3_bind_int64(stmt, 1, cutoff);

    std::vector<std::string> paths;
    std::size_t count = 0;
    while (sqlite3_step(stmt) == 100 /*SQLITE_ROW*/) {
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)))
            paths.emplace_back(p);
        ++count;
    }
    sqlite3_finalize(stmt);

    // Delete artifact files from disk.
    for (const auto& path : paths)
        (void)rivet::fs::remove_file(Path{path});

    // Remove DB rows.
    const char* del = "DELETE FROM cache_entries WHERE last_hit < ?";
    sqlite3_stmt* del_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, del, -1, &del_stmt, nullptr) == 0) {
        sqlite3_bind_int64(del_stmt, 1, cutoff);
        sqlite3_step(del_stmt);
        sqlite3_finalize(del_stmt);
    }
    return count;
}

// ─── trim_to() ───────────────────────────────────────────────────────────────

Result<std::size_t> Store::trim_to(int64_t max_bytes) {
    if (!db_) return 0u;

    auto s = stats();
    if (!s || s->total_bytes <= max_bytes) return 0u;

    // Collect LRU entries until we'd be under budget, then delete them.
    const char* sql =
        "SELECT key, artifact, size_bytes FROM cache_entries ORDER BY last_hit ASC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != 0) return 0u;

    int64_t running = s->total_bytes;
    std::vector<std::pair<std::string, std::string>> to_evict;  // (key, artifact)

    while (sqlite3_step(stmt) == 100 /*SQLITE_ROW*/ && running > max_bytes) {
        auto* key  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        auto* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (key && path) to_evict.emplace_back(key, path);
        running -= sqlite3_column_int64(stmt, 2);
    }
    sqlite3_finalize(stmt);

    std::size_t evicted = 0;
    for (const auto& [key, path] : to_evict) {
        (void)rivet::fs::remove_file(Path{path});

        const char* del = "DELETE FROM cache_entries WHERE key=?";
        sqlite3_stmt* del_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, del, -1, &del_stmt, nullptr) == 0) {
            sqlite3_bind_text(del_stmt, 1, key.c_str(), -1, nullptr);
            sqlite3_step(del_stmt);
            sqlite3_finalize(del_stmt);
        }
        ++evicted;
    }
    return evicted;
}

} // namespace rivet::cache
