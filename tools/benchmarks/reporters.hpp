#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace othello::benchmarks {

enum class ColumnAlign {
    Left,
    Right,
};

struct ColumnSpec {
    std::string_view title;
    int width = 0;
    ColumnAlign align = ColumnAlign::Right;
};

void print_header_row(std::ostream& output, std::span<const ColumnSpec> columns);

} // namespace othello::benchmarks
