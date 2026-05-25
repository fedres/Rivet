// platform/macos/fs.cpp — macOS filesystem backend
// Differences from Linux:
//   - Filesystem is case-INSENSITIVE by default (HFS+/APFS default volumes).
//   - Unicode normalization: macOS uses NFD, compare using canonical form.
//   - rename(2) is atomic (APFS guarantees).
//   - Symlinks work but may require specific sandbox permissions.
//   - FSEvents (or kqueue) for filesystem watching.
#include "../interface/fs.hpp"
#include "../interface/result.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <dirent.h>
#include <AvailabilityMacros.h>
#include <ftw.h>

namespace rivet::fs {

using rivet::propagate;

namespace {
    Error errno_error(std::string_view ctx) {
        return Error{std::string(ctx) + ": " + std::strerror(errno), errno};
    }
} // namespace

Result<bool> exists(const Path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) == 0) return true;
    if (errno == ENOENT || errno == ENOTDIR) return false;
    return make_error<bool>(std::string("stat: ") + std::strerror(errno), errno);
}

Result<FileStat> stat(const Path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0)
        return make_error<FileStat>(std::string("stat: ") + std::strerror(errno), errno);
    FileStat out{};
    out.size_bytes  = static_cast<uint64_t>(st.st_size);
    out.mtime_ns    = static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1'000'000'000LL
                    + st.st_mtimespec.tv_nsec;
    out.is_file     = S_ISREG(st.st_mode);
    out.is_dir      = S_ISDIR(st.st_mode);
    out.is_symlink  = false;
    out.permissions = st.st_mode & 0777;
    return out;
}

Result<Path> canonical(const Path& p) {
    char buf[PATH_MAX]{};
    if (::realpath(p.c_str(), buf) == nullptr)
        return make_error<Path>(std::string("realpath: ") + std::strerror(errno), errno);
    return Path{buf};
}

Result<Path> read_symlink(const Path& p) {
    char buf[PATH_MAX]{};
    ssize_t n = ::readlink(p.c_str(), buf, sizeof(buf) - 1);
    if (n < 0)
        return make_error<Path>(std::string("readlink: ") + std::strerror(errno), errno);
    buf[n] = '\0';
    return Path{buf};
}

Result<std::vector<Path>> list_dir(const Path& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return make_error<std::vector<Path>>(std::string("opendir: ") + std::strerror(errno), errno);
    std::vector<Path> result;
    errno = 0;
    while (dirent* e = ::readdir(d)) {
        std::string_view name{e->d_name};
        if (name == "." || name == "..") continue;
        result.push_back(dir / name);
    }
    int saved = errno; ::closedir(d);
    if (saved) return make_error<std::vector<Path>>(std::strerror(saved), saved);
    return result;
}

Result<void> create_dir(const Path& p) {
    if (::mkdir(p.c_str(), 0755) != 0 && errno != EEXIST)
        return make_error(std::string("mkdir: ") + std::strerror(errno), errno);
    return {};
}

Result<void> create_dirs(const Path& p) {
    auto parent = p.parent_path();
    if (!parent.empty() && parent != p) {
        auto r = rivet::fs::exists(parent);
        if (r && !*r) RIVET_TRY(create_dirs(parent));
    }
    return create_dir(p);
}

Result<void> remove_file(const Path& p) {
    if (::unlink(p.c_str()) != 0)
        return make_error(std::string("unlink: ") + std::strerror(errno), errno);
    return {};
}

Result<void> remove_dir(const Path& p) {
    if (::rmdir(p.c_str()) != 0)
        return make_error(std::string("rmdir: ") + std::strerror(errno), errno);
    return {};
}

Result<void> remove_all(const Path& p) {
    // Use nftw to walk the tree depth-first and remove files + dirs.
    std::string root = p.string();
    int rc = ::nftw(root.c_str(),
        [](const char* fpath, const struct stat* /*sb*/, int typeflag, struct FTW* /*ftwbuf*/) -> int {
            if (typeflag == FTW_DP || typeflag == FTW_D)
                return ::rmdir(fpath);
            return ::unlink(fpath);
        },
        64 /*nopenfd*/, FTW_DEPTH | FTW_PHYS);
    if (rc != 0)
        return make_error(std::string("remove_all: ") + std::strerror(errno), errno);
    return {};
}

Result<void> copy_file(const Path& from, const Path& to) {
    (void)from; (void)to;
    return make_error("copy_file: not yet implemented");
}

Result<void> rename_atomic(const Path& from, const Path& to) {
    if (::rename(from.c_str(), to.c_str()) != 0)
        return make_error(std::string("rename: ") + std::strerror(errno), errno);
    return {};
}

Result<void> create_symlink(const Path& target, const Path& link) {
    if (::symlink(target.c_str(), link.c_str()) != 0)
        return make_error(std::string("symlink: ") + std::strerror(errno), errno);
    return {};
}

