#include "sms/ui/Terminal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "sms/core/Exceptions.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace sms::ui {

std::string readBoundedLine(std::istream& in, std::size_t maxBytes, bool& truncated) {
    truncated = false;
    std::string line;
    bool consumedAny = false;
    char c = '\0';
    while (true) {
        in.get(c);
        if (!in) {
            if (in.eof()) {
                if (!consumedAny) throw InputClosedException();
                break;  // final line without a trailing newline
            }
            if (in.bad()) throw InputClosedException();  // unrecoverable I/O error
            // Recoverable failure state: reset flags and flush the rest of the line.
            in.clear();
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            truncated = true;
            break;
        }
        consumedAny = true;
        if (c == '\n') break;
        if (line.size() < maxBytes) {
            line.push_back(c);
        } else {
            truncated = true;  // keep draining to the newline, but do not store
        }
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

namespace {

bool isTty(int fd) {
#ifdef _WIN32
    return _isatty(fd) != 0;
#else
    return ::isatty(fd) != 0;
#endif
}

const char* env(const char* name) { return std::getenv(name); }

#ifdef _WIN32
void appendUtf8(std::string& s, unsigned long cp) {
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
#endif

}  // namespace

Terminal::Terminal(TerminalOptions options, std::istream& in, std::ostream& out) : in_(in), out_(out) {
    const bool stdinTty = (&in == &std::cin) && isTty(0);
    const bool stdoutTty = (&out == &std::cout) && isTty(1);
    interactive_ = stdinTty && stdoutTty;
    const char* term = env("TERM");
    color_ = stdoutTty && !options.forceNoColor && env("NO_COLOR") == nullptr &&
             !(term != nullptr && std::string(term) == "dumb");
    unicode_ = !options.forceAscii && env("SMS_ASCII") == nullptr;

#ifdef _WIN32
    if (stdoutTty) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode) ||
            !SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            color_ = false;  // legacy console without ANSI support
            unicode_ = false;
        }
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &info)) {
            width_ = static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
            height_ = static_cast<std::size_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
        }
    }
#else
    if (stdoutTty) {
        struct winsize ws {};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            width_ = ws.ws_col;
            height_ = ws.ws_row > 0 ? ws.ws_row : 40;
        }
    } else if (const char* cols = env("COLUMNS")) {
        const long v = std::strtol(cols, nullptr, 10);
        if (v > 0) width_ = static_cast<std::size_t>(v);
    }
#endif
    width_ = std::min<std::size_t>(std::max<std::size_t>(width_, 60), 140);
    height_ = std::min<std::size_t>(std::max<std::size_t>(height_, 20), 100);
}

std::string Terminal::code(Style style) const {
    if (!color_) return {};
    switch (style) {
        case Style::Reset: return "\033[0m";
        case Style::Bold: return "\033[1m";
        case Style::Dim: return "\033[2m";
        case Style::Red: return "\033[38;5;203m";
        case Style::Green: return "\033[38;5;78m";
        case Style::Amber: return "\033[38;5;214m";
        case Style::Cyan: return "\033[38;5;80m";
        case Style::Gold: return "\033[1;38;5;220m";
        case Style::Blue: return "\033[38;5;75m";
        case Style::Gray: return "\033[38;5;245m";
        case Style::BadgeOk: return "\033[1;30;48;5;78m";
        case Style::BadgeWarn: return "\033[1;30;48;5;214m";
        case Style::BadgeErr: return "\033[1;97;48;5;160m";
        case Style::BadgeInfo: return "\033[1;30;48;5;80m";
        case Style::StatusBar: return "\033[97;48;5;238m";
    }
    return {};
}

std::string Terminal::paint(std::string_view text, Style style) const {
    if (!color_) return std::string(text);
    return code(style) + std::string(text) + code(Style::Reset);
}

void Terminal::clearScreen() {
    if (interactive_ && color_) {
        out_ << "\033[2J\033[3J\033[H" << std::flush;
    } else if (interactive_) {
        out_ << "\n\n" << std::flush;
    } else {
        out_ << "\n";  // piped output: keep the transcript readable instead of emitting escapes
    }
}

std::string Terminal::readPassword(const std::string& prompt, std::size_t maxBytes) {
    out_ << prompt << std::flush;
    if (!interactive_) {
        bool truncated = false;
        std::string pw = readBoundedLine(in_, maxBytes, truncated);
        out_ << "********\n";  // never echo the real value, even when scripted
        if (truncated) pw.resize(maxBytes);
        return pw;
    }

    std::string pw;
    auto eraseLast = [&]() {
        if (pw.empty()) return;
        while (!pw.empty() && (static_cast<unsigned char>(pw.back()) & 0xC0U) == 0x80U) pw.pop_back();
        if (!pw.empty()) pw.pop_back();
        out_ << "\b \b" << std::flush;
    };

#ifdef _WIN32
    while (true) {
        const wint_t ch = _getwch();
        if (ch == L'\r' || ch == L'\n') break;
        if (ch == 3) {
            out_ << "\n";
            throw OperationCancelled();
        }
        if (ch == 26) throw InputClosedException();
        if (ch == 0 || ch == 0xE0) {
            (void)_getwch();  // function / arrow key: swallow the scan code
            continue;
        }
        if (ch == L'\b') {
            eraseLast();
            continue;
        }
        if (ch < 32) continue;
        unsigned long cp = ch;
        if (ch >= 0xD800 && ch <= 0xDBFF) {  // surrogate pair
            const wint_t low = _getwch();
            cp = 0x10000 + ((static_cast<unsigned long>(ch) - 0xD800) << 10) + (static_cast<unsigned long>(low) - 0xDC00);
        }
        if (pw.size() < maxBytes) {
            appendUtf8(pw, cp);
            out_ << '*' << std::flush;
        }
    }
#else
    struct termios saved {};
    if (tcgetattr(STDIN_FILENO, &saved) != 0) {
        bool truncated = false;  // cannot control the terminal: fall back to a normal read
        pw = readBoundedLine(in_, maxBytes, truncated);
        return pw;
    }
    struct termios raw = saved;
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | ISIG));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    struct Restore {  // RAII: the terminal is restored even if an exception escapes
        const termios& saved;
        ~Restore() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved); }
    } restore{saved};

    int escapeState = 0;  // 0 normal, 1 after ESC, 2 inside CSI sequence
    while (true) {
        unsigned char c = 0;
        const ssize_t n = ::read(STDIN_FILENO, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw InputClosedException();
        if (escapeState == 1) {
            escapeState = (c == '[' || c == 'O') ? 2 : 0;
            continue;
        }
        if (escapeState == 2) {
            if (c >= '@' && c <= '~') escapeState = 0;
            continue;
        }
        if (c == 0x1B) {
            escapeState = 1;
            continue;
        }
        if (c == '\n' || c == '\r') break;
        if (c == 0x03) {
            out_ << "\n";
            throw OperationCancelled();
        }
        if (c == 0x04) {
            if (pw.empty()) throw InputClosedException();
            continue;
        }
        if (c == 0x7F || c == 0x08) {
            eraseLast();
            continue;
        }
        if (c < 0x20) continue;
        if (pw.size() < maxBytes) {
            pw.push_back(static_cast<char>(c));
            if ((c & 0xC0U) != 0x80U) out_ << '*' << std::flush;
        }
    }
#endif
    out_ << "\n";
    return pw;
}

}  // namespace sms::ui
