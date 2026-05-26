// platform/windows/net.cpp — Windows networking backend
// TLS: embedded mbedTLS + CertOpenSystemStore for CA trust.
// TODO Phase 0: implement.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#include "../interface/net.hpp"

namespace rivet::net {

Result<Url> Url::parse(std::string_view raw) {
    (void)raw; return make_error<Url>("Url::parse: not yet implemented");
}
std::string Url::to_string() const { return scheme + "://" + host + path; }

struct HttpClient::Impl { std::string base_url; RequestOptions defaults; };
HttpClient::HttpClient(std::string base_url, RequestOptions defaults)
    : impl_(std::make_unique<Impl>(Impl{std::move(base_url), std::move(defaults)})) {}
HttpClient::~HttpClient() = default;
Result<Response> HttpClient::get(std::string_view p, Headers) { (void)p; return make_error<Response>("not implemented"); }
Result<Response> HttpClient::post(std::string_view p, ByteSpan, std::string_view, Headers) { (void)p; return make_error<Response>("not implemented"); }
Result<Response> HttpClient::put(std::string_view p, ByteSpan, std::string_view) { (void)p; return make_error<Response>("not implemented"); }
Result<Response> HttpClient::head(std::string_view p) { (void)p; return make_error<Response>("not implemented"); }
Result<void> HttpClient::download(std::string_view p, const Path& d, std::optional<std::string>, ProgressCallback) { (void)p; (void)d; return make_error("not implemented"); }
Result<Response> http_get(const Url& url, RequestOptions opts) { HttpClient c{url.to_string(), std::move(opts)}; return c.get("/"); }
Result<void> download_file(const Url& url, const Path& dest, std::optional<std::string> sha, ProgressCallback pb, RequestOptions opts) { HttpClient c{url.to_string(), std::move(opts)}; return c.download("/", dest, std::move(sha), std::move(pb)); }

std::string Response::body_str() const {
    return std::string(reinterpret_cast<const char*>(body.data()), body.size());
}

Result<void> init_tls_trust_store() {
    // TODO: enumerate CertOpenSystemStore("ROOT") and import into mbedTLS context.
    return {};
}

} // namespace rivet::net
