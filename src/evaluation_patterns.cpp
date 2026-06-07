#include "evaluation_internal.hpp"

#include <othello/evaluation_patterns.hpp>

#include <array>
#include <cstddef>

namespace othello {
namespace {

using evaluation_detail::square_bit;

struct Corner2x3PatternSpec {
    Corner2x3PatternCorner corner = Corner2x3PatternCorner::A1;
    std::array<int, 6> square_indexes{};
};

struct Corner3x3PatternSpec {
    Corner3x3PatternCorner corner = Corner3x3PatternCorner::A1;
    std::array<int, 9> square_indexes{};
};

struct Edge8PatternSpec {
    Edge8PatternEdge edge = Edge8PatternEdge::Top;
    std::array<int, 8> square_indexes{};
};

struct EdgeX10PatternSpec {
    EdgeX10PatternEdge edge = EdgeX10PatternEdge::Top;
    std::array<int, 10> square_indexes{};
};

struct Row8PatternSpec {
    Row8PatternRow row = Row8PatternRow::Rank1;
    std::array<int, 8> square_indexes{};
};

struct Column8PatternSpec {
    Column8PatternColumn column = Column8PatternColumn::FileA;
    std::array<int, 8> square_indexes{};
};

template <std::size_t N>
struct IndexedPatternSpec {
    int instance = 0;
    std::array<int, N> square_indexes{};
};

struct Diagonal8PatternSpec {
    Diagonal8PatternDiagonal diagonal = Diagonal8PatternDiagonal::A1H8;
    std::array<int, 8> square_indexes{};
};

struct InnerRow8PatternSpec {
    InnerRow8PatternLine line = InnerRow8PatternLine::Top;
    std::array<int, 8> square_indexes{};
};

struct Corner2x4PatternSpec {
    Corner2x4PatternCorner corner = Corner2x4PatternCorner::A1;
    std::array<int, 8> square_indexes{};
};

struct PatternIndexContext {
    Bitboard own = 0;
    Bitboard opponent = 0;
};

// Canonical order is side-relative and mirrored for each corner:
// corner, horizontal C-square, horizontal far edge, vertical C-square, X-square,
// inner support. For a1 this is a1, b1, c1, a2, b2, c2.
constexpr std::array<Corner2x3PatternSpec, 4> corner_2x3_pattern_specs{{
    {.corner = Corner2x3PatternCorner::A1, .square_indexes = {0, 1, 2, 8, 9, 10}},
    {.corner = Corner2x3PatternCorner::H1, .square_indexes = {7, 6, 5, 15, 14, 13}},
    {.corner = Corner2x3PatternCorner::A8, .square_indexes = {56, 57, 58, 48, 49, 50}},
    {.corner = Corner2x3PatternCorner::H8, .square_indexes = {63, 62, 61, 55, 54, 53}},
}};

// Canonical 3x3 corner order mirrors the 2x3 order and adds the third rank/file.
constexpr std::array<Corner3x3PatternSpec, 4> corner_3x3_pattern_specs{{
    {.corner = Corner3x3PatternCorner::A1,
     .square_indexes = {0, 1, 2, 8, 9, 10, 16, 17, 18}},
    {.corner = Corner3x3PatternCorner::H1,
     .square_indexes = {7, 6, 5, 15, 14, 13, 23, 22, 21}},
    {.corner = Corner3x3PatternCorner::A8,
     .square_indexes = {56, 57, 58, 48, 49, 50, 40, 41, 42}},
    {.corner = Corner3x3PatternCorner::H8,
     .square_indexes = {63, 62, 61, 55, 54, 53, 47, 46, 45}},
}};

// Canonical edge order is stable and side-relative through cell state encoding:
// top a1..h1, bottom a8..h8, left a1..a8, right h1..h8.
constexpr std::array<Edge8PatternSpec, 4> edge_8_pattern_specs{{
    {.edge = Edge8PatternEdge::Top, .square_indexes = {0, 1, 2, 3, 4, 5, 6, 7}},
    {.edge = Edge8PatternEdge::Bottom, .square_indexes = {56, 57, 58, 59, 60, 61, 62, 63}},
    {.edge = Edge8PatternEdge::Left, .square_indexes = {0, 8, 16, 24, 32, 40, 48, 56}},
    {.edge = Edge8PatternEdge::Right, .square_indexes = {7, 15, 23, 31, 39, 47, 55, 63}},
}};

// Edge context keeps the full edge and appends the two adjacent X-squares.
constexpr std::array<EdgeX10PatternSpec, 4> edge_x_10_pattern_specs{{
    {.edge = EdgeX10PatternEdge::Top,
     .square_indexes = {0, 1, 2, 3, 4, 5, 6, 7, 9, 14}},
    {.edge = EdgeX10PatternEdge::Bottom,
     .square_indexes = {56, 57, 58, 59, 60, 61, 62, 63, 49, 54}},
    {.edge = EdgeX10PatternEdge::Left,
     .square_indexes = {0, 8, 16, 24, 32, 40, 48, 56, 9, 49}},
    {.edge = EdgeX10PatternEdge::Right,
     .square_indexes = {7, 15, 23, 31, 39, 47, 55, 63, 14, 54}},
}};

constexpr std::array<Row8PatternSpec, 8> row_8_pattern_specs{{
    {.row = Row8PatternRow::Rank1, .square_indexes = {0, 1, 2, 3, 4, 5, 6, 7}},
    {.row = Row8PatternRow::Rank2, .square_indexes = {8, 9, 10, 11, 12, 13, 14, 15}},
    {.row = Row8PatternRow::Rank3, .square_indexes = {16, 17, 18, 19, 20, 21, 22, 23}},
    {.row = Row8PatternRow::Rank4, .square_indexes = {24, 25, 26, 27, 28, 29, 30, 31}},
    {.row = Row8PatternRow::Rank5, .square_indexes = {32, 33, 34, 35, 36, 37, 38, 39}},
    {.row = Row8PatternRow::Rank6, .square_indexes = {40, 41, 42, 43, 44, 45, 46, 47}},
    {.row = Row8PatternRow::Rank7, .square_indexes = {48, 49, 50, 51, 52, 53, 54, 55}},
    {.row = Row8PatternRow::Rank8, .square_indexes = {56, 57, 58, 59, 60, 61, 62, 63}},
}};

constexpr std::array<Column8PatternSpec, 8> column_8_pattern_specs{{
    {.column = Column8PatternColumn::FileA, .square_indexes = {0, 8, 16, 24, 32, 40, 48, 56}},
    {.column = Column8PatternColumn::FileB, .square_indexes = {1, 9, 17, 25, 33, 41, 49, 57}},
    {.column = Column8PatternColumn::FileC, .square_indexes = {2, 10, 18, 26, 34, 42, 50, 58}},
    {.column = Column8PatternColumn::FileD, .square_indexes = {3, 11, 19, 27, 35, 43, 51, 59}},
    {.column = Column8PatternColumn::FileE, .square_indexes = {4, 12, 20, 28, 36, 44, 52, 60}},
    {.column = Column8PatternColumn::FileF, .square_indexes = {5, 13, 21, 29, 37, 45, 53, 61}},
    {.column = Column8PatternColumn::FileG, .square_indexes = {6, 14, 22, 30, 38, 46, 54, 62}},
    {.column = Column8PatternColumn::FileH, .square_indexes = {7, 15, 23, 31, 39, 47, 55, 63}},
}};

constexpr std::array<IndexedPatternSpec<4>, 4> diagonal_4_pattern_specs{{
    {.instance = 0, .square_indexes = {4, 13, 22, 31}},
    {.instance = 1, .square_indexes = {32, 41, 50, 59}},
    {.instance = 2, .square_indexes = {3, 10, 17, 24}},
    {.instance = 3, .square_indexes = {39, 46, 53, 60}},
}};

constexpr std::array<IndexedPatternSpec<5>, 4> diagonal_5_pattern_specs{{
    {.instance = 0, .square_indexes = {3, 12, 21, 30, 39}},
    {.instance = 1, .square_indexes = {24, 33, 42, 51, 60}},
    {.instance = 2, .square_indexes = {4, 11, 18, 25, 32}},
    {.instance = 3, .square_indexes = {31, 38, 45, 52, 59}},
}};

constexpr std::array<IndexedPatternSpec<6>, 4> diagonal_6_pattern_specs{{
    {.instance = 0, .square_indexes = {2, 11, 20, 29, 38, 47}},
    {.instance = 1, .square_indexes = {16, 25, 34, 43, 52, 61}},
    {.instance = 2, .square_indexes = {5, 12, 19, 26, 33, 40}},
    {.instance = 3, .square_indexes = {23, 30, 37, 44, 51, 58}},
}};

constexpr std::array<IndexedPatternSpec<7>, 4> diagonal_7_pattern_specs{{
    {.instance = 0, .square_indexes = {1, 10, 19, 28, 37, 46, 55}},
    {.instance = 1, .square_indexes = {8, 17, 26, 35, 44, 53, 62}},
    {.instance = 2, .square_indexes = {6, 13, 20, 27, 34, 41, 48}},
    {.instance = 3, .square_indexes = {15, 22, 29, 36, 43, 50, 57}},
}};

constexpr std::array<Diagonal8PatternSpec, 2> diagonal_8_pattern_specs{{
    {.diagonal = Diagonal8PatternDiagonal::A1H8,
     .square_indexes = {0, 9, 18, 27, 36, 45, 54, 63}},
    {.diagonal = Diagonal8PatternDiagonal::H1A8,
     .square_indexes = {7, 14, 21, 28, 35, 42, 49, 56}},
}};

// The second rank/file lines capture interior pressure next to edge and corner
// context without expanding to every row and column.
constexpr std::array<InnerRow8PatternSpec, 4> inner_row_8_pattern_specs{{
    {.line = InnerRow8PatternLine::Top, .square_indexes = {8, 9, 10, 11, 12, 13, 14, 15}},
    {.line = InnerRow8PatternLine::Bottom,
     .square_indexes = {48, 49, 50, 51, 52, 53, 54, 55}},
    {.line = InnerRow8PatternLine::Left, .square_indexes = {1, 9, 17, 25, 33, 41, 49, 57}},
    {.line = InnerRow8PatternLine::Right, .square_indexes = {6, 14, 22, 30, 38, 46, 54, 62}},
}};

constexpr std::array<Corner2x4PatternSpec, 4> corner_2x4_pattern_specs{{
    {.corner = Corner2x4PatternCorner::A1, .square_indexes = {0, 1, 2, 3, 8, 9, 10, 11}},
    {.corner = Corner2x4PatternCorner::H1, .square_indexes = {7, 6, 5, 4, 15, 14, 13, 12}},
    {.corner = Corner2x4PatternCorner::A8, .square_indexes = {56, 57, 58, 59, 48, 49, 50, 51}},
    {.corner = Corner2x4PatternCorner::H8, .square_indexes = {63, 62, 61, 60, 55, 54, 53, 52}},
}};

[[nodiscard]] constexpr const Corner2x3PatternSpec&
corner_2x3_pattern_spec(Corner2x3PatternCorner corner) noexcept {
    switch (corner) {
    case Corner2x3PatternCorner::A1:
        return corner_2x3_pattern_specs[0];
    case Corner2x3PatternCorner::H1:
        return corner_2x3_pattern_specs[1];
    case Corner2x3PatternCorner::A8:
        return corner_2x3_pattern_specs[2];
    case Corner2x3PatternCorner::H8:
        return corner_2x3_pattern_specs[3];
    }

    return corner_2x3_pattern_specs[0];
}

[[nodiscard]] constexpr const Corner3x3PatternSpec&
corner_3x3_pattern_spec(Corner3x3PatternCorner corner) noexcept {
    switch (corner) {
    case Corner3x3PatternCorner::A1:
        return corner_3x3_pattern_specs[0];
    case Corner3x3PatternCorner::H1:
        return corner_3x3_pattern_specs[1];
    case Corner3x3PatternCorner::A8:
        return corner_3x3_pattern_specs[2];
    case Corner3x3PatternCorner::H8:
        return corner_3x3_pattern_specs[3];
    }

    return corner_3x3_pattern_specs[0];
}

[[nodiscard]] constexpr const Edge8PatternSpec&
edge_8_pattern_spec(Edge8PatternEdge edge) noexcept {
    switch (edge) {
    case Edge8PatternEdge::Top:
        return edge_8_pattern_specs[0];
    case Edge8PatternEdge::Bottom:
        return edge_8_pattern_specs[1];
    case Edge8PatternEdge::Left:
        return edge_8_pattern_specs[2];
    case Edge8PatternEdge::Right:
        return edge_8_pattern_specs[3];
    }

    return edge_8_pattern_specs[0];
}

[[nodiscard]] constexpr const EdgeX10PatternSpec&
edge_x_10_pattern_spec(EdgeX10PatternEdge edge) noexcept {
    switch (edge) {
    case EdgeX10PatternEdge::Top:
        return edge_x_10_pattern_specs[0];
    case EdgeX10PatternEdge::Bottom:
        return edge_x_10_pattern_specs[1];
    case EdgeX10PatternEdge::Left:
        return edge_x_10_pattern_specs[2];
    case EdgeX10PatternEdge::Right:
        return edge_x_10_pattern_specs[3];
    }

    return edge_x_10_pattern_specs[0];
}

[[nodiscard]] constexpr const Row8PatternSpec&
row_8_pattern_spec(Row8PatternRow row) noexcept {
    switch (row) {
    case Row8PatternRow::Rank1:
        return row_8_pattern_specs[0];
    case Row8PatternRow::Rank2:
        return row_8_pattern_specs[1];
    case Row8PatternRow::Rank3:
        return row_8_pattern_specs[2];
    case Row8PatternRow::Rank4:
        return row_8_pattern_specs[3];
    case Row8PatternRow::Rank5:
        return row_8_pattern_specs[4];
    case Row8PatternRow::Rank6:
        return row_8_pattern_specs[5];
    case Row8PatternRow::Rank7:
        return row_8_pattern_specs[6];
    case Row8PatternRow::Rank8:
        return row_8_pattern_specs[7];
    }

    return row_8_pattern_specs[0];
}

[[nodiscard]] constexpr const Column8PatternSpec&
column_8_pattern_spec(Column8PatternColumn column) noexcept {
    switch (column) {
    case Column8PatternColumn::FileA:
        return column_8_pattern_specs[0];
    case Column8PatternColumn::FileB:
        return column_8_pattern_specs[1];
    case Column8PatternColumn::FileC:
        return column_8_pattern_specs[2];
    case Column8PatternColumn::FileD:
        return column_8_pattern_specs[3];
    case Column8PatternColumn::FileE:
        return column_8_pattern_specs[4];
    case Column8PatternColumn::FileF:
        return column_8_pattern_specs[5];
    case Column8PatternColumn::FileG:
        return column_8_pattern_specs[6];
    case Column8PatternColumn::FileH:
        return column_8_pattern_specs[7];
    }

    return column_8_pattern_specs[0];
}

template <std::size_t N, std::size_t M>
[[nodiscard]] constexpr const IndexedPatternSpec<N>&
indexed_pattern_spec(const std::array<IndexedPatternSpec<N>, M>& specs, int instance) noexcept {
    if (instance < 0 || instance >= static_cast<int>(M)) {
        return specs[0];
    }
    return specs[static_cast<std::size_t>(instance)];
}

[[nodiscard]] constexpr const Diagonal8PatternSpec&
diagonal_8_pattern_spec(Diagonal8PatternDiagonal diagonal) noexcept {
    switch (diagonal) {
    case Diagonal8PatternDiagonal::A1H8:
        return diagonal_8_pattern_specs[0];
    case Diagonal8PatternDiagonal::H1A8:
        return diagonal_8_pattern_specs[1];
    }

    return diagonal_8_pattern_specs[0];
}

[[nodiscard]] constexpr const InnerRow8PatternSpec&
inner_row_8_pattern_spec(InnerRow8PatternLine line) noexcept {
    switch (line) {
    case InnerRow8PatternLine::Top:
        return inner_row_8_pattern_specs[0];
    case InnerRow8PatternLine::Bottom:
        return inner_row_8_pattern_specs[1];
    case InnerRow8PatternLine::Left:
        return inner_row_8_pattern_specs[2];
    case InnerRow8PatternLine::Right:
        return inner_row_8_pattern_specs[3];
    }

    return inner_row_8_pattern_specs[0];
}

[[nodiscard]] constexpr const Corner2x4PatternSpec&
corner_2x4_pattern_spec(Corner2x4PatternCorner corner) noexcept {
    switch (corner) {
    case Corner2x4PatternCorner::A1:
        return corner_2x4_pattern_specs[0];
    case Corner2x4PatternCorner::H1:
        return corner_2x4_pattern_specs[1];
    case Corner2x4PatternCorner::A8:
        return corner_2x4_pattern_specs[2];
    case Corner2x4PatternCorner::H8:
        return corner_2x4_pattern_specs[3];
    }

    return corner_2x4_pattern_specs[0];
}

template <std::size_t N>
[[nodiscard]] int pattern_index_for_squares(const PatternIndexContext& context,
                                            const std::array<int, N>& square_indexes) noexcept {
    int index = 0;
    int place_value = 1;
    for (const int square_index : square_indexes) {
        const Bitboard square = square_bit(square_index);
        int state = 0;
        if ((context.own & square) != 0) {
            state = 1;
        } else if ((context.opponent & square) != 0) {
            state = 2;
        }
        index += state * place_value;
        place_value *= 3;
    }
    return index;
}

[[nodiscard]] PatternIndexContext pattern_index_context(const Board& board,
                                                        Side side) noexcept {
    return PatternIndexContext{
        .own = board.discs(side),
        .opponent = board.discs(opponent(side)),
    };
}

[[nodiscard]] constexpr PatternIndexContext pattern_index_context(Bitboard player,
                                                                  Bitboard opponent) noexcept {
    return PatternIndexContext{
        .own = player,
        .opponent = opponent,
    };
}

template <std::size_t N>
[[nodiscard]] int pattern_index_for_squares(const Board& board, Side side,
                                            const std::array<int, N>& square_indexes) noexcept {
    return pattern_index_for_squares(pattern_index_context(board, side), square_indexes);
}

[[nodiscard]] constexpr int pattern_signed_state(int state) noexcept {
    if (state == 1) {
        return 1;
    }
    if (state == 2) {
        return -1;
    }
    return 0;
}

[[nodiscard]] constexpr int clamp_corner_2x3_pattern_value(int value) noexcept {
    if (value < -6) {
        return -6;
    }
    if (value > 6) {
        return 6;
    }
    return value;
}

[[nodiscard]] constexpr int corner_2x3_pattern_state_at(int index, int cell) noexcept {
    for (int current = 0; current < cell; ++current) {
        index /= 3;
    }
    return index % 3;
}

[[nodiscard]] constexpr int corner_2x3_rule_value(int index) noexcept {
    const int corner = corner_2x3_pattern_state_at(index, 0);
    const int horizontal_c = corner_2x3_pattern_state_at(index, 1);
    const int horizontal_far = corner_2x3_pattern_state_at(index, 2);
    const int vertical_c = corner_2x3_pattern_state_at(index, 3);
    const int x_square = corner_2x3_pattern_state_at(index, 4);
    const int inner_support = corner_2x3_pattern_state_at(index, 5);

    int value = 0;
    if (corner == 1) {
        value += 4;
    } else if (corner == 2) {
        value -= 4;
    }

    const int adjacent_support =
        pattern_signed_state(horizontal_c) + pattern_signed_state(vertical_c);
    if (corner == 0) {
        value -= adjacent_support;
        value -= 3 * pattern_signed_state(x_square);
        value -= pattern_signed_state(horizontal_far);
    } else {
        value += adjacent_support;
        value += pattern_signed_state(horizontal_far);
        value += pattern_signed_state(inner_support);
    }

    return clamp_corner_2x3_pattern_value(value);
}

[[nodiscard]] constexpr std::array<int, corner_2x3_pattern_table_size>
make_corner_2x3_pattern_table() noexcept {
    std::array<int, corner_2x3_pattern_table_size> table{};
    for (int index = 0; index < corner_2x3_pattern_table_size; ++index) {
        table[index] = corner_2x3_rule_value(index);
    }
    return table;
}

constexpr std::array<int, corner_2x3_pattern_table_size> corner_2x3_pattern_table =
    make_corner_2x3_pattern_table();

[[nodiscard]] constexpr int clamp_edge_8_pattern_value(int value) noexcept {
    if (value < -10) {
        return -10;
    }
    if (value > 10) {
        return 10;
    }
    return value;
}

[[nodiscard]] constexpr int edge_8_rule_value(int index) noexcept {
    const int cell0 = pattern_signed_state(index % 3);
    const int cell1 = pattern_signed_state((index / 3) % 3);
    const int cell2 = pattern_signed_state((index / 9) % 3);
    const int cell3 = pattern_signed_state((index / 27) % 3);
    const int cell4 = pattern_signed_state((index / 81) % 3);
    const int cell5 = pattern_signed_state((index / 243) % 3);
    const int cell6 = pattern_signed_state((index / 729) % 3);
    const int cell7 = pattern_signed_state((index / 2187) % 3);
    const int left_corner = cell0;
    const int right_corner = cell7;
    const int left_c_square = cell1;
    const int right_c_square = cell6;

    int value = 2 * (left_corner + right_corner);

    if (left_corner != 0) {
        int chain = 1;
        if (cell1 == left_corner) {
            ++chain;
            if (cell2 == left_corner) {
                ++chain;
                if (cell3 == left_corner) {
                    ++chain;
                    if (cell4 == left_corner) {
                        ++chain;
                    }
                }
            }
        }
        value += left_corner * (chain > 4 ? 4 : chain);
        value += left_c_square;
    } else {
        value -= 2 * left_c_square;
        if (left_c_square == 0) {
            value -= cell2;
        }
    }

    if (right_corner != 0) {
        int chain = 1;
        if (cell6 == right_corner) {
            ++chain;
            if (cell5 == right_corner) {
                ++chain;
                if (cell4 == right_corner) {
                    ++chain;
                    if (cell3 == right_corner) {
                        ++chain;
                    }
                }
            }
        }
        value += right_corner * (chain > 4 ? 4 : chain);
        value += right_c_square;
    } else {
        value -= 2 * right_c_square;
        if (right_c_square == 0) {
            value -= cell5;
        }
    }

    if (left_corner == 0 && right_corner == 0) {
        value -= cell1 + cell2 + cell3 + cell4 + cell5 + cell6;
    }

    if (cell0 == 1 && cell1 == 1 && cell2 == 1 && cell3 == 1 && cell4 == 1 &&
        cell5 == 1 && cell6 == 1 && cell7 == 1) {
        value += 3;
    } else if (cell0 == -1 && cell1 == -1 && cell2 == -1 && cell3 == -1 &&
               cell4 == -1 && cell5 == -1 && cell6 == -1 && cell7 == -1) {
        value -= 3;
    }

    return clamp_edge_8_pattern_value(value);
}

[[nodiscard]] constexpr std::array<int, edge_8_pattern_table_size>
make_edge_8_pattern_table() noexcept {
    std::array<int, edge_8_pattern_table_size> table{};
    for (int index = 0; index < edge_8_pattern_table_size; ++index) {
        table[index] = edge_8_rule_value(index);
    }
    return table;
}

constexpr std::array<int, edge_8_pattern_table_size> edge_8_pattern_table =
    make_edge_8_pattern_table();

} // namespace

int corner_2x3_pattern_index(const Board& board, Side side,
                             Corner2x3PatternCorner corner) noexcept {
    const Corner2x3PatternSpec& spec = corner_2x3_pattern_spec(corner);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int corner_2x3_pattern_table_value(int index) noexcept {
    if (index < 0 || index >= corner_2x3_pattern_table_size) {
        return 0;
    }
    return corner_2x3_pattern_table[static_cast<std::size_t>(index)];
}

int corner_2x3_pattern_value(const Board& board, Side side) noexcept {
    int value = 0;
    for (const Corner2x3PatternSpec& spec : corner_2x3_pattern_specs) {
        value += corner_2x3_pattern_table_value(
            corner_2x3_pattern_index(board, side, spec.corner));
    }
    return value;
}

int corner_2x3_pattern_score(const Board& board, Side side) noexcept {
    return corner_2x3_pattern_value(board, side);
}

int corner_3x3_pattern_index(const Board& board, Side side,
                             Corner3x3PatternCorner corner) noexcept {
    const Corner3x3PatternSpec& spec = corner_3x3_pattern_spec(corner);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int edge_8_pattern_index(const Board& board, Side side, Edge8PatternEdge edge) noexcept {
    const Edge8PatternSpec& spec = edge_8_pattern_spec(edge);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int edge_8_pattern_table_value(int index) noexcept {
    if (index < 0 || index >= edge_8_pattern_table_size) {
        return 0;
    }
    return edge_8_pattern_table[static_cast<std::size_t>(index)];
}

int edge_8_pattern_value(const Board& board, Side side) noexcept {
    int value = 0;
    for (const Edge8PatternSpec& spec : edge_8_pattern_specs) {
        value += edge_8_pattern_table_value(edge_8_pattern_index(board, side, spec.edge));
    }
    return value;
}

int edge_8_pattern_score(const Board& board, Side side) noexcept {
    return edge_8_pattern_value(board, side);
}

int edge_x_10_pattern_index(const Board& board, Side side, EdgeX10PatternEdge edge) noexcept {
    const EdgeX10PatternSpec& spec = edge_x_10_pattern_spec(edge);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int row_8_pattern_index(const Board& board, Side side, Row8PatternRow row) noexcept {
    const Row8PatternSpec& spec = row_8_pattern_spec(row);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int column_8_pattern_index(const Board& board, Side side,
                           Column8PatternColumn column) noexcept {
    const Column8PatternSpec& spec = column_8_pattern_spec(column);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int diagonal_4_pattern_index(const Board& board, Side side, int instance) noexcept {
    const IndexedPatternSpec<4>& spec = indexed_pattern_spec(diagonal_4_pattern_specs, instance);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int diagonal_5_pattern_index(const Board& board, Side side, int instance) noexcept {
    const IndexedPatternSpec<5>& spec = indexed_pattern_spec(diagonal_5_pattern_specs, instance);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int diagonal_6_pattern_index(const Board& board, Side side, int instance) noexcept {
    const IndexedPatternSpec<6>& spec = indexed_pattern_spec(diagonal_6_pattern_specs, instance);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int diagonal_7_pattern_index(const Board& board, Side side, int instance) noexcept {
    const IndexedPatternSpec<7>& spec = indexed_pattern_spec(diagonal_7_pattern_specs, instance);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int diagonal_8_pattern_index(const Board& board, Side side,
                             Diagonal8PatternDiagonal diagonal) noexcept {
    const Diagonal8PatternSpec& spec = diagonal_8_pattern_spec(diagonal);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int inner_row_8_pattern_index(const Board& board, Side side,
                              InnerRow8PatternLine line) noexcept {
    const InnerRow8PatternSpec& spec = inner_row_8_pattern_spec(line);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int corner_2x4_pattern_index(const Board& board, Side side,
                             Corner2x4PatternCorner corner) noexcept {
    const Corner2x4PatternSpec& spec = corner_2x4_pattern_spec(corner);
    return pattern_index_for_squares(board, side, spec.square_indexes);
}

int evaluation_pattern_table_value(const Board& board, Side side,
                                   const PatternTableBundle& tables) noexcept {
    const PatternIndexContext context = pattern_index_context(board, side);
    int value = 0;
    for (const Corner2x3PatternSpec& spec : corner_2x3_pattern_specs) {
        value += tables.corner_2x3[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Corner3x3PatternSpec& spec : corner_3x3_pattern_specs) {
        value += tables.corner_3x3[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Edge8PatternSpec& spec : edge_8_pattern_specs) {
        value += tables.edge_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const EdgeX10PatternSpec& spec : edge_x_10_pattern_specs) {
        value += tables.edge_x_10[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Row8PatternSpec& spec : row_8_pattern_specs) {
        value += tables.row_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Column8PatternSpec& spec : column_8_pattern_specs) {
        value += tables.column_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<4>& spec : diagonal_4_pattern_specs) {
        value += tables.diagonal_4[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<5>& spec : diagonal_5_pattern_specs) {
        value += tables.diagonal_5[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<6>& spec : diagonal_6_pattern_specs) {
        value += tables.diagonal_6[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<7>& spec : diagonal_7_pattern_specs) {
        value += tables.diagonal_7[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Diagonal8PatternSpec& spec : diagonal_8_pattern_specs) {
        value += tables.diagonal_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const InnerRow8PatternSpec& spec : inner_row_8_pattern_specs) {
        value += tables.inner_row_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Corner2x4PatternSpec& spec : corner_2x4_pattern_specs) {
        value += tables.corner_2x4[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    return value;
}

int evaluation_pattern_table_score(const Board& board, Side side,
                                   const PatternTableBundle& tables) noexcept {
    return evaluation_pattern_table_value(board, side, tables);
}

namespace evaluation_detail {

int corner_2x3_pattern_score(Bitboard player, Bitboard opponent) noexcept {
    const PatternIndexContext context = pattern_index_context(player, opponent);
    int value = 0;
    for (const Corner2x3PatternSpec& spec : corner_2x3_pattern_specs) {
        value += corner_2x3_pattern_table_value(
            pattern_index_for_squares(context, spec.square_indexes));
    }
    return value;
}

int edge_8_pattern_score(Bitboard player, Bitboard opponent) noexcept {
    const PatternIndexContext context = pattern_index_context(player, opponent);
    int value = 0;
    for (const Edge8PatternSpec& spec : edge_8_pattern_specs) {
        value += edge_8_pattern_table_value(
            pattern_index_for_squares(context, spec.square_indexes));
    }
    return value;
}

int evaluation_pattern_table_score(Bitboard player, Bitboard opponent,
                                   const PatternTableBundle& tables) noexcept {
    const PatternIndexContext context = pattern_index_context(player, opponent);
    int value = 0;
    for (const Corner2x3PatternSpec& spec : corner_2x3_pattern_specs) {
        value += tables.corner_2x3[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Corner3x3PatternSpec& spec : corner_3x3_pattern_specs) {
        value += tables.corner_3x3[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Edge8PatternSpec& spec : edge_8_pattern_specs) {
        value += tables.edge_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const EdgeX10PatternSpec& spec : edge_x_10_pattern_specs) {
        value += tables.edge_x_10[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Row8PatternSpec& spec : row_8_pattern_specs) {
        value += tables.row_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Column8PatternSpec& spec : column_8_pattern_specs) {
        value += tables.column_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<4>& spec : diagonal_4_pattern_specs) {
        value += tables.diagonal_4[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<5>& spec : diagonal_5_pattern_specs) {
        value += tables.diagonal_5[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<6>& spec : diagonal_6_pattern_specs) {
        value += tables.diagonal_6[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const IndexedPatternSpec<7>& spec : diagonal_7_pattern_specs) {
        value += tables.diagonal_7[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Diagonal8PatternSpec& spec : diagonal_8_pattern_specs) {
        value += tables.diagonal_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const InnerRow8PatternSpec& spec : inner_row_8_pattern_specs) {
        value += tables.inner_row_8[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    for (const Corner2x4PatternSpec& spec : corner_2x4_pattern_specs) {
        value += tables.corner_2x4[static_cast<std::size_t>(
            pattern_index_for_squares(context, spec.square_indexes))];
    }
    return value;
}

} // namespace evaluation_detail

} // namespace othello
