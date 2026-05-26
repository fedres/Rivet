// platform/common/http_impl.hpp
// Shared HTTP/1.1 request building and response parsing.
// Included by platform-specific net.cpp files — not a public header.
//
// This layer is transport-agnostic: it builds raw request bytes and parses
// raw response bytes. The platform backend provides the socket + TLS I/O.
#pragma once

#include "../interface/net.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace rivet::net::detail {

// ─── URL parser ──────────────────────────────────────────────────────────────

inline Result<Url> parse_url(std::string_view raw) {
    Url u;

    // Extract scheme.
    auto scheme_end = raw.find("://");
    if (scheme_end == std::string_view::npos)
        return make_error<Url>("url: missing scheme");
    u.scheme = std::string(raw.substr(0, scheme_end));
    raw = raw.substr(scheme_end + 3);

    if (u.scheme != "http" && u.scheme != "https")
        return make_error<Url>("url: unsupported scheme '" + u.scheme + "'");

    // Extract path+query.
    auto slash = raw.find('/');
    std::string_view authority;
    if (slash == std::string_view::npos) {
        authority = raw;
        u.path = "/";
    } else {
        authority = raw.substr(0, slash);
        u.path    = std::string(raw.substr(slash));
    }

    // Extract query from path.
    auto qmark = u.path.find('?');
    if (qmark != std::string::npos) {
        u.query = u.path.substr(qmark + 1);
        u.path  = u.path.substr(0, qmark);
    }

    // Extract host and port.
    auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        u.host = std::string(authority.substr(0, colon));
        auto port_sv = authority.substr(colon + 1);
        uint16_t p = 0;
        auto [ptr, ec] = std::from_chars(port_sv.data(), port_sv.data() + port_sv.size(), p);
        if (ec != std::errc{}) return make_error<Url>("url: invalid port");
        u.port = p;
    } else {
        u.host = std::string(authority);
        u.port = (u.scheme == "https") ? 443 : 80;
    }

    if (u.host.empty()) return make_error<Url>("url: empty host");
    if (u.port == 0)    u.port = (u.scheme == "https") ? 443 : 80;
    return u;
}

// ─── HTTP request builder ────────────────────────────────────────────────────

struct HttpRequest {
    std::string method;
    std::string path;   // includes query string
    std::string host;
    uint16_t    port = 0;
    bool        tls  = false;
    Headers     headers;
    std::vector<std::byte> body;
};

inline std::string build_request(const HttpRequest& req) {
    std::string full_path = req.path;

    std::ostringstream ss;
    ss << req.method << " " << full_path << " HTTP/1.1\r\n";
    ss << "Host: " << req.host;
    bool default_port = (req.tls && req.port == 443) || (!req.tls && req.port == 80);
    if (!default_port) ss << ":" << req.port;
    ss << "\r\n";
    ss << "Connection: close\r\n";
    ss << "User-Agent: rivet/0.1\r\n";
    if (!req.body.empty())
        ss << "Content-Length: " << req.body.size() << "\r\n";
    for (auto& [k, v] : req.headers)
        ss << k << ": " << v << "\r\n";
    ss << "\r\n";
    return ss.str();
}

// ─── HTTP response parser ────────────────────────────────────────────────────

struct ParsedResponse {
    int     status_code = 0;
    Headers headers;
    std::vector<std::byte> body;
    std::string location;  // redirect target
};

inline std::string header_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Parse headers from raw bytes. Returns offset past the header block (start of body).
// Returns -1 on incomplete data.
inline int parse_response_headers(const std::vector<std::byte>& raw, ParsedResponse& out) {
    // Find \r\n\r\n separator.
    const char* data = reinterpret_cast<const char*>(raw.data());
    size_t      len  = raw.size();

    const char* sep = nullptr;
    for (size_t i = 0; i + 3 < len; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '\r' && data[i+3] == '\n') {
            sep = data + i;
            break;
        }
    }
    if (!sep) return -1;  // incomplete

    std::string_view header_block(data, sep - data);
    int body_start = static_cast<int>(sep - data) + 4;

    // Parse status line.
    auto nl = header_block.find("\r\n");
    if (nl == std::string_view::npos) return -1;
    std::string_view status_line = header_block.substr(0, nl);
    header_block = header_block.substr(nl + 2);

    // "HTTP/1.x NNN Reason"
    auto sp1 = status_line.find(' ');
    if (sp1 == std::string_view::npos) return -1;
    auto code_sv = status_line.substr(sp1 + 1, 3);
    std::from_chars(code_sv.data(), code_sv.data() + code_sv.size(), out.status_code);

    // Parse header fields.
    while (!header_block.empty()) {
        nl = header_block.find("\r\n");
        if (nl == std::string_view::npos) break;
        auto line = header_block.substr(0, nl);
        header_block = header_block.substr(nl + 2);
        auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        std::string key = header_lower(std::string(line.substr(0, colon)));
        std::string_view val = line.substr(colon + 1);
        while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
        out.headers[key] = std::string(val);
        if (key == "location") out.location = std::string(val);
    }

    return body_start;
}

