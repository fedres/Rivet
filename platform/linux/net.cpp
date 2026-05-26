// platform/linux/net.cpp — Linux networking backend
// Transport: POSIX BSD sockets (getaddrinfo + connect).
// TLS:       System OpenSSL / LibreSSL via -lssl -lcrypto.
// HTTP:      HTTP/1.1 shared implementation from platform/common/http_impl.hpp.
//
// Note: links against system libssl. When vendored mbedTLS is available the
//       TlsConn implementation can be swapped without changing the HTTP layer.
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

// OpenSSL TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

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

std::string Response::body_str() const {
    return std::string(reinterpret_cast<const char*>(body.data()), body.size());
}

// ─── OpenSSL context (created once, thread-safe via static local) ─────────────

static SSL_CTX* get_ssl_ctx() {
    // C++11 guarantees thread-safe static local initialization.
    static SSL_CTX* ctx = [] {
        SSL_CTX* c = SSL_CTX_new(TLS_client_method());
        if (!c) return c;
        SSL_CTX_set_default_verify_paths(c);   // system CA bundle
        SSL_CTX_set_verify(c, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_mode(c, SSL_MODE_AUTO_RETRY);
        return c;
    }();
    return ctx;
}

// ─── TLS connection wrapper ───────────────────────────────────────────────────

struct TlsConn {
    int  fd  = -1;
    SSL* ssl = nullptr;
    bool tls = false;

    ~TlsConn() { close(); }

    void close() {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
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
            int s = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
            if (s < 0) continue;
            if (::connect(s, ai->ai_addr, ai->ai_addrlen) == 0) {
                fd = s;
                break;
            }
            ::close(s);
        }
        ::freeaddrinfo(res);
        if (fd < 0)
            return make_error(std::string("connect to ") + host + ": " + std::strerror(errno), errno);
        return {};
    }

    Result<void> start_tls(const std::string& hostname) {
        SSL_CTX* ctx = get_ssl_ctx();
        if (!ctx) return make_error("SSL_CTX_new failed");

        ssl = SSL_new(ctx);
        if (!ssl) return make_error("SSL_new failed");

        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, hostname.c_str());  // SNI

        // Hostname verification.
        SSL_set1_host(ssl, hostname.c_str());

        int rc = SSL_connect(ssl);
        if (rc != 1) {
            unsigned long err = ERR_get_error();
            char buf[256];
            ERR_error_string_n(err, buf, sizeof(buf));
            SSL_free(ssl); ssl = nullptr;
            return make_error(std::string("SSL_connect: ") + buf);
        }
        tls = true;
        return {};
    }

    Result<void> send_all(const char* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            if (tls) {
                int n = SSL_write(ssl, data + sent, static_cast<int>(len - sent));
                if (n <= 0) {
                    char buf[256]; ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
                    return make_error(std::string("SSL_write: ") + buf);
                }
                sent += static_cast<size_t>(n);
            } else {
                ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
                if (n < 0) return make_error(std::string("send: ") + std::strerror(errno), errno);
                sent += static_cast<size_t>(n);
            }
        }
        return {};
    }

    Result<std::vector<std::byte>> recv_all() {
        std::vector<std::byte> buf;
        std::vector<std::byte> chunk(32768);
        for (;;) {
            if (tls) {
                int n = SSL_read(ssl, chunk.data(), static_cast<int>(chunk.size()));
                if (n <= 0) break;
                buf.insert(buf.end(), chunk.begin(), chunk.begin() + n);
            } else {
                ssize_t n = ::recv(fd, chunk.data(), chunk.size(), 0);
                if (n <= 0) break;
                buf.insert(buf.end(), chunk.begin(), chunk.begin() + n);
            }
        }
        return buf;
    }
};

// ─── Core HTTP request logic ──────────────────────────────────────────────────

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

        std::vector<std::byte> body(raw->begin() + body_start, raw->end());

        auto te_it = pr.headers.find("transfer-encoding");
        if (te_it != pr.headers.end() && te_it->second.find("chunked") != std::string::npos)
            body = decode_chunked(body);

        if (pr.status_code >= 300 && pr.status_code < 400 && !pr.location.empty()) {
            auto loc_url = Url::parse(pr.location);
            if (!loc_url) break;
            HttpRequest next = req;
            next.method = "GET";
            next.host   = loc_url->host;
            next.port   = loc_url->port;
            next.tls    = (loc_url->scheme == "https");
            next.path   = loc_url->path + (loc_url->query.empty() ? "" : "?" + loc_url->query);
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

static Result<Response> do_request_with_retry(const HttpRequest& req, const RequestOptions& opts) {
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
    req.method  = "GET";
    req.host    = impl_->base.host;
    req.port    = impl_->base.port;
    req.tls     = (impl_->base.scheme == "https");
    req.path    = std::string(path.empty() ? "/" : path);
    req.headers = impl_->defaults.headers;
    for (auto& [k,v] : extra) req.headers[k] = v;
    return do_request_with_retry(req, impl_->defaults);
}

Result<Response> HttpClient::post(std::string_view path, ByteSpan body,
                                   std::string_view content_type, Headers extra) {
    HttpRequest req;
    req.method  = "POST";
    req.host    = impl_->base.host;
    req.port    = impl_->base.port;
    req.tls     = (impl_->base.scheme == "https");
    req.path    = std::string(path.empty() ? "/" : path);
    req.headers = impl_->defaults.headers;
    req.headers["Content-Type"] = std::string(content_type);
    for (auto& [k,v] : extra) req.headers[k] = v;
    req.body.assign(body.begin(), body.end());
    return do_request_with_retry(req, impl_->defaults);
}

Result<Response> HttpClient::put(std::string_view path, ByteSpan body,
                                  std::string_view content_type) {
    HttpRequest req;
    req.method  = "PUT";
    req.host    = impl_->base.host;
    req.port    = impl_->base.port;
    req.tls     = (impl_->base.scheme == "https");
    req.path    = std::string(path.empty() ? "/" : path);
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

    if (expected_sha256) {
        auto actual = sha256_hex(resp->body);
        if (actual != *expected_sha256)
            return make_error("download: sha256 mismatch (got " + actual + ")");
    }

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
    // Force OpenSSL context init with system CA store.
    if (!get_ssl_ctx()) return make_error("init_tls_trust_store: SSL_CTX_new failed");
    return {};
}

} // namespace rivet::net
