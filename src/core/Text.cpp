#include "sms/core/Text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace sms::text {

namespace {

/// Decodes one code point starting at s[i] and advances i. Returns false if the sequence is
/// malformed (i is still advanced by one byte so callers always make progress).
bool decodeUtf8(std::string_view s, std::size_t& i, char32_t& cp) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    std::size_t extra = 0;
    char32_t minValue = 0;
    if (b0 < 0x80) {
        cp = b0;
        ++i;
        return true;
    }
    if ((b0 & 0xE0U) == 0xC0U) {
        extra = 1; cp = b0 & 0x1FU; minValue = 0x80;
    } else if ((b0 & 0xF0U) == 0xE0U) {
        extra = 2; cp = b0 & 0x0FU; minValue = 0x800;
    } else if ((b0 & 0xF8U) == 0xF0U) {
        extra = 3; cp = b0 & 0x07U; minValue = 0x10000;
    } else {
        ++i;
        return false;
    }
    if (i + extra >= s.size()) {
        ++i;
        return false;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0U) != 0x80U) {
            ++i;
            return false;
        }
        cp = (cp << 6) | (b & 0x3FU);
    }
    i += extra + 1;
    if (cp < minValue || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    return true;
}

int codePointWidth(char32_t cp) {
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF)) {
        return 0;  // combining marks / zero-width characters
    }
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD)) {
        return 2;  // East Asian wide / emoji
    }
    return 1;
}

}  // namespace

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return std::string(s.substr(b, e - b));
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string toUpper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) { return toLower(a) == toLower(b); }

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

std::vector<std::string> split(std::string_view s, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(delimiter, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(s.substr(start));
            break;
        }
        parts.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string repeat(std::string_view unit, std::size_t count) {
    std::string out;
    out.reserve(unit.size() * count);
    for (std::size_t i = 0; i < count; ++i) out.append(unit);
    return out;
}

bool isValidUtf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        char32_t cp = 0;
        if (!decodeUtf8(s, i, cp)) return false;
    }
    return true;
}

bool hasControlChars(std::string_view s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7F;
    });
}

std::size_t displayWidth(std::string_view s) {
    std::size_t width = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
            if (i < s.size()) ++i;
            continue;
        }
        char32_t cp = 0;
        if (decodeUtf8(s, i, cp)) {
            width += static_cast<std::size_t>(codePointWidth(cp));
        } else {
            width += 1;  // replacement glyph
        }
    }
    return width;
}

std::string stripAnsi(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
            if (i < s.size()) ++i;
            continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}

std::string truncate(std::string_view s, std::size_t maxWidth, std::string_view ellipsis) {
    if (displayWidth(s) <= maxWidth) return std::string(s);
    const std::size_t ellWidth = displayWidth(ellipsis);
    if (maxWidth <= ellWidth) return std::string(ellipsis.substr(0, maxWidth));
    const std::size_t budget = maxWidth - ellWidth;
    std::string out;
    std::size_t width = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const std::size_t before = i;
        char32_t cp = 0;
        const bool ok = decodeUtf8(s, i, cp);
        const std::size_t w = ok ? static_cast<std::size_t>(codePointWidth(cp)) : 1;
        if (width + w > budget) break;
        if (ok) {
            out.append(s.substr(before, i - before));
        } else {
            out.push_back('?');
        }
        width += w;
    }
    out.append(ellipsis);
    return out;
}

std::string padRight(std::string_view s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    std::string out(s);
    if (w < width) out.append(width - w, ' ');
    return out;
}

std::string padLeft(std::string_view s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    std::string out;
    if (w < width) out.append(width - w, ' ');
    out.append(s);
    return out;
}

std::string padCenter(std::string_view s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    if (w >= width) return std::string(s);
    const std::size_t total = width - w;
    const std::size_t left = total / 2;
    return std::string(left, ' ') + std::string(s) + std::string(total - left, ' ');
}

std::string escapeField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::optional<std::string> unescapeField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out.push_back(s[i]);
            continue;
        }
        if (i + 1 >= s.size()) return std::nullopt;
        switch (s[++i]) {
            case '\\': out.push_back('\\'); break;
            case 't': out.push_back('\t'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            default: return std::nullopt;
        }
    }
    return out;
}

std::string csvEscape(std::string_view s) {
    std::string value(s);
    // Spreadsheet formula-injection guard (OWASP): neutralise leading = + - @ TAB CR.
    if (!value.empty() && (value[0] == '=' || value[0] == '+' || value[0] == '-' || value[0] == '@' ||
                           value[0] == '\t' || value[0] == '\r')) {
        value.insert(value.begin(), '\'');
    }
    const bool needsQuotes = value.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuotes) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string markdownEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '|': case '\\': case '`': case '*': case '_': case '<': case '>':
                out.push_back('\\');
                out.push_back(c);
                break;
            case '\n': case '\r': out.push_back(' '); break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string sanitizeForLog(std::string_view s, std::size_t maxBytes) {
    std::string out;
    out.reserve(std::min(s.size(), maxBytes));
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) {
            out.push_back('?');
        } else if (c == '|') {
            out.push_back('/');
        } else {
            out.push_back(c);
        }
    }
    if (out.size() > maxBytes) {
        std::size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0U) == 0x80U) --cut;
        out.resize(cut);
        out += "...";
    }
    return out;
}

std::string formatFixed(double value, int decimals) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::fixed << std::setprecision(decimals) << value;
    return os.str();
}

std::optional<double> parseDouble(std::string_view s) {
    const std::string t = trim(s);
    if (t.empty() || t.size() > 32) return std::nullopt;
    // Whitelist the characters first: digits, sign, one dot, exponent. Rejects "nan", "inf", hex.
    for (char c : t) {
        if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) {
            return std::nullopt;
        }
    }
    std::istringstream is(t);
    is.imbue(std::locale::classic());
    double v = 0;
    is >> v;
    if (is.fail() || !is.eof() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::optional<long long> parseInteger(std::string_view s) {
    const std::string t = trim(s);
    if (t.empty() || t.size() > 18) return std::nullopt;
    std::size_t i = 0;
    bool negative = false;
    if (t[0] == '+' || t[0] == '-') {
        negative = t[0] == '-';
        i = 1;
    }
    if (i >= t.size()) return std::nullopt;
    long long v = 0;
    for (; i < t.size(); ++i) {
        if (t[i] < '0' || t[i] > '9') return std::nullopt;
        v = v * 10 + (t[i] - '0');
    }
    return negative ? -v : v;
}

}  // namespace sms::text
