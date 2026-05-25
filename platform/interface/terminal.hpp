// rivet/platform/interface/terminal.hpp
// Terminal / console Platform Abstraction Layer.
//
// Platform implementations:
//   platform/linux/terminal.cpp    (termios / ANSI / ioctl TIOCGWINSZ)
//   platform/macos/terminal.cpp    (same as Linux)
//   platform/windows/terminal.cpp  (SetConsoleMode VT / GetConsoleScreenBufferInfo)
#pragma once

#include "result.hpp"
#include "types.hpp"

#include <string_view>

namespace rivet::terminal {

// Color capability detected from TERM / COLORTERM / TERM_PROGRAM env vars.
enum class ColorCap {
    None,       // no color
    Basic,      // 8-color ANSI
    Color256,   // 256-color xterm
    TrueColor,  // 24-bit RGB
};

// Is the given file descriptor connected to a TTY/console?
[[nodiscard]] bool is_tty(int fd);

// Query terminal dimensions. Returns {80, 24} as fallback if unknown.
[[nodiscard]] TermSize get_size();

// Detect color support.
[[nodiscard]] ColorCap color_capability();

// On Windows: enable VT processing for the given HANDLE (stdout/stderr).
// No-op on POSIX. Must be called before any ANSI output.
[[nodiscard]] Result<void> enable_vt_processing(int fd);

// Write raw ANSI/VT escape sequence bytes directly (no buffering).
void write_ansi(int fd, std::string_view seq);

// ─── Cursor / output helpers ─────────────────────────────────────────────────

inline constexpr std::string_view RESET        = "\x1b[0m";
inline constexpr std::string_view BOLD         = "\x1b[1m";
inline constexpr std::string_view DIM          = "\x1b[2m";
inline constexpr std::string_view RED          = "\x1b[31m";
inline constexpr std::string_view GREEN        = "\x1b[32m";
inline constexpr std::string_view YELLOW       = "\x1b[33m";
inline constexpr std::string_view BLUE         = "\x1b[34m";
inline constexpr std::string_view CYAN         = "\x1b[36m";
inline constexpr std::string_view CLEAR_LINE   = "\x1b[2K\r";
inline constexpr std::string_view CURSOR_UP    = "\x1b[1A";
inline constexpr std::string_view HIDE_CURSOR  = "\x1b[?25l";
inline constexpr std::string_view SHOW_CURSOR  = "\x1b[?25h";

} // namespace rivet::terminal
