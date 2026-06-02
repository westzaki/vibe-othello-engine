#pragma once

#include <othello/types.hpp>

#include <array>
#include <cstdint>

namespace othello {

struct Board;

enum class Corner2x3PatternCorner {
    A1,
    H1,
    A8,
    H8,
};

inline constexpr int corner_2x3_pattern_table_size = 729;

enum class Corner3x3PatternCorner {
    A1,
    H1,
    A8,
    H8,
};

inline constexpr int corner_3x3_pattern_table_size = 19683;

enum class Edge8PatternEdge {
    Top,
    Bottom,
    Left,
    Right,
};

inline constexpr int edge_8_pattern_table_size = 6561;

enum class EdgeX10PatternEdge {
    Top,
    Bottom,
    Left,
    Right,
};

inline constexpr int edge_x_10_pattern_table_size = 59049;

enum class Diagonal8PatternDiagonal {
    A1H8,
    H1A8,
};

inline constexpr int diagonal_8_pattern_table_size = 6561;

enum class InnerRow8PatternLine {
    Top,
    Bottom,
    Left,
    Right,
};

inline constexpr int inner_row_8_pattern_table_size = 6561;

struct PatternTableBundle {
    std::array<std::int16_t, corner_2x3_pattern_table_size> corner_2x3{};
    std::array<std::int16_t, corner_3x3_pattern_table_size> corner_3x3{};
    std::array<std::int16_t, edge_8_pattern_table_size> edge_8{};
    std::array<std::int16_t, edge_x_10_pattern_table_size> edge_x_10{};
    std::array<std::int16_t, diagonal_8_pattern_table_size> diagonal_8{};
    std::array<std::int16_t, inner_row_8_pattern_table_size> inner_row_8{};

    [[nodiscard]] friend bool operator==(const PatternTableBundle&,
                                         const PatternTableBundle&) = default;
};

using EvaluationPatternTables = PatternTableBundle;

[[nodiscard]] int corner_2x3_pattern_index(const Board& board, Side side,
                                           Corner2x3PatternCorner corner) noexcept;
[[nodiscard]] int corner_2x3_pattern_table_value(int index) noexcept;
[[nodiscard]] int corner_2x3_pattern_value(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_2x3_pattern_score(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_3x3_pattern_index(const Board& board, Side side,
                                           Corner3x3PatternCorner corner) noexcept;
[[nodiscard]] int edge_8_pattern_index(const Board& board, Side side,
                                       Edge8PatternEdge edge) noexcept;
[[nodiscard]] int edge_8_pattern_table_value(int index) noexcept;
[[nodiscard]] int edge_8_pattern_value(const Board& board, Side side) noexcept;
[[nodiscard]] int edge_8_pattern_score(const Board& board, Side side) noexcept;
[[nodiscard]] int edge_x_10_pattern_index(const Board& board, Side side,
                                          EdgeX10PatternEdge edge) noexcept;
[[nodiscard]] int diagonal_8_pattern_index(const Board& board, Side side,
                                           Diagonal8PatternDiagonal diagonal) noexcept;
[[nodiscard]] int inner_row_8_pattern_index(const Board& board, Side side,
                                            InnerRow8PatternLine line) noexcept;
[[nodiscard]] int evaluation_pattern_table_value(
    const Board& board, Side side, const PatternTableBundle& tables) noexcept;
[[nodiscard]] int evaluation_pattern_table_score(
    const Board& board, Side side, const PatternTableBundle& tables) noexcept;

} // namespace othello
