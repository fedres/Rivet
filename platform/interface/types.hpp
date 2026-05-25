// rivet/platform/interface/types.hpp
// Common portable types used throughout the PAL and runtime.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstddef>

namespace rivet {

// Canonical path type. Always use this — never raw std::string for paths.
// std::filesystem::path handles wide strings on Windows transparently.
using Path = std::filesystem::path;

// Non-owning byte span.
#include <span>
using ByteSpan = std::span<const std::byte>;
using MutByteSpan = std::span<std::byte>;

// Platform opaque handle types.
#if defined(_WIN32)
    #include <windows.h>
    using NativeHandle = HANDLE;
    using NativePid    = DWORD;
#else
    #include <unistd.h>
    using NativeHandle = int;   // file descriptor
    using NativePid    = pid_t;
#endif

// Terminal size.
struct TermSize {
    uint16_t cols;
    uint16_t rows;
};

// File stat information (portable subset).
struct FileStat {
    uint64_t size_bytes;
    int64_t  mtime_ns;    // modification time, nanoseconds since epoch
    bool     is_file;
    bool     is_dir;
    bool     is_symlink;
    uint32_t permissions; // Unix permission bits; approximated on Windows
};

// Open mode flags.
enum class OpenMode : uint32_t {
    Read      = 1 << 0,
    Write     = 1 << 1,
    Append    = 1 << 2,
    Create    = 1 << 3,
    Truncate  = 1 << 4,
    Exclusive = 1 << 5,
};

inline OpenMode operator|(OpenMode a, OpenMode b) {
    return static_cast<OpenMode>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool operator&(OpenMode a, OpenMode b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// Lock mode for file locking.
enum class LockMode { Shared, Exclusive };

// Signal abstraction.
enum class Signal {
    Terminate,  // SIGTERM / TerminateProcess
    Kill,       // SIGKILL / TerminateProcess (forceful)
    Interrupt,  // SIGINT  / Ctrl+C event
};

} // namespace rivet
