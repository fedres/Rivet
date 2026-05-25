// platform/linux/net.cpp — Linux networking backend
// TLS via embedded mbedTLS (vendored in vendor/mbedtls/).
// System CA store: /etc/ssl/certs/ca-certificates.crt
// TODO Phase 0: implement full HTTP/1.1 + TLS + retry + resume.
#include "../interface/net.hpp"

namespace rivet::net {

Result<Url> Url::parse(std::string_view raw) {
    // TODO: implement URL parser
    (void)raw;
    return make_error<Url>("Url::parse: not yet implemented");
}

std::string Url::to_string() const {
    return scheme + "://" + host + path;
}

struct HttpClient::Impl {
    std::string base_url;
    RequestOptions defaults;
};

HttpClient::HttpClient(std::string base_url, RequestOptions defaults)
    : impl_(std::make_unique<Impl>(Impl{std::move(base_url), std::move(defaults)})) {}

HttpClient::~HttpClient() = default;

Result<Response> HttpClient::get(std::string_view path, Headers /*extra*/) {
    // TODO Phase 0: implement
    (void)path;
    return make_error<Response>("HttpClient::get: not yet implemented");
}

Result<Response> HttpClient::post(std::string_view path, ByteSpan /*body*/,
                                   std::string_view /*ct*/, Headers /*extra*/) {
    (void)path;
    return make_error<Response>("HttpClient::post: not yet implemented");
}

Result<Response> HttpClient::put(std::string_view path, ByteSpan /*body*/,
                                  std::string_view /*ct*/) {
    (void)path;
    return make_error<Response>("HttpClient::put: not yet implemented");
}

Result<Response> HttpClient::head(std::string_view path) {
    (void)path;
    return make_error<Response>("HttpClient::head: not yet implemented");
}

Result<void> HttpClient::download(std::string_view path, const Path& dest,
                                   std::optional<std::string> /*sha256*/,
                                   ProgressCallback /*progress*/) {
    (void)path; (void)dest;
    return make_error("HttpClient::download: not yet implemented");
}

Result<Response> http_get(const Url& url, RequestOptions opts) {
    HttpClient c{url.to_string(), std::move(opts)};
    return c.get("/");
}

Result<void> download_file(const Url& url, const Path& dest,
                            std::optional<std::string> sha256,
                            ProgressCallback progress, RequestOptions opts) {
    HttpClient c{url.to_string(), std::move(opts)};
    return c.download("/", dest, std::move(sha256), std::move(progress));
}

Result<void> init_tls_trust_store() {
    // TODO: load /etc/ssl/certs/ca-certificates.crt into mbedTLS context
    return {};
}

} // namespace rivet::net
