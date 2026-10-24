#include "sms/ui/InputValidator.hpp"

#include <cmath>

#include "sms/core/Exceptions.hpp"
#include "sms/core/Text.hpp"
#include "sms/models/Rules.hpp"

namespace sms::ui {

namespace {
std::string compact(double v) {
    std::string s = text::formatFixed(v, 2);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}
}  // namespace

std::string InputValidator::prompt(const std::string& label, const std::string& suffix) const {
    return "  " + term_.paint(ui_.symbol("\u203A", ">"), Style::Cyan) + " " + label +
           (suffix.empty() ? "" : " " + term_.paint(suffix, Style::Gray)) + ": ";
}

void InputValidator::failedAttempt(std::size_t& attempts, const std::string& message) {
    ui_.fieldError(message);
    if (++attempts >= kMaxAttempts) throw ValidationException("Too many invalid entries - the form was aborted.");
}

std::string InputValidator::readTrimmed(const std::string& promptText, std::size_t maxLen, std::string& error) {
    error.clear();
    term_.out() << promptText << std::flush;
    bool truncated = false;
    std::string raw = readBoundedLine(term_.in(), maxLen + 1, truncated);  // throws on EOF
    if (!term_.interactive()) term_.out() << raw << "\n";                  // keep scripted transcripts readable
    if (truncated || raw.size() > maxLen) {
        error = "Input too long (maximum " + std::to_string(maxLen) + " characters).";
        return {};
    }
    if (!text::isValidUtf8(raw)) {
        error = "Input contains invalid text encoding.";
        return {};
    }
    if (text::hasControlChars(raw)) {
        error = "Input contains control characters.";
        return {};
    }
    std::string t = text::trim(raw);
    if (text::iequals(t, kCancelToken)) throw OperationCancelled();
    return t;
}

std::string InputValidator::line(const std::string& label, std::size_t maxLen, bool optional, const Checker& check,
                                 const std::string& defaultValue) {
    std::string suffix;
    if (!defaultValue.empty()) suffix = "[" + defaultValue + "]";
    else if (optional) suffix = "(optional)";
    std::size_t attempts = 0;
    while (true) {
        std::string error;
        std::string value = readTrimmed(prompt(label, suffix), maxLen, error);
        if (!error.empty()) {
            failedAttempt(attempts, error);
            continue;
        }
        if (value.empty()) {
            if (!defaultValue.empty()) return defaultValue;
            if (optional) return {};
            failedAttempt(attempts, "This field is required.");
            continue;
        }
        if (check) {
            if (auto e = check(value)) {
                failedAttempt(attempts, *e);
                continue;
            }
        }
        return value;
    }
}

std::optional<std::string> InputValidator::checkNumber(const std::string& input, double min, double max, double& out) {
    const auto v = text::parseDouble(input);
    if (!v) return "Please enter a number (for example 72.5).";
    if (*v < min || *v > max) return "Value must be between " + compact(min) + " and " + compact(max) + ".";
    out = *v;
    return std::nullopt;
}

std::optional<std::string> InputValidator::checkInteger(const std::string& input, int min, int max, int& out) {
    const auto v = text::parseInteger(input);
    if (!v) return "Please enter a whole number.";
    if (*v < min || *v > max) return "Value must be between " + std::to_string(min) + " and " + std::to_string(max) + ".";
    out = static_cast<int>(*v);
    return std::nullopt;
}

int InputValidator::integer(const std::string& label, int min, int max) {
    int out = 0;
    line(label, 12, false, [&](const std::string& s) { return checkInteger(s, min, max, out); });
    return out;
}

std::optional<int> InputValidator::optionalInteger(const std::string& label, int min, int max) {
    int out = 0;
    const std::string s = line(label, 12, true, [&](const std::string& v) { return checkInteger(v, min, max, out); });
    if (s.empty()) return std::nullopt;
    return out;
}

double InputValidator::number(const std::string& label, double min, double max) {
    double out = 0;
    line(label, 24, false, [&](const std::string& s) { return checkNumber(s, min, max, out); });
    return out;
}

std::optional<double> InputValidator::optionalNumber(const std::string& label, double min, double max) {
    double out = 0;
    const std::string s = line(label, 24, true, [&](const std::string& v) { return checkNumber(v, min, max, out); });
    if (s.empty()) return std::nullopt;
    return out;
}

bool InputValidator::confirm(const std::string& question, bool defaultYes) {
    std::size_t attempts = 0;
    while (true) {
        std::string error;
        const std::string a = text::toLower(readTrimmed(prompt(question, defaultYes ? "[Y/n]" : "[y/N]"), 8, error));
        if (!error.empty()) {
            failedAttempt(attempts, error);
            continue;
        }
        if (a.empty()) return defaultYes;
        if (a == "y" || a == "yes") return true;
        if (a == "n" || a == "no") return false;
        failedAttempt(attempts, "Please answer y or n.");
    }
}

bool InputValidator::confirmTyped(const std::string& question, const std::string& phrase) {
    ui_.warning(question);
    std::string error;
    const std::string typed = readTrimmed(prompt("Type " + term_.paint(phrase, Style::Red) + " to confirm", ""), 64, error);
    if (!error.empty()) {
        ui_.fieldError(error);
        return false;
    }
    return typed == phrase;
}

std::string InputValidator::secret(const std::string& label) {
    return term_.readPassword(prompt(label, "(hidden)"), rules::kMaxPasswordLen + 1);
}

std::string InputValidator::newSecret(const std::string& label) {
    std::size_t attempts = 0;
    ui_.hint("Password: 8-64 characters with an upper-case letter, a lower-case letter and a digit.");
    while (true) {
        const std::string first = secret(label);
        if (auto e = rules::checkPassword(first)) {
            failedAttempt(attempts, *e);
            continue;
        }
        const std::string second = secret("Confirm " + label);
        if (first != second) {
            failedAttempt(attempts, "The two entries do not match.");
            continue;
        }
        return first;
    }
}

std::string InputValidator::menuKey(const std::string& label) {
    std::string error;
    try {
        std::string key = text::toLower(readTrimmed(prompt(label, ""), 32, error));
        if (!error.empty()) {
            ui_.fieldError(error);
            return {};
        }
        return key;
    } catch (const OperationCancelled&) {
        return {};  // "/cancel" at a menu prompt is simply "do nothing"
    }
}

void InputValidator::pause() {
    if (!term_.interactive()) return;
    term_.out() << "\n  " << term_.paint("Press Enter to continue" + ui_.symbol("\u2026", "..."), Style::Gray) << std::flush;
    bool truncated = false;
    (void)readBoundedLine(term_.in(), 16, truncated);
}

}  // namespace sms::ui
