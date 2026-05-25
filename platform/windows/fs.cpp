// platform/windows/fs.cpp — Windows filesystem backend
//
// Key differences from POSIX:
//   - Atomic rename: MoveFileExW with MOVEFILE_REPLACE_EXISTING.
//   - Long paths: use \\?\ prefix for paths > MAX_PATH (260 chars).
//   - Symlinks require Developer Mode or elevated privilege; fall back to junctions.
//   - Case-insensitive by default (NTFS).
//   - fsync equivalent: FlushFileBuffers.
//   - Unicode: all paths are UTF-16 internally; convert via MultiByteToWideChar.
//   - File locking: LockFileEx (mandatory, unlike POSIX advisory locks).
#include "../interface/fs.hpp"
#include "../interface/result.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fileapi.h>
#include <shlwapi.h>

namespace rivet::fs {

namespace {
    // Convert UTF-8 Path to wide string for Windows API calls.
    std::wstring to_wide(const Path& p) {
        auto s = p.wstring();
        // Prepend \\?\ to support long paths when LongPathsEnabled is not set.
        if (s.size() >= MAX_PATH && s.substr(0, 4) != L"\\\\?\\")
            return L"\\\\?\\" + s;
        return s;
    }

    Error win32_error(std::string_view ctx, DWORD code = ::GetLastError()) {
        char buf[256]{};
        ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         nullptr, code, 0, buf, sizeof(buf), nullptr);
        return Error{std::string(ctx) + ": " + buf, static_cast<int>(code)};
    }
} // namespace

