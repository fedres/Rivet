// platform/macos/terminal.cpp — macOS terminal backend
// Essentially identical to Linux terminal — both are POSIX + ANSI.
// macOS-specific: Terminal.app, iTerm2, etc., all support true color.
#include "../interface/terminal.hpp"

#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <unistd.h>

namespace rivet::terminal {

bool is_tty(int fd) { return ::isatty(fd) != 0; }

TermSize get_size() {
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return {ws.ws_col, ws.ws_row};
    return {80, 24};
}

ColorCap color_capability() {
    if (!is_tty(STDOUT_FILENO)) return ColorCap::None;
    const char* ct = std::getenv("COLORTERM");
    if (ct && (std::strcmp(ct, "truecolor") == 0 || std::strcmp(ct, "24bit") == 0))
        return ColorCap::TrueColor;
    // TERM_PROGRAM=iTerm.app or Apple_Terminal
    const char* tp = std::getenv("TERM_PROGRAM");
    if (tp && (std::strcmp(tp, "iTerm.app") == 0)) return ColorCap::TrueColor;
    const char* term = std::getenv("TERM");
    if (term && std::strstr(term, "256color")) return ColorCap::Color256;
    return ColorCap::Basic;
}

Result<void> enable_vt_processing(int /*fd*/) { return {}; }

void write_ansi(int fd, std::string_view seq) {
    ::write(fd, seq.data(), seq.size());
}

} // namespace rivet::terminal
