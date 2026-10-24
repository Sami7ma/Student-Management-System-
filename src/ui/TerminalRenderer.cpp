#include "sms/ui/TerminalRenderer.hpp"

#include <algorithm>

#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"

namespace sms::ui {

TerminalRenderer::TerminalRenderer(Terminal& terminal) : term_(terminal) {}

std::size_t TerminalRenderer::contentWidth() const { return std::min<std::size_t>(term_.width(), 110); }

std::string TerminalRenderer::symbol(const char* unicode, const char* ascii) const {
    return term_.unicodeEnabled() ? unicode : ascii;
}

std::string TerminalRenderer::badge(const std::string& text, Style style) const {
    return term_.paint(" " + text + " ", style);
}

TableBuilder TerminalRenderer::table() const {
    TableTheme theme;
    theme.box = term_.unicodeEnabled() ? BoxStyle::unicode() : BoxStyle::ascii();
    theme.border = term_.code(Style::Cyan);
    theme.header = term_.code(Style::Gold);
    theme.title = term_.code(Style::Gold);
    theme.footer = term_.code(Style::Gray);
    theme.reset = term_.code(Style::Reset);
    TableBuilder t(theme);
    t.maxTotalWidth(contentWidth());
    return t;
}

void TerminalRenderer::beginScreen(const std::vector<std::string>& crumbs, const StatusInfo* status) {
    term_.clearScreen();
    const std::size_t w = contentWidth();
    const bool u = term_.unicodeEnabled();
    const std::string tl = u ? "\u2554" : "+", tr = u ? "\u2557" : "+", bl = u ? "\u255A" : "+", br = u ? "\u255D" : "+";
    const std::string h = u ? "\u2550" : "=", v = u ? "\u2551" : "|";
    const std::string title = "STUDENT MANAGEMENT SYSTEM";
    const std::string version = "v2.0";
    const std::string inner = text::padCenter(title, w - 2 - version.size() - 3) + std::string(2, ' ') + version + " ";
    auto gold = [&](const std::string& s) { return term_.paint(s, Style::Gold); };
    out() << gold(tl + text::repeat(h, w - 2) + tr) << "\n";
    out() << gold(v) << term_.paint(text::padRight(inner, w - 2), Style::Bold) << gold(v) << "\n";
    out() << gold(bl + text::repeat(h, w - 2) + br) << "\n";

    if (status != nullptr) {
        const std::string sep = u ? "  \u2502  " : "  |  ";
        std::string bar = " " + status->user + " (" + status->role + ")" + sep +
                          "Audit: " + (status->auditHealthy ? "OK" : "ERROR") + sep + timeutil::nowTimestamp();
        if (!status->dataFile.empty()) bar += sep + text::truncate(status->dataFile, 30, u ? "\u2026" : "~");
        bar = text::truncate(bar, w, u ? "\u2026" : "~");
        out() << term_.paint(text::padRight(bar, w), Style::StatusBar) << "\n";
    }
    if (!crumbs.empty()) {
        const std::string arrow = u ? " \u203A " : " > ";
        out() << " ";
        for (std::size_t i = 0; i < crumbs.size(); ++i) {
            if (i > 0) out() << term_.paint(arrow, Style::Gray);
            out() << (i + 1 == crumbs.size() ? term_.paint(crumbs[i], Style::Gold) : term_.paint(crumbs[i], Style::Cyan));
        }
        out() << "\n";
    }
    out() << term_.paint(text::repeat(u ? "\u2500" : "-", w), Style::Gray) << "\n";
}

void TerminalRenderer::menu(const std::string& title, const std::vector<MenuItem>& items) {
    TableBuilder t = table();
    t.title(title).column({"Key", Align::Center, 3, 0}).column({"Action", Align::Left, 10, 0}).column({"Description", Align::Left, 0, 0});
    for (const auto& item : items) {
        t.row(std::vector<Cell>{{item.key, term_.code(Style::Gold)}, {item.label, term_.code(Style::Bold)},
                                {item.hint, term_.code(Style::Gray)}});
    }
    out() << t.render();
}

void TerminalRenderer::heading(const std::string& text) {
    out() << "\n " << term_.paint(text, Style::Gold) << "\n";
}

void TerminalRenderer::line(const std::string& text) { out() << text << "\n"; }

void TerminalRenderer::hint(const std::string& text) { out() << " " << term_.paint(text, Style::Gray) << "\n"; }

void TerminalRenderer::rule() { out() << term_.paint(text::repeat(term_.unicodeEnabled() ? "\u2500" : "-", contentWidth()), Style::Gray) << "\n"; }

void TerminalRenderer::success(const std::string& m) { out() << " " << badge(symbol("\u2714 SUCCESS", "OK"), Style::BadgeOk) << " " << m << "\n"; }
void TerminalRenderer::warning(const std::string& m) { out() << " " << badge(symbol("\u26A0 WARNING", "WARN"), Style::BadgeWarn) << " " << m << "\n"; }
void TerminalRenderer::error(const std::string& m) { out() << " " << badge(symbol("\u2716 ERROR", "ERROR"), Style::BadgeErr) << " " << m << "\n"; }
void TerminalRenderer::info(const std::string& m) { out() << " " << badge(symbol("\u2139 INFO", "INFO"), Style::BadgeInfo) << " " << m << "\n"; }
void TerminalRenderer::fieldError(const std::string& m) { out() << "    " << term_.paint(symbol("\u2716 ", "x ") + m, Style::Red) << "\n"; }

void TerminalRenderer::keyValues(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows) {
    TableBuilder t = table();
    t.title(title).column({"Field", Align::Left, 8, 0}).column({"Value", Align::Left, 10, 0});
    for (const auto& [k, v] : rows) t.row(std::vector<Cell>{{k, term_.code(Style::Cyan)}, {v, {}}});
    out() << t.render();
}

void TerminalRenderer::barChart(const std::string& title, const std::vector<std::pair<std::string, double>>& bars,
                                double total) {
    heading(title);
    std::size_t labelW = 0;
    for (const auto& b : bars) labelW = std::max(labelW, text::displayWidth(b.first));
    const std::size_t barMax = contentWidth() > labelW + 24 ? std::min<std::size_t>(40, contentWidth() - labelW - 24) : 10;
    double peak = 0;
    for (const auto& b : bars) peak = std::max(peak, b.second);
    for (const auto& [label, value] : bars) {
        const std::size_t len = peak > 0 ? static_cast<std::size_t>(value / peak * static_cast<double>(barMax) + 0.5) : 0;
        const double pct = total > 0 ? 100.0 * value / total : 0.0;
        out() << "  " << text::padRight(label, labelW) << " " << term_.paint(text::repeat(symbol("\u2588", "#"), len), Style::Cyan)
              << (len == 0 ? "" : " ") << text::formatFixed(value, 0) << term_.paint("  (" + text::formatFixed(pct, 1) + "%)", Style::Gray) << "\n";
    }
}

}  // namespace sms::ui
