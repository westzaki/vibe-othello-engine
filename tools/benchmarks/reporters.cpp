#include "benchmarks/reporters.hpp"

#include <iomanip>
#include <ostream>

namespace othello::benchmarks {

void print_header_row(std::ostream& output, std::span<const ColumnSpec> columns) {
    for (const ColumnSpec& column : columns) {
        if (column.align == ColumnAlign::Left) {
            output << std::left;
        } else {
            output << std::right;
        }
        output << std::setw(column.width) << column.title;
    }
    output << '\n';
}

} // namespace othello::benchmarks
