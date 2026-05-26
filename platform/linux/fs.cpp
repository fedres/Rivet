// platform/linux/fs.cpp — Linux filesystem backend
// Kernel targets: Linux 5.4+ (io_uring), graceful fallback to epoll.
// glibc target: 2.17 minimum for broad distro compatibility.
#include "../interface/fs.hpp"
#include "../interface/result.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ftw.h>
#include <sys/file.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <dirent.h>

namespace rivet::fs {

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
    out.mtime_ns    = static_cast<int64_t>(st.st_mtim.tv_sec) * 1'000'000'000LL
                    + st.st_mtim.tv_nsec;
    out.is_file     = S_ISREG(st.st_mode);
    out.is_dir      = S_ISDIR(st.st_mode);
    out.is_symlink  = false; // stat() follows symlinks; use lstat() if needed
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
    ssize_t len = ::readlink(p.c_str(), buf, sizeof(buf) - 1);
    if (len < 0)
        return make_error<Path>(std::string("readlink: ") + std::strerror(errno), errno);
    buf[len] = '\0';
    return Path{buf};
}

Result<std::vector<Path>> list_dir(const Path& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return make_error<std::vector<Path>>(std::string("opendir: ") + std::strerror(errno), errno);

    std::vector<Path> result;
    errno = 0;
    while (dirent* entry = ::readdir(d)) {
        std::string_view name{entry->d_name};
        if (name == "." || name == "..") continue;
        result.push_back(dir / name);
    }
    int saved = errno;
    ::closedir(d);
    if (saved != 0)
        return make_error<std::vector<Path>>(std::string("readdir: ") + std::strerror(saved), saved);
    return result;
}

Result<void> create_dir(const Path& p) {
    if (::mkdir(p.c_str(), 0755) != 0 && errno != EEXIST)
        return make_error(std::string("mkdir: ") + std::strerror(errno), errno);
    return {};
}

Result<void> create_dirs(const Path& p) {
    // Implemented via recursive parent creation.
    auto parent = p.parent_path();
    if (!parent.empty() && parent != p) {
        auto r = rivet::fs::exists(parent);
        if (r && !*r) {
            RIVET_TRY(create_dirs(parent));
        }
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
    int src = ::open(from.c_str(), O_RDONLY | O_CLOEXEC);
    if (src < 0)
        return make_error(std::string("copy_file open src: ") + std::strerror(errno), errno);

    struct stat st{};
    ::fstat(src, &st);

    int dst = ::open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                     st.st_mode & 0777);
    if (dst < 0) {
        ::close(src);
        return make_error(std::string("copy_file open dst: ") + std::strerror(errno), errno);
    }

    off_t remaining = st.st_size;
    off_t offset = 0;
    while (remaining > 0) {
        ssize_t n = ::sendfile(dst, src, &offset, static_cast<size_t>(remaining));
        if (n < 0) {
            int saved = errno;
            ::close(src); ::close(dst);
            return make_error(std::string("sendfile: ") + std::strerror(saved), saved);
        }
        remaining -= n;
    }
    ::close(src);
    ::close(dst);
    return {};
}

Result<void> rename_atomic(const Path& from, const Path& to) {
    // rename(2) is atomic on POSIX when from and to are on the same filesystem.
    if (::rename(from.c_str(), to.c_str()) != 0)
        return make_error(std::string("rename: ") + std::strerror(errno), errno);
    return {};
}

Result<void> create_symlink(const Path& target, const Path& link) {
    if (::symlink(target.c_str(), link.c_str()) != 0)
        return make_error(std::string("symlink: ") + std::strerror(errno), errno);
    return {};
}

// ─── FileHandle ──────────────────────────────────────────────────────────────

FileHandle::~FileHandle() {
    if (handle_ > 0) ::close(handle_);
}

FileHandle::FileHandle(FileHandle&& o) noexcept : handle_(o.handle_) {
    o.handle_ = -1;
}
FileHandle& FileHandle::operator=(FileHandle&& o) noexcept {
    if (this != &o) {
        if (handle_ > 0) ::close(handle_);
        handle_ = o.handle_;
        o.handle_ = -1;
    }
    return *this;
}

bool FileHandle::is_valid() const { return handle_ >= 0; }

Result<size_t> FileHandle::read(MutByteSpan buf) {
    ssize_t n = ::read(handle_, buf.data(), buf.size());
    if (n < 0) return make_error<size_t>(std::string("read: ") + std::strerror(errno), errno);
    return static_cast<size_t>(n);
}

