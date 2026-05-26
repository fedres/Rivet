// platform/macos/net.cpp — macOS networking backend
// Transport: POSIX BSD sockets (getaddrinfo + connect).
// TLS:       Security.framework SecureTransport (handles system CA trust automatically).
// HTTP:      HTTP/1.1 shared implementation from platform/common/http_impl.hpp.
// Suppress SecureTransport deprecation warnings (deprecated in macOS 10.15,
// replacement Network.framework requires Objective-C/Swift; vendored mbedTLS
// is the long-term plan per vendor/mbedtls/UPSTREAM).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include "../interface/net.hpp"
#include "../interface/fs.hpp"
#include "../common/http_impl.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

// POSIX sockets
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

// macOS TLS
#include <Security/Security.h>
#include <Security/SecureTransport.h>

namespace rivet::net {

using namespace detail;

// ─── Url ─────────────────────────────────────────────────────────────────────

Result<Url> Url::parse(std::string_view raw) { return detail::parse_url(raw); }

std::string Url::to_string() const {
    std::string s = scheme + "://" + host;
    bool default_port = (scheme == "https" && port == 443) || (scheme == "http" && port == 80);
    if (!default_port && port != 0) s += ":" + std::to_string(port);
    s += path;
    if (!query.empty()) s += "?" + query;
    return s;
}

// ─── Response helpers ─────────────────────────────────────────────────────────

std::string Response::body_str() const {
    return std::string(reinterpret_cast<const char*>(body.data()), body.size());
}

// ─── TLS socket wrapper ───────────────────────────────────────────────────────

struct TlsConn {
    int           fd  = -1;
    SSLContextRef ctx = nullptr;
    bool          tls = false;

    ~TlsConn() { close(); }

    void close() {
        if (ctx) { SSLClose(ctx); CFRelease(ctx); ctx = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

    Result<void> connect_tcp(const std::string& host, uint16_t port) {
        addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        auto port_str = std::to_string(port);
        int rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
        if (rc != 0)
            return make_error(std::string("getaddrinfo: ") + ::gai_strerror(rc));

        fd = -1;
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (s < 0) continue;
            if (::connect(s, ai->ai_addr, ai->ai_addrlen) == 0) {
                fd = s;
                break;
            }
            ::close(s);
        }
        ::freeaddrinfo(res);
        if (fd < 0)
            return make_error(std::string("connect: ") + std::strerror(errno), errno);
        return {};
    }

    Result<void> start_tls(const std::string& hostname) {
        ctx = SSLCreateContext(nullptr, kSSLClientSide, kSSLStreamType);
        if (!ctx) return make_error("SSLCreateContext failed");

        // I/O callbacks: read/write through our plain socket fd.
        SSLSetIOFuncs(ctx,
            [](SSLConnectionRef conn, void* data, size_t* len) -> OSStatus {
                int fd = static_cast<int>(reinterpret_cast<intptr_t>(conn));
                ssize_t n = ::read(fd, data, *len);
                if (n < 0) { *len = 0; return errSSLClosedAbort; }
                if (n == 0) { *len = 0; return errSSLClosedGraceful; }
                *len = static_cast<size_t>(n);
                return noErr;
            },
            [](SSLConnectionRef conn, const void* data, size_t* len) -> OSStatus {
                int fd = static_cast<int>(reinterpret_cast<intptr_t>(conn));
                ssize_t n = ::write(fd, data, *len);
                if (n < 0) { *len = 0; return errSSLClosedAbort; }
                *len = static_cast<size_t>(n);
                return noErr;
            });

        SSLSetConnection(ctx, reinterpret_cast<SSLConnectionRef>(static_cast<intptr_t>(fd)));
        SSLSetPeerDomainName(ctx, hostname.c_str(), hostname.size());

        OSStatus err;
        do { err = SSLHandshake(ctx); }
        while (err == errSSLWouldBlock);

        if (err != noErr) {
            CFRelease(ctx); ctx = nullptr;
            return make_error("SSLHandshake failed: " + std::to_string(err));
        }
        tls = true;
        return {};
    }

    Result<void> send_all(const char* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            if (tls) {
                size_t n = 0;
                OSStatus err = SSLWrite(ctx, data + sent, len - sent, &n);
                if (err != noErr && err != errSSLWouldBlock)
                    return make_error("SSLWrite: " + std::to_string(err));
                sent += n;
            } else {
                ssize_t n = ::send(fd, data + sent, len - sent, 0);
                if (n < 0) return make_error(std::string("send: ") + std::strerror(errno), errno);
                sent += static_cast<size_t>(n);
            }
        }
        return {};
    }

