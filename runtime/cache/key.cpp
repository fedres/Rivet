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
#elif defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "Bcrypt.lib")
#else
#  include <openssl/evp.h>
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

#elif defined(_WIN32)

static std::array<uint8_t, 32> sha256_raw(const uint8_t* data, size_t len) {
    std::array<uint8_t, 32> out{};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(len), 0);
            BCryptFinishHash(hash, out.data(), static_cast<ULONG>(out.size()), 0);
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return out;
}

#else // Linux and other POSIX

static std::array<uint8_t, 32> sha256_raw(const uint8_t* data, size_t len) {
    std::array<uint8_t, 32> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
        unsigned int out_len = static_cast<unsigned int>(out.size());
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, data, len);
        EVP_DigestFinal_ex(ctx, out.data(), &out_len);
        EVP_MD_CTX_free(ctx);
    }
    return out;
}

#endif

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
