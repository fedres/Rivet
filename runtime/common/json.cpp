// runtime/common/json.cpp — Recursive-descent JSON parser
#include "json.hpp"

#include <cctype>
#include <cstdlib>
#include <format>

namespace rivet::json {

const std::string& Value::empty_string() { static std::string s; return s; }
const Array&  Value::empty_array()  { static Array  a; return a; }
const Object& Value::empty_object() { static Object o; return o; }
const Value&  Value::null_value()   { static Value  v; return v; }

const Value& Value::operator[](std::string_view key) const {
    if (!is_object()) return null_value();
    auto it = obj_->find(std::string(key));
    if (it == obj_->end()) return null_value();
    return it->second;
}

const Value& Value::operator[](size_t idx) const {
    if (!is_array() || idx >= arr_->size()) return null_value();
    return (*arr_)[idx];
}

// ─── Parser ──────────────────────────────────────────────────────────────────

namespace {

struct Parser {
    std::string_view src;
    size_t pos = 0;

    [[nodiscard]] bool eof() const { return pos >= src.size(); }
    [[nodiscard]] char peek() const { return src[pos]; }

    void skip_ws() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos; continue; }
            // JSON has no comments, but some manifests use // — tolerate them.
            if (c == '/' && pos + 1 < src.size() && src[pos+1] == '/') {
                while (!eof() && peek() != '\n') ++pos;
                continue;
            }
            break;
        }
    }

    [[nodiscard]] bool match(char c) {
        skip_ws();
        if (eof() || peek() != c) return false;
        ++pos;
        return true;
    }

    [[nodiscard]] Result<Value> parse_value() {
        skip_ws();
        if (eof()) return make_error<Value>("json: unexpected eof");
        char c = peek();
        if (c == '"')                                return parse_string_val();
        if (c == '[')                                return parse_array();
        if (c == '{')                                return parse_object();
        if (c == '-' || (c >= '0' && c <= '9'))      return parse_number();
        if (c == 't' || c == 'f')                    return parse_bool();
        if (c == 'n')                                return parse_null();
        return make_error<Value>(std::format("json: unexpected '{}' at offset {}", c, pos));
    }

    [[nodiscard]] Result<std::string> parse_string_raw() {
        if (!match('"')) return make_error<std::string>("json: expected '\"'");
        std::string out;
        while (!eof()) {
            char c = src[pos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (eof()) return make_error<std::string>("json: unterminated escape");
                char e = src[pos++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (pos + 4 > src.size())
                            return make_error<std::string>("json: short \\u escape");
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src[pos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9')      code |= h - '0';
                            else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
                            else return make_error<std::string>("json: bad hex in \\u");
                        }
                        // Encode as UTF-8.
                        if (code < 0x80) {
                            out += static_cast<char>(code);
                        } else if (code < 0x800) {
                            out += static_cast<char>(0xC0 | (code >> 6));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (code >> 12));
                            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (code & 0x3F));
                        }
                        break;
                    }
                    default:
                        return make_error<std::string>(
                            std::format("json: bad escape \\{}", e));
                }
            } else {
                out += c;
            }
        }
        return make_error<std::string>("json: unterminated string");
    }

    [[nodiscard]] Result<Value> parse_string_val() {
        auto s = parse_string_raw();
        if (!s) return propagate<Value>(s);
        return Value{std::move(*s)};
    }

    [[nodiscard]] Result<Value> parse_number() {
        size_t start = pos;
        if (peek() == '-') ++pos;
        while (!eof() && (std::isdigit(static_cast<unsigned char>(peek())) ||
                           peek() == '.' || peek() == 'e' || peek() == 'E' ||
                           peek() == '+' || peek() == '-'))
            ++pos;
        // std::from_chars<double> requires modern libc++/libstdc++; std::strtod
        // is universally available and good enough for JSON manifest numbers.
        std::string num_str{src.data() + start, src.data() + pos};
        char* end = nullptr;
        double n  = std::strtod(num_str.c_str(), &end);
        if (end == num_str.c_str()) return make_error<Value>("json: bad number");
        return Value{n};
    }

    [[nodiscard]] Result<Value> parse_bool() {
        if (src.compare(pos, 4, "true")  == 0) { pos += 4; return Value{true};  }
        if (src.compare(pos, 5, "false") == 0) { pos += 5; return Value{false}; }
        return make_error<Value>("json: expected true/false");
    }

    [[nodiscard]] Result<Value> parse_null() {
        if (src.compare(pos, 4, "null") == 0) { pos += 4; return Value{}; }
        return make_error<Value>("json: expected null");
    }

    [[nodiscard]] Result<Value> parse_array() {
        if (!match('[')) return make_error<Value>("json: expected '['");
        Array arr;
        skip_ws();
        if (match(']')) return Value{std::move(arr)};
        for (;;) {
            auto v = parse_value();
            if (!v) return v;
            arr.push_back(std::move(*v));
            skip_ws();
            if (match(',')) continue;
            if (match(']')) return Value{std::move(arr)};
            return make_error<Value>("json: expected ',' or ']'");
        }
    }

    [[nodiscard]] Result<Value> parse_object() {
        if (!match('{')) return make_error<Value>("json: expected '{'");
        Object obj;
        skip_ws();
        if (match('}')) return Value{std::move(obj)};
        for (;;) {
            skip_ws();
            auto key = parse_string_raw();
            if (!key) return propagate<Value>(key);
            skip_ws();
            if (!match(':')) return make_error<Value>("json: expected ':'");
            auto val = parse_value();
            if (!val) return val;
            obj.emplace(std::move(*key), std::move(*val));
            skip_ws();
            if (match(',')) continue;
            if (match('}')) return Value{std::move(obj)};
            return make_error<Value>("json: expected ',' or '}'");
        }
    }
};

} // namespace

Result<Value> parse(std::string_view text) {
    Parser p{text};
    auto v = p.parse_value();
    if (!v) return v;
    p.skip_ws();
    // Allow trailing whitespace, but no other content.
    if (p.pos != p.src.size())
        return make_error<Value>("json: trailing content after value");
    return v;
}

} // namespace rivet::json
