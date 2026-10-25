#pragma once
// String utilities: trimming, case folding, UTF-8 validation and display width, escaping for the
// storage format / CSV / Markdown / log lines, and locale-independent number parsing.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sms::text {

std::string trim(std::string_view s);
std::string toLower(std::string_view s);  // ASCII only; other bytes are preserved
std::string toUpper(std::string_view s);  // ASCII only
bool iequals(std::string_view a, std::string_view b);
bool icontains(std::string_view haystack, std::string_view needle);
std::vector<std::string> split(std::string_view s, char delimiter);  // keeps empty fields
std::string repeat(std::string_view unit, std::size_t count);

// ---- UTF-8 ----
bool isValidUtf8(std::string_view s);
bool hasControlChars(std::string_view s);  // C0 controls and DEL
std::size_t displayWidth(std::string_view s);  // terminal columns; ANSI escapes count as 0
std::string stripAnsi(std::string_view s);
/// Cuts `s` to at most `maxWidth` columns, ending with `ellipsis` when something was removed.
std::string truncate(std::string_view s, std::size_t maxWidth, std::string_view ellipsis = "\xE2\x80\xA6");
std::string padRight(std::string_view s, std::size_t width);
std::string padLeft(std::string_view s, std::size_t width);
std::string padCenter(std::string_view s, std::size_t width);

// ---- Escaping ----
std::string escapeField(std::string_view s);                 // storage format (\\ \t \n \r)
std::optional<std::string> unescapeField(std::string_view s);  // nullopt on malformed escape
std::string csvEscape(std::string_view s);                   // RFC 4180 + formula-injection guard
std::string markdownEscape(std::string_view s);
std::string sanitizeForLog(std::string_view s, std::size_t maxBytes = 300);

// ---- Numbers (locale independent, whole-string, no NaN/Inf) ----
std::string formatFixed(double value, int decimals);
std::optional<double> parseDouble(std::string_view s);
std::optional<long long> parseInteger(std::string_view s);

}  // namespace sms::text
