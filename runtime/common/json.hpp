// runtime/common/json.hpp — Minimal JSON reader for manifests and registry payloads.
//
// Used by VcpkgSource (vcpkg.json), the CLI registry queries, and other
// places that need to read well-formed JSON without pulling in simdjson.
// Recursive-descent parser, ~250 LOC including helpers. Not a full RFC 8259
// implementation: ignores comments and trailing commas; passes structural
// JSON. Handles unicode escapes for ASCII range only.
#pragma once

#include "../../platform/interface/result.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rivet::json {

class Value;
using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;

class Value {
public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value() = default;
    explicit Value(bool b)         : kind_(Kind::Bool),   bool_(b) {}
    explicit Value(double n)       : kind_(Kind::Number), num_(n) {}
    explicit Value(std::string s)  : kind_(Kind::String), str_(std::move(s)) {}
    explicit Value(Array a)        : kind_(Kind::Array)  { arr_ = std::make_shared<Array>(std::move(a)); }
    explicit Value(Object o)       : kind_(Kind::Object) { obj_ = std::make_shared<Object>(std::move(o)); }

    [[nodiscard]] Kind kind() const { return kind_; }

    [[nodiscard]] bool        is_null()   const { return kind_ == Kind::Null; }
    [[nodiscard]] bool        is_bool()   const { return kind_ == Kind::Bool; }
    [[nodiscard]] bool        is_number() const { return kind_ == Kind::Number; }
    [[nodiscard]] bool        is_string() const { return kind_ == Kind::String; }
    [[nodiscard]] bool        is_array()  const { return kind_ == Kind::Array; }
    [[nodiscard]] bool        is_object() const { return kind_ == Kind::Object; }

    [[nodiscard]] bool                  as_bool   (bool def = false)               const { return is_bool()   ? bool_ : def; }
    [[nodiscard]] double                as_number (double def = 0)                 const { return is_number() ? num_  : def; }
    [[nodiscard]] const std::string&    as_string (const std::string& def = empty_string()) const { return is_string() ? str_  : def; }
    [[nodiscard]] const Array&          as_array  ()                              const { return is_array()  ? *arr_  : empty_array(); }
    [[nodiscard]] const Object&         as_object ()                              const { return is_object() ? *obj_  : empty_object(); }

    // Object lookup. Returns null Value if key missing or not an object.
    [[nodiscard]] const Value& operator[](std::string_view key) const;
    // Array lookup. Returns null Value if out of range or not an array.
    [[nodiscard]] const Value& operator[](size_t idx) const;

private:
    static const std::string& empty_string();
    static const Array&  empty_array();
    static const Object& empty_object();
    static const Value&  null_value();

    Kind   kind_ = Kind::Null;
    bool   bool_ = false;
    double num_  = 0;
    std::string             str_;
    std::shared_ptr<Array>  arr_;
    std::shared_ptr<Object> obj_;
};

[[nodiscard]] Result<Value> parse(std::string_view text);

} // namespace rivet::json
