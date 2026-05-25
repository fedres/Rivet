// rivet/platform/interface/result.hpp
// Project-wide Result<T> type based on std::expected (C++23).
// All platform APIs return Result<T> — no exceptions in the platform layer.
#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace rivet {

// Error type. May be enriched with structured diagnostics in the future.
struct Error {
    std::string message;
    int         code = 0;   // platform errno / Win32 error, 0 if not applicable

    explicit Error(std::string msg, int c = 0)
        : message(std::move(msg)), code(c) {}
};

template <typename T>
using Result = std::expected<T, Error>;

// Convenience: make an error result.
template <typename T = void>
[[nodiscard]] inline Result<T> make_error(std::string msg, int code = 0) {
    return std::unexpected(Error{std::move(msg), code});
}

// Propagate an error from a Result<U> into a Result<T>.
// Usage: return propagate<ReturnType>(other_result);
template <typename T, typename U>
[[nodiscard]] inline Result<T> propagate(const Result<U>& r) {
    return std::unexpected(r.error());
}

} // namespace rivet

// TRY macro — early-return on error, like Rust's `?` operator.
// Usage:  TRY(some_result_expression);
//         auto val = TRY_VAL(some_result_expression);
#define RIVET_TRY(expr)                        \
    do {                                       \
        auto _r = (expr);                      \
        if (!_r) return std::unexpected(_r.error()); \
    } while (0)

#define RIVET_TRY_VAL(expr)                    \
    [&]() -> decltype(auto) {                  \
        auto _r = (expr);                      \
        if (!_r) return std::unexpected(_r.error()); \
        return std::move(*_r);                 \
    }()
