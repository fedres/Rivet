// platform/windows/terminal.cpp — Windows console/terminal backend
// Windows 10 1903+ supports VT/ANSI escape sequences via SetConsoleMode.
// Must call enable_vt_processing() before any ANSI output.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "../interface/terminal.hpp"

namespace rivet::terminal {

bool is_tty(int fd) {
    HANDLE h = (fd == 1) ? ::GetStdHandle(STD_OUTPUT_HANDLE)
                         : ::GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    return ::GetConsoleMode(h, &mode) != 0;
}

TermSize get_size() {
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (::GetConsoleScreenBufferInfo(h, &csbi)) {
        uint16_t cols = static_cast<uint16_t>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
        uint16_t rows = static_cast<uint16_t>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
        return {cols, rows};
    }
    return {80, 24};
}

ColorCap color_capability() {
    if (!is_tty(1)) return ColorCap::None;
    // Check COLORTERM env var first.
    char buf[64]{};
    DWORD n = ::GetEnvironmentVariableA("COLORTERM", buf, sizeof(buf));
    if (n > 0) {
        if (std::strcmp(buf, "truecolor") == 0 || std::strcmp(buf, "24bit") == 0)
            return ColorCap::TrueColor;
    }
    // Windows Terminal always supports true color.
    n = ::GetEnvironmentVariableA("WT_SESSION", buf, sizeof(buf));
    if (n > 0) return ColorCap::TrueColor;
    // Check for VT support.
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (::GetConsoleMode(h, &mode) && (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return ColorCap::Color256;
    return ColorCap::Basic;
}

Result<void> enable_vt_processing(int fd) {
    HANDLE h = (fd == 1) ? ::GetStdHandle(STD_OUTPUT_HANDLE)
                         : ::GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    if (!::GetConsoleMode(h, &mode))
        return {}; // Not a console — that's fine.
    if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        if (!::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            // Windows 10 <1607 doesn't support this flag — silently ignore.
        }
    }
    return {};
}

void write_ansi(int fd, std::string_view seq) {
    HANDLE h = (fd == 1) ? ::GetStdHandle(STD_OUTPUT_HANDLE)
                         : ::GetStdHandle(STD_ERROR_HANDLE);
    DWORD written;
    ::WriteConsoleA(h, seq.data(), static_cast<DWORD>(seq.size()), &written, nullptr);
}

} // namespace rivet::terminal
