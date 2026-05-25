// rivet/platform/interface/net.hpp
// Networking Platform Abstraction Layer.
//
// RULES:
//   - NEVER shell out to curl/wget. Ever.
//   - TLS is mandatory for all production endpoints.
//   - All downloads must be checksum-verified before the caller sees the file.
//   - Downloads must support HTTP Range resumption for large bundles.
//   - Retry with exponential backoff is built in — callers do not retry.
//
// Platform implementations:
//   platform/linux/net.cpp    (vendored mbedTLS + sockets)
//   platform/macos/net.cpp    (vendored mbedTLS + SecureTransport fallback)
//   platform/windows/net.cpp  (vendored mbedTLS + CertOpenSystemStore)
#pragma once

#include "result.hpp"
#include "types.hpp"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace rivet::net {

// ─── Types ───────────────────────────────────────────────────────────────────

using Headers    = std::unordered_map<std::string, std::string>;
using QueryParams = std::unordered_map<std::string, std::string>;

struct Url {
    std::string scheme;   // "https" or "http"
    std::string host;
    uint16_t    port = 0; // 0 = default for scheme
    std::string path;
    std::string query;

    static Result<Url> parse(std::string_view raw);
    [[nodiscard]] std::string to_string() const;
};

struct Response {
    int                          status_code;
    Headers                      headers;
    std::vector<std::byte>       body;

    [[nodiscard]] std::string    body_str() const;
    [[nodiscard]] bool           ok() const { return status_code >= 200 && status_code < 300; }
};

struct RequestOptions {
    Headers     headers;
    bool        verify_tls = true;
    int         timeout_ms = 30'000;
    int         max_retries = 3;       // exponential backoff, automatic
    bool        follow_redirects = true;
};

// Progress callback for downloads. Called with (bytes_done, total_bytes).
// total_bytes == 0 means Content-Length is unknown.
using ProgressCallback = std::function<void(uint64_t done, uint64_t total)>;

// ─── HTTP Client ─────────────────────────────────────────────────────────────

class HttpClient {
public:
    explicit HttpClient(std::string base_url, RequestOptions defaults = {});
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    [[nodiscard]] Result<Response> get(std::string_view path,
                                       Headers extra_headers = {});
    [[nodiscard]] Result<Response> post(std::string_view path,
                                        ByteSpan body,
                                        std::string_view content_type = "application/octet-stream",
                                        Headers extra_headers = {});
    [[nodiscard]] Result<Response> put(std::string_view path,
                                       ByteSpan body,
                                       std::string_view content_type = "application/octet-stream");
    [[nodiscard]] Result<Response> head(std::string_view path);

    // Download `path` to `dest` on disk.
    // - Validates SHA256 checksum if `expected_sha256` is provided.
    // - Supports HTTP Range resume if `dest` already partially exists.
    // - Writes atomically: final file appears only after full verified download.
    [[nodiscard]] Result<void> download(std::string_view path,
                                        const Path& dest,
                                        std::optional<std::string> expected_sha256 = {},
                                        ProgressCallback progress = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ─── Convenience free functions ──────────────────────────────────────────────

[[nodiscard]] Result<Response> http_get(const Url& url, RequestOptions opts = {});

[[nodiscard]] Result<void>     download_file(const Url& url,
                                             const Path& dest,
                                             std::optional<std::string> expected_sha256 = {},
                                             ProgressCallback progress = nullptr,
                                             RequestOptions opts = {});

// ─── TLS trust store ─────────────────────────────────────────────────────────

// Load the platform's system trust store for TLS certificate validation.
// Called once at startup; implementation is platform-specific.
[[nodiscard]] Result<void> init_tls_trust_store();

} // namespace rivet::net
