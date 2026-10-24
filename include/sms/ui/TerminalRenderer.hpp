#pragma once
// Screen composition: banner, breadcrumb header, status bar, menus, status badges, key/value
// panels and horizontal bar charts. All colour is routed through Terminal so it degrades cleanly.
#include <string>
#include <utility>
#include <vector>

#include "sms/ui/TableBuilder.hpp"
#include "sms/ui/Terminal.hpp"

namespace sms::ui {

struct StatusInfo {
    std::string user;
    std::string role;
    bool auditHealthy = true;
    std::string dataFile;
};

struct MenuItem {
    std::string key;
    std::string label;
    std::string hint;
};

class TerminalRenderer {
public:
    explicit TerminalRenderer(Terminal& terminal);

    Terminal& terminal() { return term_; }
    std::ostream& out() { return term_.out(); }
    std::size_t contentWidth() const;
    TableBuilder table() const;  // themed and width-limited

    /// Clears the screen and draws banner + breadcrumbs (+ status bar when `status` is given).
    void beginScreen(const std::vector<std::string>& crumbs, const StatusInfo* status);
    void menu(const std::string& title, const std::vector<MenuItem>& items);
    void heading(const std::string& text);
    void line(const std::string& text = "");
    void hint(const std::string& text);
    void rule();

    void success(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void info(const std::string& message);
    void fieldError(const std::string& message);

    void keyValues(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows);
    void barChart(const std::string& title, const std::vector<std::pair<std::string, double>>& bars, double total);

    std::string badge(const std::string& text, Style style) const;
    std::string symbol(const char* unicode, const char* ascii) const;

private:
    Terminal& term_;
};

}  // namespace sms::ui
