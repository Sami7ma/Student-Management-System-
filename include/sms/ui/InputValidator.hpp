#pragma once
// Every interactive prompt goes through this class. Guarantees:
//  * never reads with operator>> (no type-mismatch failbit loops, no stale newlines);
//  * every line is length-capped, UTF-8 checked and control-character checked;
//  * numbers are parsed strictly (whole string, finite, in range);
//  * EOF throws InputClosedException instead of spinning;
//  * too many consecutive invalid entries abort the form;
//  * typing "/cancel" aborts any form (OperationCancelled).
#include <functional>
#include <optional>
#include <string>

#include "sms/ui/TerminalRenderer.hpp"

namespace sms::ui {

class InputValidator {
public:
    using Checker = std::function<std::optional<std::string>(const std::string&)>;
    static constexpr std::size_t kMaxAttempts = 10;
    static constexpr const char* kCancelToken = "/cancel";

    InputValidator(Terminal& terminal, TerminalRenderer& renderer) : term_(terminal), ui_(renderer) {}

    /// Text field. `check` receives the trimmed input and returns an error message or nullopt.
    /// Empty input returns `defaultValue` (or "" if `optional`).
    std::string line(const std::string& label, std::size_t maxLen, bool optional = false, const Checker& check = nullptr,
                     const std::string& defaultValue = "");
    int integer(const std::string& label, int min, int max);
    std::optional<int> optionalInteger(const std::string& label, int min, int max);
    double number(const std::string& label, double min, double max);
    std::optional<double> optionalNumber(const std::string& label, double min, double max);
    bool confirm(const std::string& question, bool defaultYes = false);
    /// Destructive-action gate: the user must type `phrase` exactly.
    bool confirmTyped(const std::string& question, const std::string& phrase);
    std::string secret(const std::string& label);
    std::string newSecret(const std::string& label);  // policy-checked, entered twice
    std::string menuKey(const std::string& label = "Select");
    void pause();

    /// Pure helpers exposed for unit tests.
    static std::optional<std::string> checkNumber(const std::string& input, double min, double max, double& out);
    static std::optional<std::string> checkInteger(const std::string& input, int min, int max, int& out);

private:
    std::string readTrimmed(const std::string& prompt, std::size_t maxLen, std::string& error);
    std::string prompt(const std::string& label, const std::string& suffix) const;
    void failedAttempt(std::size_t& attempts, const std::string& message);

    Terminal& term_;
    TerminalRenderer& ui_;
};

}  // namespace sms::ui
