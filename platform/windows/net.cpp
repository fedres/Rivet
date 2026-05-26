// platform/windows/net.cpp — Windows networking backend
//
// HTTP/HTTPS: WinHTTP — Windows-native client. Handles TLS, system CA store
//             (Crypt32 / "ROOT" cert store), proxies, redirects, decompression.
// SHA-256:    BCrypt (already linked for runtime/cache/key.cpp).
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include "../interface/net.hpp"
#include "../interface/fs.hpp"
#include "../common/http_impl.hpp"

#include <array>
#include <cstdio>

namespace rivet::net {

using namespace detail;

// ─── Helpers ─────────────────────────────────────────────────────────────────

namespace {

std::wstring to_wide(std::string_view s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                          out.data(), n);
    return out;
}

std::string from_wide(const wchar_t* s, size_t n) {
    if (n == 0 || s == nullptr) return {};
    int m = ::WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(m), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(n),
                          out.data(), m, nullptr, nullptr);
    return out;
}

std::string win_error(std::string_view ctx, DWORD code) {
    return std::string(ctx) + ": Windows error " + std::to_string(code);
}

// SHA-256 via BCrypt — avoids depending on the portable RFC 6234 impl in
// http_impl.hpp which had divergence under some compilers.
std::string sha256_hex_bcrypt(const std::vector<std::byte>& data) {
    std::array<uint8_t, 32> out{};
    BCRYPT_ALG_HANDLE  alg  = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
            ::BCryptHashData(hash,
                const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data())),
                static_cast<ULONG>(data.size()), 0);
            ::BCryptFinishHash(hash, out.data(),
                static_cast<ULONG>(out.size()), 0);
            ::BCryptDestroyHash(hash);
        }
        ::BCryptCloseAlgorithmProvider(alg, 0);
    }
    char hex[65]{};
    for (size_t i = 0; i < out.size(); ++i)
        std::snprintf(hex + i*2, 3, "%02x", out[i]);
    return std::string(hex);
}

