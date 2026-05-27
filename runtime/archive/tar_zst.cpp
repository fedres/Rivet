// runtime/archive/tar_zst.cpp — in-process tar.zst extraction
#include "tar_zst.hpp"

#include "../../platform/interface/fs.hpp"

#include <zstd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#  include <sys/stat.h>
#endif

namespace rivet::archive {

namespace {

constexpr size_t TAR_BLOCK = 512;

// POSIX-1003.1-1990 ustar header.
struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};
static_assert(sizeof(TarHeader) == TAR_BLOCK, "tar header size");

// Parse an octal field; tar fields are space- or NUL-terminated.
uint64_t parse_octal(const char* p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        char c = p[i];
        if (c < '0' || c > '7') break;
        v = (v << 3) | static_cast<uint64_t>(c - '0');
    }
    return v;
}

bool is_all_zero(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (p[i]) return false;
    return true;
}

// Combine ustar's name[100] + prefix[155] back into a single path.
std::string combine_path(const TarHeader& h) {
    auto fixed = [](const char* p, size_t cap) -> std::string {
        size_t n = ::strnlen(p, cap);
        return std::string(p, n);
    };
    std::string name = fixed(h.name, sizeof(h.name));
    std::string prefix = fixed(h.prefix, sizeof(h.prefix));
    if (prefix.empty()) return name;
    if (!prefix.empty() && prefix.back() == '/') return prefix + name;
    return prefix + "/" + name;
}

// Drop the first `n` slash-separated path components.
std::string strip_components(std::string_view path, int n) {
    if (n <= 0) return std::string(path);
    int stripped = 0;
    size_t pos = 0;
    while (stripped < n) {
        size_t slash = path.find('/', pos);
        if (slash == std::string_view::npos) {
            // Not enough components — drop the whole entry by returning empty.
            return {};
        }
        pos = slash + 1;
        ++stripped;
    }
    return std::string(path.substr(pos));
}

// Streaming reader that joins a libzstd decompress stream with a
// fixed-size internal output buffer the tar parser can consume.
class ZstdReader {
public:
    explicit ZstdReader(const Path& archive) {
        // Open input file with stdio (portable on Windows via _wfopen
        // path that std::filesystem::path::c_str() yields on each OS).
#if defined(_WIN32)
        in_ = ::_wfopen(archive.c_str(), L"rb");
#else
        in_ = std::fopen(archive.c_str(), "rb");
#endif
        if (!in_) return;
        ctx_ = ZSTD_createDStream();
        if (!ctx_) return;
        ZSTD_initDStream(ctx_);
        in_buf_.resize(ZSTD_DStreamInSize());
        out_buf_.resize(ZSTD_DStreamOutSize());
        out_used_ = 0;
        out_filled_ = 0;
        in_filled_ = 0;
        in_consumed_ = 0;
    }

    ~ZstdReader() {
        if (ctx_) ZSTD_freeDStream(ctx_);
        if (in_) std::fclose(in_);
    }

    bool ok() const { return in_ != nullptr && ctx_ != nullptr; }

    // Read `want` bytes into `dst`. Returns the number actually read;
    // < want at EOF.
    size_t read(uint8_t* dst, size_t want) {
        size_t got = 0;
        while (got < want) {
            // Refill out_buf_ if exhausted.
            if (out_used_ >= out_filled_) {
                if (!refill_output()) break;
            }
            size_t avail = out_filled_ - out_used_;
            size_t take = std::min(avail, want - got);
            std::memcpy(dst + got, out_buf_.data() + out_used_, take);
            out_used_ += take;
            got += take;
        }
        return got;
    }

    // Skip `n` bytes without copying them.
    void skip(size_t n) {
        std::vector<uint8_t> sink(std::min<size_t>(n, 64 * 1024));
        while (n > 0) {
            size_t take = std::min<size_t>(n, sink.size());
            size_t got = read(sink.data(), take);
            if (got == 0) break;
            n -= got;
        }
    }

    std::string last_error() const { return last_err_; }

private:
    bool refill_output() {
        out_used_ = 0;
        out_filled_ = 0;

        while (true) {
            // Pull more compressed bytes from disk if our input buffer is dry.
            if (in_consumed_ >= in_filled_) {
                size_t r = std::fread(in_buf_.data(), 1, in_buf_.size(), in_);
                if (r == 0) {
                    if (std::feof(in_)) return false;
                    last_err_ = "tar.zst: read error";
                    return false;
                }
                in_filled_ = r;
                in_consumed_ = 0;
            }

            ZSTD_inBuffer in{in_buf_.data() + in_consumed_,
                             in_filled_ - in_consumed_, 0};
            ZSTD_outBuffer out{out_buf_.data(), out_buf_.size(), 0};
            size_t rc = ZSTD_decompressStream(ctx_, &out, &in);
            in_consumed_ += in.pos;
            out_filled_  = out.pos;

            if (ZSTD_isError(rc)) {
                last_err_ = std::string("zstd: ") + ZSTD_getErrorName(rc);
                return false;
            }
            if (out_filled_ > 0) return true;
            // Otherwise loop to pull more compressed bytes.
        }
    }

