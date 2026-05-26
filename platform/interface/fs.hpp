// rivet/platform/interface/fs.hpp
// Filesystem Platform Abstraction Layer.
//
// RULES:
//   - All paths are rivet::Path (std::filesystem::path).
//   - All operations return Result<T> — no exceptions, no errno leaking.
//   - Never call std::filesystem directly outside platform backends.
//   - Atomic rename is the only safe way to publish a new file.
//   - Assume case sensitivity is unknown — use canonical() for comparisons.
//
// Platform implementations:
//   platform/linux/fs.cpp
//   platform/macos/fs.cpp
//   platform/windows/fs.cpp
#pragma once

#include "result.hpp"
#include "types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rivet::fs {

// ─── Queries ─────────────────────────────────────────────────────────────────

[[nodiscard]] Result<bool>     exists(const Path& p);
[[nodiscard]] Result<FileStat> stat(const Path& p);
[[nodiscard]] Result<Path>     canonical(const Path& p);
[[nodiscard]] Result<Path>     read_symlink(const Path& p);
[[nodiscard]] Result<std::vector<Path>> list_dir(const Path& dir);

// ─── Mutations ───────────────────────────────────────────────────────────────

[[nodiscard]] Result<void> create_dir(const Path& p);
[[nodiscard]] Result<void> create_dirs(const Path& p);   // mkdir -p
[[nodiscard]] Result<void> remove_file(const Path& p);
[[nodiscard]] Result<void> remove_dir(const Path& p);    // must be empty
[[nodiscard]] Result<void> remove_all(const Path& p);    // recursive
[[nodiscard]] Result<void> copy_file(const Path& from, const Path& to);

// Atomic rename — the ONLY safe way to publish a file.
// On POSIX: rename(2). On Windows: MoveFileExW with MOVEFILE_REPLACE_EXISTING.
// Both from and to must be on the same filesystem.
[[nodiscard]] Result<void> rename_atomic(const Path& from, const Path& to);

// Create a symlink at `link` pointing to `target`.
// On Windows: requires Developer Mode or falls back to junctions/hardlinks.
[[nodiscard]] Result<void> create_symlink(const Path& target, const Path& link);

// ─── File I/O ────────────────────────────────────────────────────────────────

// RAII file handle — closed on destruction.
class FileHandle {
public:
    FileHandle() = default;
    explicit FileHandle(NativeHandle h) : handle_(h) {}
    ~FileHandle();

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    FileHandle(FileHandle&&) noexcept;
    FileHandle& operator=(FileHandle&&) noexcept;

    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] NativeHandle native() const { return handle_; }

    [[nodiscard]] Result<size_t> read(MutByteSpan buf);
    [[nodiscard]] Result<size_t> write(ByteSpan buf);
    [[nodiscard]] Result<void>   seek(int64_t offset, int whence = 0);
    [[nodiscard]] Result<void>   fsync();  // flush to disk (required before atomic rename)

private:
    NativeHandle handle_{};
};

[[nodiscard]] Result<FileHandle> open(const Path& p, OpenMode mode);

// Convenience: read entire file into a vector.
[[nodiscard]] Result<std::vector<std::byte>> read_file(const Path& p);

// Atomic write helper: writes to a temp file, fsyncs, then renames atomically.
[[nodiscard]] Result<void> write_atomic(const Path& dest, ByteSpan data);

// ─── File Locking ────────────────────────────────────────────────────────────

[[nodiscard]] Result<void> lock_file(const FileHandle& fh, LockMode mode);
[[nodiscard]] Result<void> unlock_file(const FileHandle& fh);

// RAII file lock.
class FileLock {
public:
    static Result<FileLock> acquire(const Path& p, LockMode mode);
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock(FileLock&&) noexcept;
private:
    FileLock() = default;
    FileHandle fh_;
};

// ─── Filesystem Watching ─────────────────────────────────────────────────────

enum class WatchEvent { Created, Modified, Deleted, Renamed };

struct WatchChange {
    Path       path;
    WatchEvent event;
};

using WatchCallback = std::function<void(const WatchChange&)>;

class Watcher {
public:
    static Result<std::unique_ptr<Watcher>> create(const Path& dir,
                                                    WatchCallback cb);
    virtual ~Watcher() = default;
    virtual Result<void> start() = 0;
    virtual void         stop()  = 0;
};

// ─── Path Utilities ──────────────────────────────────────────────────────────

// Case-insensitive path equality. Do NOT use == for paths if case sensitivity
// might differ between platforms.
[[nodiscard]] bool path_eq_icase(const Path& a, const Path& b);

// Make `p` relative to `base`. Returns p unchanged if not under base.
[[nodiscard]] Path relative_to(const Path& p, const Path& base);

// Return a unique temp path in the same directory as near,
// suitable for atomic write-then-rename patterns.
[[nodiscard]] Path temp_path_near(const Path& p);

} // namespace rivet::fs