Result<size_t> FileHandle::write(ByteSpan buf) {
    ssize_t n = ::write(handle_, buf.data(), buf.size());
    if (n < 0) return make_error<size_t>(std::string("write: ") + std::strerror(errno), errno);
    return static_cast<size_t>(n);
}

Result<void> FileHandle::seek(int64_t offset, int whence) {
    if (::lseek(handle_, static_cast<off_t>(offset), whence) < 0)
        return make_error(std::string("lseek: ") + std::strerror(errno), errno);
    return {};
}

Result<void> FileHandle::fsync() {
    if (::fsync(handle_) != 0)
        return make_error(std::string("fsync: ") + std::strerror(errno), errno);
    return {};
}

Result<FileHandle> open(const Path& p, OpenMode mode) {
    int flags = 0;
    bool rd = mode & OpenMode::Read;
    bool wr = mode & OpenMode::Write;
    if (rd && wr) flags = O_RDWR;
    else if (wr)  flags = O_WRONLY;
    else          flags = O_RDONLY;
    if (mode & OpenMode::Create)    flags |= O_CREAT;
    if (mode & OpenMode::Truncate)  flags |= O_TRUNC;
    if (mode & OpenMode::Append)    flags |= O_APPEND;
    if (mode & OpenMode::Exclusive) flags |= O_EXCL;

    int fd = ::open(p.c_str(), flags, 0644);
    if (fd < 0)
        return make_error<FileHandle>(std::string("open: ") + std::strerror(errno), errno);
    return FileHandle{fd};
}

Result<std::vector<std::byte>> read_file(const Path& p) {
    auto fh = open(p, OpenMode::Read);
    if (!fh) return propagate<std::vector<std::byte>>(fh);

    auto s = stat(p);
    if (!s) return propagate<std::vector<std::byte>>(s);

    std::vector<std::byte> buf(s->size_bytes);
    size_t offset = 0;
    while (offset < buf.size()) {
        auto n = fh->read(MutByteSpan{buf.data() + offset, buf.size() - offset});
        if (!n) return propagate<std::vector<std::byte>>(n);
        if (*n == 0) break;
        offset += *n;
    }
    buf.resize(offset);
    return buf;
}

Result<void> write_atomic(const Path& dest, ByteSpan data) {
    auto tmp = temp_path_near(dest);
    auto fh  = open(tmp, OpenMode::Write | OpenMode::Create | OpenMode::Truncate);
    if (!fh) return propagate<void>(fh);

    size_t written = 0;
    while (written < data.size()) {
        auto n = fh->write(ByteSpan{data.data() + written, data.size() - written});
        if (!n) return propagate<void>(n);
        written += *n;
    }
    RIVET_TRY(fh->fsync());
    return rename_atomic(tmp, dest);
}

// ─── File locking ─────────────────────────────────────────────────────────────

Result<void> lock_file(const FileHandle& fh, LockMode mode) {
    int op = (mode == LockMode::Shared) ? LOCK_SH : LOCK_EX;
    if (::flock(fh.native(), op) != 0)
        return make_error(std::string("flock: ") + std::strerror(errno), errno);
    return {};
}

Result<void> unlock_file(const FileHandle& fh) {
    if (::flock(fh.native(), LOCK_UN) != 0)
        return make_error(std::string("flock(LOCK_UN): ") + std::strerror(errno), errno);
    return {};
}

// ─── FileLock ─────────────────────────────────────────────────────────────────

FileLock::~FileLock() = default;
FileLock::FileLock(FileLock&&) noexcept = default;

Result<FileLock> FileLock::acquire(const Path& p, LockMode mode) {
    auto fh = open(p, OpenMode::Write | OpenMode::Create);
    if (!fh) return propagate<FileLock>(fh);
    auto r = lock_file(*fh, mode);
    if (!r) return propagate<FileLock>(r);
    FileLock lk;
    lk.fh_ = std::move(*fh);
    return lk;
}

// ─── Path utilities ───────────────────────────────────────────────────────────

bool path_eq_icase(const Path& a, const Path& b) {
    // Linux is case-sensitive: direct compare.
    return a == b;
}

Path relative_to(const Path& p, const Path& base) {
    auto rel = std::filesystem::relative(p, base);
    return rel.empty() ? p : rel;
}

Path temp_path_near(const Path& p) {
    return p.parent_path() / (p.filename().string() + ".rivet_tmp_"
        + std::to_string(static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count())));
}

} // namespace rivet::fs
