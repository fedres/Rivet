// runtime/cache/key.hpp — Cache key derivation
//
// CacheKey = SHA-256 of:
//   tool_version | target_triple | flags_sorted | input_hashes_sorted |
//   defines_sorted | cxx_std
//
// Timestamps are NEVER included. Only content determines cache validity.
#pragma once

#include "../build/ir.hpp"
#include "../../platform/interface/result.hpp"
#include "../../platform/interface/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace rivet::cache {

// ─── Key material ────────────────────────────────────────────────────────────

struct KeyMaterial {
    std::string              tool_version;       // "clang-18.1.0"
    std::string              target_triple;      // "x86_64-linux-gnu"
    std::vector<std::string> flags;              // compile flags (will be sorted)
    std::vector<std::string> defines;            // -DFOO=BAR (will be sorted)
    std::vector<std::string> input_hashes;       // SHA-256 of each input file
    std::string              cxx_std;            // "c++23"
};

// ─── Hash helpers ─────────────────────────────────────────────────────────────

/// Compute SHA-256 of arbitrary bytes.
/// Returns a 64-char lowercase hex string.
[[nodiscard]] Result<std::string> sha256_bytes(std::span<const std::byte> data);

/// Compute SHA-256 of a string.
[[nodiscard]] Result<std::string> sha256_string(std::string_view s);

/// Compute SHA-256 of a file's contents.
[[nodiscard]] Result<std::string> sha256_file(const Path& p);

// ─── Key derivation ──────────────────────────────────────────────────────────

/// Derive a CacheKey from key material.
/// All string arrays are sorted before hashing for determinism.
[[nodiscard]] Result<build::CacheKey> derive_key(KeyMaterial mat);

/// Convenience: derive a key directly from a TaskNode.
/// Requires that all InputFile::content_hash fields have been populated.
[[nodiscard]] Result<build::CacheKey> derive_key(const build::TaskNode& task,
                                                   std::string_view tool_version,
                                                   std::string_view target_triple);

} // namespace rivet::cache
