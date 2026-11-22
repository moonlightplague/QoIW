#include "console.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace qoiw::cli {
namespace {
constexpr std::size_t kLabelWidth = 16;
constexpr std::size_t kRuleWidth = 72;

std::string printf_string(const char* format, double value, int precision) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, precision, value);
    return buffer;
}

// Count display columns of UTF-8 text without styling escapes.
std::size_t width(std::string_view text) {
    std::size_t columns = 0;
    for (unsigned char c : text) columns += (c & 0xC0) != 0x80;
    return columns;
}

std::string pad(std::string_view text, std::size_t target, Table::Align align) {
    const auto missing = target > width(text) ? target - width(text) : 0;
    std::string spaces(missing, ' ');
    return align == Table::Align::Left ? std::string(text) + spaces : spaces + std::string(text);
}
}  // namespace

bool use_color(ColorMode mode) {
    if (mode != ColorMode::Auto) return mode == ColorMode::Always;
#ifdef _WIN32
    return false;
#else
    const char* no_color = std::getenv("NO_COLOR");
    if (no_color && *no_color) return false;
    const char* term = std::getenv("TERM");
    if (term && std::strcmp(term, "dumb") == 0) return false;
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::string group_digits(std::uint64_t value) {
    auto digits = std::to_string(value);
    for (auto i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) digits.insert(i, ",");
    return digits;
}

std::string significant(double value, int digits) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    return printf_string("%.*g", value, digits);
}

std::string fixed(double value, int decimals) {
    if (!std::isfinite(value)) return significant(value);
    return printf_string("%.*f", value, decimals);
}

std::string percent(double share) {
    return fixed(100 * share, 1) + " %";
}

std::string size(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "kB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1000 && unit + 1 < std::size(units)) {
        value /= 1000;
        ++unit;
    }
    if (unit == 0) return std::to_string(bytes) + " B";
    return significant(value, 3) + " " + units[unit];
}

std::string shape(const std::vector<std::uint64_t>& dims) {
    std::string text;
    for (std::size_t i = 0; i < dims.size(); ++i) {
        if (i) text += " x ";
        text += std::to_string(dims[i]);
    }
    return text;
}

std::string Console::style(const char* code, std::string_view text) const {
    if (!color_) return std::string(text);
    return std::string("\033[") + code + "m" + std::string(text) + "\033[0m";
}

void Console::title(std::string_view command, std::string_view detail) {
    out_ << bold(command);
    if (!detail.empty()) out_ << "  " << detail;
    out_ << '\n';
}

void Console::section(std::string_view name) {
    out_ << '\n' << bold(name) << '\n';
}

void Console::field(std::string_view label, std::string_view value) {
    out_ << "  " << pad(label, kLabelWidth, Table::Align::Left) << value << '\n';
}

void Console::line(std::string_view text) {
    out_ << text << '\n';
}

void Console::blank() {
    out_ << '\n';
}

void Console::summary(const std::vector<std::string>& items) {
    out_ << '\n' << dim(std::string(kRuleWidth, '-')) << '\n' << accent("==>");
    for (const auto& item : items) out_ << "   " << item;
    out_ << '\n';
}

void Table::row(std::vector<std::string> cells, int depth, Emphasis emphasis) {
    cells.resize(columns_.size());
    cells.front().insert(0, std::string(2 * static_cast<std::size_t>(depth), ' '));
    rows_.push_back({std::move(cells), depth, emphasis});
}

void Table::print(Console& console) const {
    std::vector<std::size_t> widths(columns_.size());
    for (std::size_t c = 0; c < columns_.size(); ++c) {
        widths[c] = width(columns_[c].heading);
        for (const auto& row : rows_) widths[c] = std::max(widths[c], width(row.cells[c]));
    }
    auto render = [&](const std::vector<std::string>& cells) {
        std::string text = "  ";
        for (std::size_t c = 0; c < cells.size(); ++c) {
            if (c) text += "   ";
            text += pad(cells[c], widths[c], columns_[c].align);
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        return text;
    };
    std::vector<std::string> headings;
    for (const auto& column : columns_) headings.push_back(column.heading);
    console.line(console.bold(render(headings)));
    for (const auto& row : rows_) {
        const auto text = render(row.cells);
        switch (row.emphasis) {
            case Emphasis::Bold: console.line(console.bold(text)); break;
            case Emphasis::Dim: console.line(console.dim(text)); break;
            case Emphasis::None: console.line(text); break;
        }
    }
}

}  // namespace qoiw::cli
