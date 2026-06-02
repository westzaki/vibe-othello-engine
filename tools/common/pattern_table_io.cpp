#include "common/pattern_table_io.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace othello::tools {
namespace {

[[nodiscard]] constexpr bool is_ascii_space(char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

[[nodiscard]] std::string_view trim_ascii(std::string_view text) noexcept {
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::string line_error(int line_number, std::string_view message) {
    std::ostringstream out;
    out << "line " << line_number << ": " << message;
    return out.str();
}

[[nodiscard]] PatternTableLoadResult pattern_table_error(std::string error) {
    PatternTableLoadResult result;
    result.error = std::move(error);
    return result;
}

template <std::size_t N>
[[nodiscard]] std::string assign_sparse_pattern_entry(std::array<std::int16_t, N>& table,
                                                      std::array<bool, N>& seen,
                                                      int line_number,
                                                      std::string_view family, int index,
                                                      int value) {
    if (index < 0 || index >= static_cast<int>(N)) {
        return line_error(line_number, std::string{family} + " index out of range");
    }
    const auto table_index = static_cast<std::size_t>(index);
    if (seen[table_index]) {
        return line_error(line_number, "duplicate " + std::string{family} + " index");
    }
    table[table_index] = static_cast<std::int16_t>(value);
    seen[table_index] = true;
    return {};
}

} // namespace

PatternTableLoadResult load_pattern_table_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        return pattern_table_error("failed to open pattern table: " + path.string());
    }

    auto loaded = std::make_shared<PatternTableBundle>();
    std::array<bool, corner_2x3_pattern_table_size> seen_corner{};
    std::array<bool, corner_3x3_pattern_table_size> seen_corner_3x3{};
    std::array<bool, edge_8_pattern_table_size> seen_edge{};
    std::array<bool, edge_x_10_pattern_table_size> seen_edge_x_10{};
    std::array<bool, diagonal_8_pattern_table_size> seen_diagonal{};
    std::array<bool, inner_row_8_pattern_table_size> seen_inner_row{};
    bool has_entries = false;

    int line_number = 0;
    std::string raw_line;
    while (std::getline(input, raw_line)) {
        ++line_number;
        std::string_view line{raw_line};
        if (const std::size_t comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim_ascii(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream parts{std::string{line}};
        std::string family;
        int index = 0;
        int value = 0;
        std::string trailing;
        if (!(parts >> family >> index >> value) || (parts >> trailing)) {
            return pattern_table_error(
                line_error(line_number, "expected '<family> <index> <value>'"));
        }
        if (value < std::numeric_limits<std::int16_t>::min() ||
            value > std::numeric_limits<std::int16_t>::max()) {
            return pattern_table_error(
                line_error(line_number, "pattern value outside int16 range"));
        }

        if (family == "corner_2x3") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->corner_2x3, seen_corner, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "corner_3x3") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->corner_3x3, seen_corner_3x3, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "edge_8") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->edge_8, seen_edge, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "edge_x_10") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->edge_x_10, seen_edge_x_10, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "diagonal_8") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->diagonal_8, seen_diagonal, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "inner_row_8") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->inner_row_8, seen_inner_row, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else {
            return pattern_table_error(
                line_error(line_number, "unknown pattern family: " + family));
        }
        has_entries = true;
    }

    if (input.bad()) {
        return pattern_table_error("failed to read pattern table: " + path.string());
    }
    if (!has_entries) {
        return pattern_table_error("pattern table has no entries");
    }

    PatternTableLoadResult result;
    result.tables = std::move(loaded);
    return result;
}

PatternTableLoadResult PatternTableCache::load(const std::filesystem::path& path) {
    const std::filesystem::path normalized = path.lexically_normal();
    for (const Entry& entry : entries_) {
        if (entry.path == normalized) {
            PatternTableLoadResult result;
            result.tables = entry.tables;
            return result;
        }
    }

    PatternTableLoadResult loaded = load_pattern_table_file(normalized);
    if (loaded.ok()) {
        entries_.push_back(Entry{.path = normalized, .tables = loaded.tables});
    }
    return loaded;
}

} // namespace othello::tools