    std::FILE*           in_ = nullptr;
    ZSTD_DStream*        ctx_ = nullptr;
    std::vector<uint8_t> in_buf_;
    std::vector<uint8_t> out_buf_;
    size_t               in_filled_   = 0;
    size_t               in_consumed_ = 0;
    size_t               out_filled_  = 0;
    size_t               out_used_    = 0;
    std::string          last_err_;
};

// Set the executable bit on a file we just wrote, if the tar mode said so.
void apply_exec_bit(const Path& p, uint64_t mode) {
#if defined(_WIN32)
    (void)p; (void)mode;
#else
    if (mode & 0111) {
        ::chmod(p.string().c_str(), 0755);
    } else {
        ::chmod(p.string().c_str(), 0644);
    }
#endif
}

} // namespace

Result<void> extract_tar_zst(const Path& archive,
                              const Path& dest_dir,
                              ExtractOptions opts) {
    ZstdReader reader{archive};
    if (!reader.ok())
        return make_error("tar.zst: cannot open " + archive.string());

    TarHeader hdr;
    int zero_blocks = 0;

    while (true) {
        size_t got = reader.read(reinterpret_cast<uint8_t*>(&hdr), TAR_BLOCK);
        if (got == 0) break;
        if (got != TAR_BLOCK) {
            // Truncated archive — tolerate the trailing partial block
            // (some tar producers don't pad), treat as EOF.
            break;
        }

        if (is_all_zero(reinterpret_cast<const uint8_t*>(&hdr), TAR_BLOCK)) {
            // Two consecutive zero blocks mark end-of-archive.
            if (++zero_blocks >= 2) break;
            continue;
        }
        zero_blocks = 0;

        uint64_t size = parse_octal(hdr.size, sizeof(hdr.size));
        uint64_t mode = parse_octal(hdr.mode, sizeof(hdr.mode));
        std::string raw_path = combine_path(hdr);
        std::string out_path = strip_components(raw_path, opts.strip_components);

        // Number of full data blocks to consume regardless of what we
        // decide to do with the entry — keeps the stream aligned.
        uint64_t blocks  = (size + TAR_BLOCK - 1) / TAR_BLOCK;
        uint64_t padding = blocks * TAR_BLOCK - size;

        // Skip entries we don't represent: hardlinks (already
        // dereferenced upstream via cp -aL), symlinks, devices, pax
        // headers, and entries stripped to nothing by strip_components.
        char tf = hdr.typeflag ? hdr.typeflag : '0';
        bool is_dir  = (tf == '5');
        bool is_file = (tf == '0' || tf == '\0' || tf == '7');
        if (out_path.empty() || (!is_dir && !is_file)) {
            reader.skip(size);
            reader.skip(padding);
            continue;
        }

        Path full = dest_dir / out_path;

        if (is_dir) {
            // Trim trailing slash from the relative path before creating.
            (void)rivet::fs::create_dirs(full);
            reader.skip(size);
            reader.skip(padding);
            continue;
        }

        // Regular file: ensure parent dir, then stream `size` bytes to disk.
        if (auto parent = full.parent_path(); !parent.empty())
            (void)rivet::fs::create_dirs(parent);

#if defined(_WIN32)
        std::FILE* out = ::_wfopen(full.c_str(), L"wb");
#else
        std::FILE* out = std::fopen(full.c_str(), "wb");
#endif
        if (!out) {
            // Couldn't open — drain bytes anyway to stay aligned, then surface.
            reader.skip(size);
            reader.skip(padding);
            return make_error("tar.zst: cannot create " + full.string());
        }

        std::vector<uint8_t> buf(64 * 1024);
        uint64_t remaining = size;
        while (remaining > 0) {
            size_t take = static_cast<size_t>(std::min<uint64_t>(remaining, buf.size()));
            size_t r = reader.read(buf.data(), take);
            if (r == 0) {
                std::fclose(out);
                std::string err = reader.last_error();
                return make_error("tar.zst: short read on " + full.string()
                                  + (err.empty() ? "" : " (" + err + ")"));
            }
            if (std::fwrite(buf.data(), 1, r, out) != r) {
                std::fclose(out);
                return make_error("tar.zst: write failed on " + full.string());
            }
            remaining -= r;
        }
        std::fclose(out);

        apply_exec_bit(full, mode);
        reader.skip(padding);
    }

    return {};
}

} // namespace rivet::archive
