// platform/linux/terminal.cpp — Linux terminal backend
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
    const char* term = std::getenv("TERM");
    if (term && std::strstr(term, "256color")) return ColorCap::Color256;
    if (term && std::strstr(term, "color"))    return ColorCap::Basic;
    return ColorCap::Basic;
}

Result<void> enable_vt_processing(int /*fd*/) {
    return {}; // No-op on Linux
}

void write_ansi(int fd, std::string_view seq) {
    ::write(fd, seq.data(), seq.size());
}

} // namespace rivet::terminal
