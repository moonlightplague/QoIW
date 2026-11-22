#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace qoiw::cli {

enum class ColorMode { Auto, Always, Never };

// Auto enables colour only for a terminal, unless NO_COLOR is set or TERM is dumb.
bool use_color(ColorMode mode);

// Text formatting for the human-readable reports. JSON output does not use it.
std::string group_digits(std::uint64_t value);                   // 25,000,000
std::string significant(double value, int digits = 6);           // 8.20011e-06, inf, -inf, nan
std::string fixed(double value, int decimals);                   // 39.65
std::string percent(double share);                               // 85.3 %
std::string size(std::uint64_t bytes);                           // 2.52 MB
std::string shape(const std::vector<std::uint64_t>& dims);       // 500 x 500 x 100

class Console {
   public:
    Console(std::ostream& out, bool color) : out_(out), color_(color) {}

    std::string bold(std::string_view text) const { return style("1", text); }
    std::string good(std::string_view text) const { return style("1;32", text); }
    std::string bad(std::string_view text) const { return style("1;31", text); }
    std::string accent(std::string_view text) const { return style("1;36", text); }
    std::string dim(std::string_view text) const { return style("2", text); }

    void title(std::string_view command, std::string_view detail);
    void section(std::string_view name);
    // One "  label   value" line; labels are padded to a common width.
    void field(std::string_view label, std::string_view value);
    void line(std::string_view text);
    void blank();
    // A rule, then the highlighted summary items separated by wide gaps.
    void summary(const std::vector<std::string>& items);

    std::ostream& stream() { return out_; }

   private:
    std::string style(const char* code, std::string_view text) const;
    std::ostream& out_;
    bool color_;
};

// A column-aligned table. Cells are plain text; styling is applied per row
// after padding, so escape codes never disturb the alignment.
class Table {
   public:
    enum class Align { Left, Right };
    enum class Emphasis { None, Bold, Dim };

    struct Column {
        std::string heading;
        Align align;
    };

    explicit Table(std::vector<Column> columns) : columns_(std::move(columns)) {}

    // depth indents the first cell by two spaces per level.
    void row(std::vector<std::string> cells, int depth = 0, Emphasis emphasis = Emphasis::None);
    void print(Console& console) const;

   private:
    struct Row {
        std::vector<std::string> cells;
        int depth;
        Emphasis emphasis;
    };
    std::vector<Column> columns_;
    std::vector<Row> rows_;
};

}  // namespace qoiw::cli