    // Read all response bytes until server closes connection.
    Result<std::vector<std::byte>> recv_all() {
        using Bytes = std::vector<std::byte>;
        Bytes buf;
        Bytes chunk(32768);
        for (;;) {
            if (tls) {
                size_t n = 0;
                OSStatus err = SSLRead(ctx, chunk.data(), chunk.size(), &n);
                if (err == errSSLClosedGraceful || err == errSSLClosedAbort || n == 0) break;
                if (err != noErr && err != errSSLWouldBlock)
                    return make_error<Bytes>("SSLRead: " + std::to_string(err));
                if (n > 0) buf.insert(buf.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(n));
            } else {
                ssize_t n = ::recv(fd, chunk.data(), chunk.size(), 0);
                if (n <= 0) break;
                buf.insert(buf.end(), chunk.begin(), chunk.begin() + n);
            }
        }
        return buf;
    }
};

// ─── Core request logic ───────────────────────────────────────────────────────

static Result<Response> do_request(const HttpRequest& req, int max_redirects = 5) {
    for (int redirect = 0; redirect <= max_redirects; ++redirect) {
        TlsConn conn;
        RIVET_TRY(conn.connect_tcp(req.host, req.port));
        if (req.tls) RIVET_TRY(conn.start_tls(req.host));

        std::string header_bytes = build_request(req);
        RIVET_TRY(conn.send_all(header_bytes.data(), header_bytes.size()));
        if (!req.body.empty())
            RIVET_TRY(conn.send_all(reinterpret_cast<const char*>(req.body.data()), req.body.size()));

        auto raw = conn.recv_all();
        if (!raw) return propagate<Response>(raw);

        ParsedResponse pr;
        int body_start = parse_response_headers(*raw, pr);
        if (body_start < 0)
            return make_error<Response>("http: malformed response headers");

        // Extract body bytes.
        std::vector<std::byte> body(raw->begin() + body_start, raw->end());

        // Decode transfer encoding.
        auto te_it = pr.headers.find("transfer-encoding");
        if (te_it != pr.headers.end() && te_it->second.find("chunked") != std::string::npos)
            body = decode_chunked(body);

        // Handle redirect (3xx).
        if (pr.status_code >= 300 && pr.status_code < 400 && !pr.location.empty()) {
            auto loc_url = Url::parse(pr.location);
            if (!loc_url) break;  // bad location, return as-is
            // Rebuild request for redirect target.
            HttpRequest redir = req;
            redir.host = loc_url->host;
            redir.port = loc_url->port;
            redir.tls  = (loc_url->scheme == "https");
            redir.path = loc_url->path + (loc_url->query.empty() ? "" : "?" + loc_url->query);
            // Follow redirect with GET.
            HttpRequest next = redir;
            next.method = "GET";
            next.body.clear();
            return do_request(next, max_redirects - redirect - 1);
        }

        Response resp;
        resp.status_code = pr.status_code;
        resp.headers     = pr.headers;
        resp.body        = std::move(body);
        return resp;
    }
    return make_error<Response>("http: too many redirects");
}

static Result<Response> do_request_with_retry(HttpRequest req, const RequestOptions& opts) {
    for (int attempt = 0; attempt <= opts.max_retries; ++attempt) {
        auto r = do_request(req);
        if (r) return r;
        if (attempt < opts.max_retries) backoff_sleep(attempt);
    }
    return make_error<Response>("http: all retries exhausted");
}

// ─── HttpClient::Impl ─────────────────────────────────────────────────────────

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
    HttpRequest req;
    req.method = "GET";
    req.host   = impl_->base.host;
    req.port   = impl_->base.port;
    req.tls    = (impl_->base.scheme == "https");
    req.path   = std::string(path.empty() ? "/" : path);
    req.headers = impl_->defaults.headers;
    for (auto& [k,v] : extra) req.headers[k] = v;
    return do_request_with_retry(req, impl_->defaults);
}

