#pragma once
// Dynamic box-drawing tables. Column widths adapt to content, shrink (with an ellipsis) to fit
// the terminal, and display width is measured in terminal columns (UTF-8 aware).
#include <string>
#include <vector>

namespace sms::ui {

struct BoxStyle {
    std::string tl, tr, bl, br, h, v, tj, bj, lj, rj, cross, ellipsis;
    static BoxStyle unicode();
    static BoxStyle ascii();
};

struct TableTheme {
    BoxStyle box = BoxStyle::unicode();
    std::string border, header, title, footer, reset;  // ANSI prefixes; empty when colour is off
};

enum class Align { Left, Right, Center };

struct Column {
    std::string header;
    Align align = Align::Left;
    std::size_t minWidth = 0;
    std::size_t maxWidth = 0;  // 0 = unlimited
};

struct Cell {
    std::string text;   // plain text (no escape codes)
    std::string style;  // optional ANSI prefix applied after truncation/padding
};

class TableBuilder {
public:
    explicit TableBuilder(TableTheme theme) : theme_(std::move(theme)) {}

    TableBuilder& title(std::string text);
    TableBuilder& footer(std::string text);
    TableBuilder& column(Column column);
    TableBuilder& maxTotalWidth(std::size_t width);
    TableBuilder& row(std::vector<Cell> cells);
    TableBuilder& row(const std::vector<std::string>& cells);
    std::size_t rowCount() const { return rows_.size(); }

    std::string render() const;

private:
    TableTheme theme_;
    std::string title_;
    std::string footer_;
    std::vector<Column> columns_;
    std::vector<std::vector<Cell>> rows_;
    std::size_t maxTotal_ = 0;
};

}  // namespace sms::ui
