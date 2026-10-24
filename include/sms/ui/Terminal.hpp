#pragma once
// Platform abstraction for the terminal: TTY detection, ANSI styling, window size, screen
// clearing, bounded line input and masked password input (termios on POSIX, conio on Windows).
#include <cstddef>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

namespace sms::ui {

struct TerminalOptions {
    bool forceAscii = false;    // ASCII borders instead of Unicode box drawing
    bool forceNoColor = false;
};

enum class Style {
    Reset, Bold, Dim, Red, Green, Amber, Cyan, Gold, Blue, Gray,
    BadgeOk, BadgeWarn, BadgeErr, BadgeInfo, StatusBar
};

/// Reads one line with a hard byte cap. Throws InputClosedException on EOF / unrecoverable
/// stream failure. On a recoverable failure it clears the stream flags and discards the rest of
/// the line (cin.clear + cin.ignore), so callers can never spin forever. `truncated` is set when
/// the line exceeded `maxBytes` (the excess is discarded).
std::string readBoundedLine(std::istream& in, std::size_t maxBytes, bool& truncated);

class Terminal {
public:
    Terminal(TerminalOptions options, std::istream& in, std::ostream& out);

    std::istream& in() { return in_; }
    std::ostream& out() { return out_; }

    bool interactive() const { return interactive_; }   // stdin and stdout are real terminals
    bool colorEnabled() const { return color_; }
    bool unicodeEnabled() const { return unicode_; }
    std::size_t width() const { return width_; }
    std::size_t height() const { return height_; }

    std::string code(Style style) const;                       // raw escape (empty if colour off)
    std::string paint(std::string_view text, Style style) const;
    void clearScreen();

    /// Masked input: prints '*' per character on a real terminal; plain line read when piped.
    /// Throws OperationCancelled on Ctrl-C and InputClosedException on EOF.
    std::string readPassword(const std::string& prompt, std::size_t maxBytes);

private:
    std::istream& in_;
    std::ostream& out_;
    bool interactive_ = false;
    bool color_ = false;
    bool unicode_ = true;
    std::size_t width_ = 100;
    std::size_t height_ = 40;
};

}  // namespace sms::ui