Result<Response> HttpClient::post(std::string_view path, ByteSpan body,
                                   std::string_view content_type, Headers extra) {
    HttpRequest req;
    req.method = "POST";
    req.host   = impl_->base.host;
    req.port   = impl_->base.port;
    req.tls    = (impl_->base.scheme == "https");
    req.path   = std::string(path.empty() ? "/" : path);
    req.headers = impl_->defaults.headers;
    req.headers["Content-Type"] = std::string(content_type);
    for (auto& [k,v] : extra) req.headers[k] = v;
    req.body.assign(body.begin(), body.end());
    return do_request_with_retry(req, impl_->defaults);
}

Result<Response> HttpClient::put(std::string_view path, ByteSpan body,
                                  std::string_view content_type) {
    HttpRequest req;
    req.method = "PUT";
    req.host   = impl_->base.host;
    req.port   = impl_->base.port;
    req.tls    = (impl_->base.scheme == "https");
    req.path   = std::string(path.empty() ? "/" : path);
    req.headers["Content-Type"] = std::string(content_type);
    req.body.assign(body.begin(), body.end());
    return do_request_with_retry(req, impl_->defaults);
}

Result<Response> HttpClient::head(std::string_view path) {
    HttpRequest req;
    req.method = "HEAD";
    req.host   = impl_->base.host;
    req.port   = impl_->base.port;
    req.tls    = (impl_->base.scheme == "https");
    req.path   = std::string(path.empty() ? "/" : path);
    return do_request_with_retry(req, impl_->defaults);
}

Result<void> HttpClient::download(std::string_view path, const Path& dest,
                                   std::optional<std::string> expected_sha256,
                                   ProgressCallback progress) {
    auto resp = get(path);
    if (!resp) return propagate<void>(resp);
    if (!resp->ok())
        return make_error("download: server returned " + std::to_string(resp->status_code));

    if (progress) progress(0, resp->body.size());

    // Verify checksum before making file visible.
    if (expected_sha256) {
        auto actual = sha256_hex(resp->body);
        if (actual != *expected_sha256)
            return make_error("download: sha256 mismatch (got " + actual + ")");
    }

    // Atomic write: the file is visible only after fsync + rename.
    ByteSpan data{resp->body.data(), resp->body.size()};
    RIVET_TRY(rivet::fs::write_atomic(dest, data));

    if (progress) progress(resp->body.size(), resp->body.size());
    return {};
}

// ─── Free functions ───────────────────────────────────────────────────────────

Result<Response> http_get(const Url& url, RequestOptions opts) {
    HttpClient c{url.to_string(), std::move(opts)};
    return c.get(url.path.empty() ? "/" : url.path);
}

Result<void> download_file(const Url& url, const Path& dest,
                            std::optional<std::string> sha256,
                            ProgressCallback progress, RequestOptions opts) {
    HttpClient c{url.to_string(), std::move(opts)};
    return c.download(url.path.empty() ? "/" : url.path, dest, std::move(sha256), std::move(progress));
}

Result<void> init_tls_trust_store() {
    // SecureTransport uses the system Keychain trust store automatically.
    return {};
}

} // namespace rivet::net

#pragma clang diagnostic pop