Result<bool> exists(const Path& p) {
    DWORD attr = ::GetFileAttributesW(to_wide(p).c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return true;
    DWORD e = ::GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return false;
    return make_error<bool>("GetFileAttributesW", e);
}

Result<FileStat> stat(const Path& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!::GetFileAttributesExW(to_wide(p).c_str(), GetFileExInfoStandard, &d))
        return make_error<FileStat>("GetFileAttributesExW");
    FileStat out{};
    ULARGE_INTEGER size{.LowPart = d.nFileSizeLow, .HighPart = d.nFileSizeHigh};
    out.size_bytes = size.QuadPart;
    ULARGE_INTEGER mtime{.LowPart = d.ftLastWriteTime.dwLowDateTime,
                         .HighPart = d.ftLastWriteTime.dwHighDateTime};
    // FILETIME is 100-ns intervals since 1601-01-01; convert to ns since epoch.
    out.mtime_ns = static_cast<int64_t>((mtime.QuadPart - 116444736000000000ULL) * 100LL);
    out.is_dir  = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out.is_file = !out.is_dir;
    out.is_symlink = (d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    out.permissions = 0644; // approximation
    return out;
}

Result<Path> canonical(const Path& p) {
    wchar_t buf[32768]{};
    DWORD n = ::GetFullPathNameW(to_wide(p).c_str(), 32768, buf, nullptr);
    if (n == 0) return make_error<Path>("GetFullPathNameW");
    return Path{buf};
}

Result<Path> read_symlink(const Path& p) {
    // TODO: use DeviceIoControl / FSCTL_GET_REPARSE_POINT
    (void)p;
    return make_error<Path>("read_symlink: not yet implemented");
}

Result<std::vector<Path>> list_dir(const Path& dir) {
    auto pattern = to_wide(dir) + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return make_error<std::vector<Path>>("FindFirstFileW");
    std::vector<Path> result;
    do {
        std::wstring name{fd.cFileName};
        if (name == L"." || name == L"..") continue;
        result.push_back(dir / Path{name});
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return result;
}

Result<void> create_dir(const Path& p) {
    if (!::CreateDirectoryW(to_wide(p).c_str(), nullptr)) {
        DWORD e = ::GetLastError();
        if (e != ERROR_ALREADY_EXISTS) return make_error("CreateDirectoryW", e);
    }
    return {};
}

Result<void> create_dirs(const Path& p) {
    auto parent = p.parent_path();
    if (!parent.empty() && parent != p) {
        auto r = exists(parent);
        if (r && !*r) RIVET_TRY(create_dirs(parent));
    }
    return create_dir(p);
}

Result<void> remove_file(const Path& p) {
    if (!::DeleteFileW(to_wide(p).c_str()))
        return make_error("DeleteFileW");
    return {};
}

Result<void> remove_dir(const Path& p) {
    if (!::RemoveDirectoryW(to_wide(p).c_str()))
        return make_error("RemoveDirectoryW");
    return {};
}

Result<void> remove_all(const Path& p) {
    (void)p; return make_error("remove_all: not yet implemented");
}

Result<void> copy_file(const Path& from, const Path& to) {
    if (!::CopyFileW(to_wide(from).c_str(), to_wide(to).c_str(), FALSE))
        return make_error("CopyFileW");
    return {};
}

Result<void> rename_atomic(const Path& from, const Path& to) {
    if (!::MoveFileExW(to_wide(from).c_str(), to_wide(to).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return make_error("MoveFileExW");
    return {};
}

Result<void> create_symlink(const Path& target, const Path& link) {
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (::CreateSymbolicLinkW(to_wide(link).c_str(), to_wide(target).c_str(), flags))
        return {};
    // Fallback for directories: try junction (TODO).
    return make_error("CreateSymbolicLinkW: requires Developer Mode");
}

// ─── FileHandle ───────────────────────────────────────────────────────────────

FileHandle::~FileHandle() {
    if (handle_ != INVALID_HANDLE_VALUE && handle_) ::CloseHandle(handle_);
}
FileHandle::FileHandle(FileHandle&& o) noexcept : handle_(o.handle_) {
    o.handle_ = INVALID_HANDLE_VALUE;
}
FileHandle& FileHandle::operator=(FileHandle&& o) noexcept {
    if (this != &o) {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_);
        handle_ = o.handle_; o.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
}
bool FileHandle::is_valid() const {
    return handle_ && handle_ != INVALID_HANDLE_VALUE;
}
Result<size_t> FileHandle::read(MutByteSpan buf) {
    DWORD n;
    if (!::ReadFile(handle_, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr))
        return make_error<size_t>("ReadFile");
    return static_cast<size_t>(n);
}
Result<size_t> FileHandle::write(ByteSpan buf) {
    DWORD n;
    if (!::WriteFile(handle_, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr))
        return make_error<size_t>("WriteFile");
    return static_cast<size_t>(n);
}
Result<void> FileHandle::seek(int64_t offset, int whence) {
    LARGE_INTEGER li{.QuadPart = offset};
    DWORD method = (whence == 0) ? FILE_BEGIN : (whence == 1) ? FILE_CURRENT : FILE_END;
    if (!::SetFilePointerEx(handle_, li, nullptr, method))
        return make_error("SetFilePointerEx");
    return {};
}
Result<void> FileHandle::fsync() {
    if (!::FlushFileBuffers(handle_)) return make_error("FlushFileBuffers");
    return {};
}

Result<FileHandle> open(const Path& p, OpenMode mode) {
    DWORD access  = 0;
    DWORD creat   = OPEN_EXISTING;
    if (mode & OpenMode::Read)  access |= GENERIC_READ;
    if (mode & OpenMode::Write) access |= GENERIC_WRITE;
    if (mode & OpenMode::Create) {
        creat = (mode & OpenMode::Exclusive) ? CREATE_NEW : CREATE_ALWAYS;
    }
    if (mode & OpenMode::Truncate && !(mode & OpenMode::Create))
        creat = TRUNCATE_EXISTING;
    HANDLE h = ::CreateFileW(to_wide(p).c_str(), access,
                              FILE_SHARE_READ, nullptr, creat,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return make_error<FileHandle>("CreateFileW");
    return FileHandle{h};
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
    if (!fh) return propagate(fh);
    size_t w = 0;
    while (w < data.size()) {
        auto n = fh->write(ByteSpan{data.data() + w, data.size() - w});
        if (!n) return propagate(n);
        w += *n;
    }
    RIVET_TRY(fh->fsync());
    return rename_atomic(tmp, dest);
}

Result<void> lock_file(const FileHandle& fh, LockMode mode) {
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY |
                  (mode == LockMode::Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0);
    OVERLAPPED ov{};
    if (!::LockFileEx(fh.native(), flags, 0, MAXDWORD, MAXDWORD, &ov))
        return make_error("LockFileEx");
    return {};
}

Result<void> unlock_file(const FileHandle& fh) {
    OVERLAPPED ov{};
    if (!::UnlockFileEx(fh.native(), 0, MAXDWORD, MAXDWORD, &ov))
        return make_error("UnlockFileEx");
    return {};
}

// Windows: always case-insensitive
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
