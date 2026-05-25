// runtime/cache/key.cpp — Cache key derivation implementation
#include "key.hpp"
#include "../../platform/interface/fs.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <sstream>

#if defined(__APPLE__)
#  include <CommonCrypto/CommonDigest.h>
#endif

namespace rivet::cache {

// ─── SHA-256 implementation ───────────────────────────────────────────────────

namespace detail {

static std::string to_hex(const uint8_t* bytes, size_t len) {
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
        s += std::format("{:02x}", bytes[i]);
    return s;
}

#if defined(__APPLE__)

static std::array<uint8_t, 32> sha256_raw(const uint8_t* data, size_t len) {
    std::array<uint8_t, 32> out{};
    CC_SHA256(data, static_cast<CC_LONG>(len), out.data());
    return out;
}

#else

// Portable SHA-256 (RFC 6234)
static constexpr uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9ca69,0xc67178f2,
};
static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static std::array<uint8_t, 32> sha256_raw(const uint8_t* data, size_t len) {
    uint32_t H[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,
    };
    // Build padded message in 64-byte blocks
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    size_t pad_len = len + 1;
    while (pad_len % 64 != 56) ++pad_len;
    pad_len += 8; // total length with 8-byte big-endian length suffix

    std::vector<uint8_t> msg(pad_len, 0);
    std::memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    for (int i = 0; i < 8; ++i)
        msg[pad_len - 8 + i] = static_cast<uint8_t>((bit_len >> (56 - 8*i)) & 0xFF);

    for (size_t off = 0; off < pad_len; off += 64) {
        uint32_t W[64];
        for (int i = 0; i < 16; ++i)
            W[i] = (uint32_t(msg[off+i*4])<<24)|(uint32_t(msg[off+i*4+1])<<16)
                  |(uint32_t(msg[off+i*4+2])<<8)|uint32_t(msg[off+i*4+3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr32(W[i-15],7)^rotr32(W[i-15],18)^(W[i-15]>>3);
            uint32_t s1 = rotr32(W[i-2],17)^rotr32(W[i-2],19)^(W[i-2]>>10);
            W[i] = W[i-16]+s0+W[i-7]+s1;
        }
        uint32_t a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1   = rotr32(e,6)^rotr32(e,11)^rotr32(e,25);
            uint32_t ch   = (e&f)^(~e&g);
            uint32_t temp1= h+S1+ch+K[i]+W[i];
            uint32_t S0   = rotr32(a,2)^rotr32(a,13)^rotr32(a,22);
            uint32_t maj  = (a&b)^(a&c)^(b&c);
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+S0+maj;
        }
        H[0]+=a; H[1]+=b; H[2]+=c; H[3]+=d;
        H[4]+=e; H[5]+=f; H[6]+=g; H[7]+=h;
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i*4+0]=(H[i]>>24)&0xFF; out[i*4+1]=(H[i]>>16)&0xFF;
        out[i*4+2]=(H[i]>> 8)&0xFF; out[i*4+3]= H[i]     &0xFF;
    }
    return out;
}
#endif // __APPLE__

} // namespace detail

// ─── Public API ──────────────────────────────────────────────────────────────

Result<std::string> sha256_bytes(std::span<const std::byte> data) {
    auto raw = detail::sha256_raw(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return detail::to_hex(raw.data(), raw.size());
}

Result<std::string> sha256_string(std::string_view s) {
    auto raw = detail::sha256_raw(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    return detail::to_hex(raw.data(), raw.size());
}

Result<std::string> sha256_file(const Path& p) {
    auto data = rivet::fs::read_file(p);
    if (!data) return propagate<std::string>(data);
    return sha256_bytes(*data);
}

Result<build::CacheKey> derive_key(KeyMaterial mat) {
    // Sort all arrays for determinism.
    std::sort(mat.flags.begin(), mat.flags.end());
    std::sort(mat.defines.begin(), mat.defines.end());
    std::sort(mat.input_hashes.begin(), mat.input_hashes.end());

    // Concatenate all key material separated by NUL bytes.
    std::string blob;
    auto append = [&](std::string_view s) {
        blob += s;
        blob += '\0';
    };
    append(mat.tool_version);
    append(mat.target_triple);
    append(mat.cxx_std);
    for (const auto& f : mat.flags)         append(f);
    for (const auto& d : mat.defines)       append(d);
    for (const auto& h : mat.input_hashes)  append(h);

    auto hex = sha256_string(blob);
    if (!hex) return propagate<build::CacheKey>(hex);
    return build::CacheKey{ *hex };
}

Result<build::CacheKey> derive_key(const build::TaskNode& task,
                                    std::string_view tool_version,
                                    std::string_view target_triple) {
    KeyMaterial mat;
    mat.tool_version  = std::string(tool_version);
    mat.target_triple = std::string(target_triple);
    mat.cxx_std       = "c++23";

    if (task.command) {
        mat.flags = task.command->args;
    }

    for (const auto& inp : task.inputs) {
        if (!inp.content_hash.empty())
            mat.input_hashes.push_back(inp.content_hash);
    }

    return derive_key(std::move(mat));
}

} // namespace rivet::cache