// FileHandle — identical to Linux (same POSIX primitives)
FileHandle::~FileHandle() { if (handle_ > 0) ::close(handle_); }
FileHandle::FileHandle(FileHandle&& o) noexcept : handle_(o.handle_) { o.handle_ = -1; }
FileHandle& FileHandle::operator=(FileHandle&& o) noexcept {
    if (this != &o) { if (handle_ > 0) ::close(handle_); handle_ = o.handle_; o.handle_ = -1; }
    return *this;
}
bool FileHandle::is_valid() const { return handle_ >= 0; }
Result<size_t> FileHandle::read(MutByteSpan buf) {
    ssize_t n = ::read(handle_, buf.data(), buf.size());
    if (n < 0) return make_error<size_t>(std::strerror(errno), errno);
    return static_cast<size_t>(n);
}
Result<size_t> FileHandle::write(ByteSpan buf) {
    ssize_t n = ::write(handle_, buf.data(), buf.size());
    if (n < 0) return make_error<size_t>(std::strerror(errno), errno);
    return static_cast<size_t>(n);
}
Result<void> FileHandle::seek(int64_t offset, int whence) {
    if (::lseek(handle_, static_cast<off_t>(offset), whence) < 0)
        return make_error(std::strerror(errno), errno);
    return {};
}
Result<void> FileHandle::fsync() {
    // macOS: use F_FULLFSYNC for true durability (fdatasync is not enough on HFS+)
    if (::fcntl(handle_, F_FULLFSYNC) != 0)
        return make_error(std::string("fcntl(F_FULLFSYNC): ") + std::strerror(errno), errno);
    return {};
}
Result<FileHandle> open(const Path& p, OpenMode mode) {
    int flags = 0;
    bool rd = mode & OpenMode::Read, wr = mode & OpenMode::Write;
    if (rd && wr) flags = O_RDWR; else if (wr) flags = O_WRONLY; else flags = O_RDONLY;
    if (mode & OpenMode::Create)    flags |= O_CREAT;
    if (mode & OpenMode::Truncate)  flags |= O_TRUNC;
    if (mode & OpenMode::Append)    flags |= O_APPEND;
    if (mode & OpenMode::Exclusive) flags |= O_EXCL;
    int fd = ::open(p.c_str(), flags, 0644);
    if (fd < 0) return make_error<FileHandle>(std::strerror(errno), errno);
    return FileHandle{fd};
}
Result<std::vector<std::byte>> read_file(const Path& p) {
    auto fh = open(p, OpenMode::Read);
    if (!fh) return propagate<std::vector<std::byte>>(fh);
    auto s = stat(p);
    if (!s) return propagate<std::vector<std::byte>>(s);
    std::vector<std::byte> buf(s->size_bytes);
    size_t off = 0;
    while (off < buf.size()) {
        auto n = fh->read(MutByteSpan{buf.data() + off, buf.size() - off});
        if (!n) return propagate<std::vector<std::byte>>(n);
        if (*n == 0) break;
        off += *n;
    }
    buf.resize(off); return buf;
}
Result<void> write_atomic(const Path& dest, ByteSpan data) {
    auto tmp = temp_path_near(dest);
    auto fh  = open(tmp, OpenMode::Write | OpenMode::Create | OpenMode::Truncate);
    if (!fh) return rivet::propagate<void>(fh);
    size_t written = 0;
    while (written < data.size()) {
        auto n = fh->write(ByteSpan{data.data() + written, data.size() - written});
        if (!n) return rivet::propagate<void>(n);
        written += *n;
    }
    RIVET_TRY(fh->fsync());
    return rename_atomic(tmp, dest);
}
Result<void> lock_file(const FileHandle& fh, LockMode mode) {
    int op = (mode == LockMode::Shared) ? LOCK_SH : LOCK_EX;
    if (::flock(fh.native(), op) != 0) return make_error(std::strerror(errno), errno);
    return {};
}
Result<void> unlock_file(const FileHandle& fh) {
    if (::flock(fh.native(), LOCK_UN) != 0) return make_error(std::strerror(errno), errno);
    return {};
}

// macOS: case-insensitive by default (unless on case-sensitive APFS volume)
bool path_eq_icase(const Path& a, const Path& b) {
    auto sa = a.string(), sb = b.string();
    if (sa.size() != sb.size()) return false;
    for (size_t i = 0; i < sa.size(); ++i)
        if (std::tolower((unsigned char)sa[i]) != std::tolower((unsigned char)sb[i]))
            return false;
    return true;
}

Path relative_to(const Path& p, const Path& base) {
    auto rel = std::filesystem::relative(p, base);
    return rel.empty() ? p : rel;
}

Path temp_path_near(const Path& near) {
    return near.parent_path() / (near.filename().string() + ".rivet_tmp");
}

} // namespace rivet::fs
