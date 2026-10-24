#include "sms/ui/TableBuilder.hpp"

#include <algorithm>

#include "sms/core/Text.hpp"

namespace sms::ui {

BoxStyle BoxStyle::unicode() {
    return {"\u250C", "\u2510", "\u2514", "\u2518", "\u2500", "\u2502", "\u252C", "\u2534", "\u251C", "\u2524",
            "\u253C", "\u2026"};
}

BoxStyle BoxStyle::ascii() { return {"+", "+", "+", "+", "-", "|", "+", "+", "+", "+", "+", "~"}; }

TableBuilder& TableBuilder::title(std::string text) { title_ = std::move(text); return *this; }
TableBuilder& TableBuilder::footer(std::string text) { footer_ = std::move(text); return *this; }
TableBuilder& TableBuilder::column(Column c) { columns_.push_back(std::move(c)); return *this; }
TableBuilder& TableBuilder::maxTotalWidth(std::size_t w) { maxTotal_ = w; return *this; }
TableBuilder& TableBuilder::row(std::vector<Cell> cells) { rows_.push_back(std::move(cells)); return *this; }

TableBuilder& TableBuilder::row(const std::vector<std::string>& cells) {
    std::vector<Cell> converted;
    converted.reserve(cells.size());
    for (const auto& c : cells) converted.push_back({c, {}});
    rows_.push_back(std::move(converted));
    return *this;
}

namespace {

std::string cleanCell(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) c = ' ';
    }
    return out;
}

}  // namespace

std::string TableBuilder::render() const {
    const std::size_t n = columns_.size();
    if (n == 0) return {};
    const BoxStyle& bx = theme_.box;

    // ---- column widths ----
    std::vector<std::size_t> w(n);
    for (std::size_t i = 0; i < n; ++i) {
        w[i] = std::max(text::displayWidth(columns_[i].header), columns_[i].minWidth);
        for (const auto& r : rows_) {
            if (i < r.size()) w[i] = std::max(w[i], text::displayWidth(cleanCell(r[i].text)));
        }
        if (columns_[i].maxWidth > 0) w[i] = std::min(w[i], columns_[i].maxWidth);
    }
    auto total = [&]() {
        std::size_t t = 1;
        for (auto x : w) t += x + 3;
        return t;
    };
    // shrink the widest shrinkable column until the table fits the terminal
    if (maxTotal_ > 0) {
        while (total() > maxTotal_) {
            std::size_t best = n;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t floor = std::max<std::size_t>(columns_[i].minWidth, 5);
                if (w[i] > floor && (best == n || w[i] > w[best])) best = i;
            }
            if (best == n) break;
            --w[best];
        }
    }
    // make room for a long title / footer by widening the last column
    std::size_t needed = std::max(text::displayWidth(title_), text::displayWidth(footer_)) + 4;
    if (rows_.empty()) needed = std::max<std::size_t>(needed, text::displayWidth("(no records)") + 4);
    if (total() < needed) w[n - 1] += needed - total();
    const std::size_t inner = total() - 2;  // width between the outer borders

    auto B = [&](const std::string& s) { return theme_.border + s + theme_.reset; };
    auto hline = [&](const std::string& left, const std::string& joint, const std::string& right) {
        std::string s = left;
        for (std::size_t i = 0; i < n; ++i) {
            s += text::repeat(bx.h, w[i] + 2);
            s += (i + 1 < n) ? joint : right;
        }
        return B(s) + "\n";
    };
    auto solid = [&](const std::string& left, const std::string& right) {
        return B(left + text::repeat(bx.h, inner) + right) + "\n";
    };
    auto spanRow = [&](const std::string& content, const std::string& style) {
        const std::string padded = text::padRight(text::truncate(content, inner - 2, bx.ellipsis), inner - 2);
        return B(bx.v) + " " + style + padded + (style.empty() ? "" : theme_.reset) + " " + B(bx.v) + "\n";
    };

    std::string out;
    if (!title_.empty()) {
        out += solid(bx.tl, bx.tr);
        out += spanRow(title_, theme_.title);
        out += hline(bx.lj, bx.tj, bx.rj);
    } else {
        out += hline(bx.tl, bx.tj, bx.tr);
    }

    // header
    out += B(bx.v);
    for (std::size_t i = 0; i < n; ++i) {
        const std::string h = text::truncate(columns_[i].header, w[i], bx.ellipsis);
        const std::string padded = columns_[i].align == Align::Right    ? text::padLeft(h, w[i])
                                   : columns_[i].align == Align::Center ? text::padCenter(h, w[i])
                                                                        : text::padRight(h, w[i]);
        out += " " + theme_.header + padded + (theme_.header.empty() ? "" : theme_.reset) + " " + B(bx.v);
    }
    out += "\n";
    out += hline(bx.lj, rows_.empty() ? bx.bj : bx.cross, bx.rj);

    if (rows_.empty()) out += spanRow("(no records)", "");
    for (const auto& r : rows_) {
        out += B(bx.v);
        for (std::size_t i = 0; i < n; ++i) {
            const Cell cell = i < r.size() ? r[i] : Cell{};
            const std::string t = text::truncate(cleanCell(cell.text), w[i], bx.ellipsis);
            const std::string padded = columns_[i].align == Align::Right    ? text::padLeft(t, w[i])
                                       : columns_[i].align == Align::Center ? text::padCenter(t, w[i])
                                                                            : text::padRight(t, w[i]);
            out += " " + cell.style + padded + (cell.style.empty() ? "" : theme_.reset) + " " + B(bx.v);
        }
        out += "\n";
    }

    if (!footer_.empty()) {
        out += hline(bx.lj, bx.bj, bx.rj);
        out += spanRow(footer_, theme_.footer);
        out += solid(bx.bl, bx.br);
    } else {
        out += hline(bx.bl, bx.bj, bx.br);
    }
    return out;
}

}  // namespace sms::ui