// RAII wrapper around HINTERNET.
struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() { if (h) ::WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

struct RawRequest {
    std::string method;
    std::string host;
    uint16_t    port = 0;
    bool        tls  = false;
    std::string path;     // includes query string
    Headers     headers;
    std::vector<std::byte> body;
};

Headers parse_raw_headers(const std::wstring& block) {
    Headers out;
    std::string utf8 = from_wide(block.data(), block.size());
    auto pos = utf8.find("\r\n");
    if (pos == std::string::npos) return out;
    std::string_view rest = std::string_view(utf8).substr(pos + 2);
    while (!rest.empty()) {
        auto nl = rest.find("\r\n");
        std::string_view line = (nl == std::string_view::npos) ? rest : rest.substr(0, nl);
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string key{line.substr(0, colon)};
            for (auto& c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string_view val = line.substr(colon + 1);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
                val.remove_prefix(1);
            out.emplace(std::move(key), std::string(val));
        }
        if (nl == std::string_view::npos) break;
        rest = rest.substr(nl + 2);
    }
    return out;
}

Result<Response> do_request_winhttp(const RawRequest& req,
                                    bool follow_redirects,
                                    ProgressCallback progress) {
    WinHttpHandle session(::WinHttpOpen(
        L"rivet/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        return make_error<Response>(win_error("WinHttpOpen", ::GetLastError()));

    WinHttpHandle conn(::WinHttpConnect(
        session, to_wide(req.host).c_str(), req.port, 0));
    if (!conn)
        return make_error<Response>(win_error("WinHttpConnect", ::GetLastError()));

    DWORD flags = req.tls ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle hreq(::WinHttpOpenRequest(
        conn,
        to_wide(req.method).c_str(),
        to_wide(req.path).c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (!hreq)
        return make_error<Response>(win_error("WinHttpOpenRequest", ::GetLastError()));

    DWORD redirect_policy = follow_redirects
        ? WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
        : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    ::WinHttpSetOption(hreq, WINHTTP_OPTION_REDIRECT_POLICY,
                       &redirect_policy, sizeof(redirect_policy));

    if (!req.headers.empty()) {
        std::wstring hdr_block;
        for (const auto& [k, v] : req.headers) {
            hdr_block += to_wide(k);
            hdr_block += L": ";
            hdr_block += to_wide(v);
            hdr_block += L"\r\n";
        }
        ::WinHttpAddRequestHeaders(hreq, hdr_block.c_str(),
            static_cast<DWORD>(hdr_block.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    LPVOID body_ptr = req.body.empty()
        ? WINHTTP_NO_REQUEST_DATA
        : const_cast<std::byte*>(req.body.data());

    if (!::WinHttpSendRequest(hreq,
            WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            body_ptr,
            static_cast<DWORD>(req.body.size()),
            static_cast<DWORD>(req.body.size()),
            0))
        return make_error<Response>(win_error("WinHttpSendRequest", ::GetLastError()));

    if (!::WinHttpReceiveResponse(hreq, nullptr))
        return make_error<Response>(win_error("WinHttpReceiveResponse", ::GetLastError()));

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!::WinHttpQueryHeaders(hreq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX))
        return make_error<Response>(win_error("WinHttpQueryHeaders status",
            ::GetLastError()));

    // Raw header block (CRLF separated). First call sizes the buffer.
    DWORD hdr_bytes = 0;
    ::WinHttpQueryHeaders(hreq,
        WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &hdr_bytes, WINHTTP_NO_HEADER_INDEX);
    std::wstring hdr_block;
    if (hdr_bytes > 0) {
        hdr_block.resize(hdr_bytes / sizeof(wchar_t));
        DWORD sz = hdr_bytes;
        if (!::WinHttpQueryHeaders(hreq,
                WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                hdr_block.data(), &sz, WINHTTP_NO_HEADER_INDEX))
            hdr_block.clear();
        while (!hdr_block.empty() && hdr_block.back() == L'\0')
            hdr_block.pop_back();
    }

    DWORD content_length = 0;
    DWORD cl_size = sizeof(content_length);
    BOOL have_cl = ::WinHttpQueryHeaders(hreq,
        WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &content_length, &cl_size,
        WINHTTP_NO_HEADER_INDEX);

    std::vector<std::byte> body;
    uint64_t total = have_cl ? content_length : 0;
    if (progress) progress(0, total);

    for (;;) {
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(hreq, &avail))
            return make_error<Response>(win_error("WinHttpQueryDataAvailable",
                ::GetLastError()));
        if (avail == 0) break;
        size_t off = body.size();
        body.resize(off + avail);
        DWORD got = 0;
        if (!::WinHttpReadData(hreq, body.data() + off, avail, &got))
            return make_error<Response>(win_error("WinHttpReadData",
                ::GetLastError()));
        body.resize(off + got);
        if (progress) progress(body.size(), total);
        if (got == 0) break;
    }

    Response resp;
    resp.status_code = static_cast<int>(status);
    resp.headers     = parse_raw_headers(hdr_block);
    resp.body        = std::move(body);
    return resp;
}

Result<Response> do_request_with_retry(const RawRequest& req,
                                       const RequestOptions& opts,
                                       ProgressCallback progress) {
    for (int attempt = 0; attempt <= opts.max_retries; ++attempt) {
        auto r = do_request_winhttp(req, opts.follow_redirects, progress);
        if (r) return r;
        if (attempt < opts.max_retries) backoff_sleep(attempt);
    }
    return make_error<Response>("http: all retries exhausted");
}

RawRequest make_raw(const Url& base, std::string_view path,
                    std::string_view method, const RequestOptions& defaults) {
    RawRequest req;
    req.method = std::string(method);
    req.host   = base.host;
    req.port   = base.port;
    req.tls    = (base.scheme == "https");
    req.path   = std::string(path.empty() ? "/" : path);
    req.headers = defaults.headers;
    return req;
}

} // namespace

// ─── Url + Response ──────────────────────────────────────────────────────────

Result<Url> Url::parse(std::string_view raw) { return detail::parse_url(raw); }

std::string Url::to_string() const {
    std::string s = scheme + "://" + host;
    bool default_port = (scheme == "https" && port == 443) ||
                        (scheme == "http"  && port == 80);
    if (!default_port && port != 0) s += ":" + std::to_string(port);
    s += path;
    if (!query.empty()) s += "?" + query;
    return s;
}

std::string Response::body_str() const {
    return std::string(reinterpret_cast<const char*>(body.data()), body.size());
}

// ─── HttpClient ──────────────────────────────────────────────────────────────

struct HttpClient::Impl {
    Url            base;
    RequestOptions defaults;
};

HttpClient::HttpClient(std::string base_url, RequestOptions defaults) {
    impl_ = std::make_unique<Impl>();
    auto u = Url::parse(base_url);
    if (u) impl_->base = *u;
    impl_->defaults = std::move(defaults);
}

HttpClient::~HttpClient() = default;

Result<Response> HttpClient::get(std::string_view path, Headers extra) {
    auto req = make_raw(impl_->base, path, "GET", impl_->defaults);
    for (auto& [k, v] : extra) req.headers[k] = v;
    return do_request_with_retry(req, impl_->defaults, nullptr);
}

Result<Response> HttpClient::post(std::string_view path, ByteSpan body,
                                  std::string_view content_type, Headers extra) {
    auto req = make_raw(impl_->base, path, "POST", impl_->defaults);
    req.headers["Content-Type"] = std::string(content_type);
    for (auto& [k, v] : extra) req.headers[k] = v;
    req.body.assign(body.begin(), body.end());
    return do_request_with_retry(req, impl_->defaults, nullptr);
}

Result<Response> HttpClient::put(std::string_view path, ByteSpan body,
                                 std::string_view content_type) {
    auto req = make_raw(impl_->base, path, "PUT", impl_->defaults);
    req.headers["Content-Type"] = std::string(content_type);
    req.body.assign(body.begin(), body.end());
    return do_request_with_retry(req, impl_->defaults, nullptr);
}

Result<Response> HttpClient::head(std::string_view path) {
    auto req = make_raw(impl_->base, path, "HEAD", impl_->defaults);
    return do_request_with_retry(req, impl_->defaults, nullptr);
}

Result<void> HttpClient::download(std::string_view path, const Path& dest,
                                  std::optional<std::string> expected_sha256,
                                  ProgressCallback progress) {
    auto req = make_raw(impl_->base, path, "GET", impl_->defaults);
    auto resp = do_request_with_retry(req, impl_->defaults, progress);
    if (!resp) return propagate<void>(resp);
    if (!resp->ok())
        return make_error("download: server returned " +
                          std::to_string(resp->status_code));

    if (expected_sha256) {
        auto actual = sha256_hex_bcrypt(resp->body);
        if (actual != *expected_sha256)
            return make_error("download: sha256 mismatch (got " + actual + ")");
    }

    ByteSpan data{resp->body.data(), resp->body.size()};
    RIVET_TRY(rivet::fs::write_atomic(dest, data));
    return {};
}

// ─── Free functions ──────────────────────────────────────────────────────────

Result<Response> http_get(const Url& url, RequestOptions opts) {
    HttpClient c{url.to_string(), std::move(opts)};
    return c.get(url.path.empty() ? "/" : url.path);
}

Result<void> download_file(const Url& url, const Path& dest,
                            std::optional<std::string> sha256,
                            ProgressCallback progress, RequestOptions opts) {
    HttpClient c{url.to_string(), std::move(opts)};
    return c.download(url.path.empty() ? "/" : url.path, dest,
                      std::move(sha256), std::move(progress));
}

Result<void> init_tls_trust_store() {
    // WinHTTP uses the system certificate store automatically.
    return {};
}

} // namespace rivet::net