// Decode chunked transfer encoding in-place.
inline std::vector<std::byte> decode_chunked(const std::vector<std::byte>& raw) {
    std::vector<std::byte> out;
    const char* p   = reinterpret_cast<const char*>(raw.data());
    const char* end = p + raw.size();

    while (p < end) {
        // Read chunk size line (hex + CRLF).
        const char* nl = static_cast<const char*>(std::memchr(p, '\n', end - p));
        if (!nl) break;
        std::string_view size_sv(p, nl - p);
        if (!size_sv.empty() && size_sv.back() == '\r') size_sv.remove_suffix(1);
        size_t chunk_size = 0;
        // Ignore chunk extensions after ';'.
        auto semi = size_sv.find(';');
        if (semi != std::string_view::npos) size_sv = size_sv.substr(0, semi);
        for (char c : size_sv) {
            chunk_size <<= 4;
            if (c >= '0' && c <= '9') chunk_size |= c - '0';
            else if (c >= 'a' && c <= 'f') chunk_size |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') chunk_size |= c - 'A' + 10;
        }
        p = nl + 1;
        if (chunk_size == 0) break;
        if (p + static_cast<ptrdiff_t>(chunk_size) > end) break;
        out.insert(out.end(),
                   reinterpret_cast<const std::byte*>(p),
                   reinterpret_cast<const std::byte*>(p) + chunk_size);
        p += chunk_size;
        if (p + 1 < end && *p == '\r') ++p;  // skip \r
        if (p < end && *p == '\n') ++p;       // skip \n
    }
    return out;
}

// ─── SHA-256 checksum (portable, no OpenSSL dependency) ──────────────────────
// RFC 6234 compact implementation for download verification.

inline uint32_t sha256_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline std::string sha256_hex(const std::vector<std::byte>& data) {
    static constexpr uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    // Pre-processing.
    std::vector<uint8_t> msg(reinterpret_cast<const uint8_t*>(data.data()),
                              reinterpret_cast<const uint8_t*>(data.data()) + data.size());
    uint64_t bit_len = data.size() * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>(bit_len >> (i * 8)));

    // Process each 512-bit block.
    for (size_t i = 0; i < msg.size(); i += 64) {
        uint32_t w[64];
        for (int j = 0; j < 16; ++j)
            w[j] = (uint32_t(msg[i + j*4]) << 24) | (uint32_t(msg[i + j*4+1]) << 16)
                 | (uint32_t(msg[i + j*4+2]) << 8) | uint32_t(msg[i + j*4+3]);
        for (int j = 16; j < 64; ++j) {
            uint32_t s0 = sha256_rotr(w[j-15],7) ^ sha256_rotr(w[j-15],18) ^ (w[j-15] >> 3);
            uint32_t s1 = sha256_rotr(w[j-2],17) ^ sha256_rotr(w[j-2],19)  ^ (w[j-2]  >> 10);
            w[j] = w[j-16] + s0 + w[j-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int j = 0; j < 64; ++j) {
            uint32_t S1  = sha256_rotr(e,6)^sha256_rotr(e,11)^sha256_rotr(e,25);
            uint32_t ch  = (e & f) ^ (~e & g);
            uint32_t tmp1= hh + S1 + ch + K[j] + w[j];
            uint32_t S0  = sha256_rotr(a,2)^sha256_rotr(a,13)^sha256_rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t tmp2= S0 + maj;
            hh=g; g=f; f=e; e=d+tmp1; d=c; c=b; b=a; a=tmp1+tmp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    char hex[65];
    for (int i = 0; i < 8; ++i)
        std::snprintf(hex + i*8, 9, "%08x", h[i]);
    hex[64] = '\0';
    return std::string(hex);
}

// ─── Retry with exponential backoff ──────────────────────────────────────────

inline void backoff_sleep(int attempt) {
    int ms = 200 * (1 << std::min(attempt, 5));  // 200, 400, 800, 1600, 3200 ms cap
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace rivet::net::detail
