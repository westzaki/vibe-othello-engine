#include "bitboard_ops.hpp"

#include <bit>
#include <othello/board.hpp>
#include <othello/evaluation.hpp>
#include <othello/rules.hpp>

#include <array>
#include <cstddef>

namespace othello {
namespace {

constexpr int terminal_score_weight = 1000;

using bitboard_detail::adjacent_squares;
using bitboard_detail::corner_squares;
using bitboard_detail::is_x_square_next_to_empty_corner;

struct CornerLocalSpec {
    int corner_index = 0;
    int x_square_index = 0;
    std::array<int, 2> c_square_indexes{};
};

struct EdgeRaySpec {
    int first_index = 0;
    int step = 0;
};

struct EdgeCornerSpec {
    int corner_index = 0;
    std::array<EdgeRaySpec, 2> rays{};
};

struct Corner2x3PatternSpec {
    Corner2x3PatternCorner corner = Corner2x3PatternCorner::A1;
    std::array<int, 6> square_indexes{};
};

struct Edge8PatternSpec {
    Edge8PatternEdge edge = Edge8PatternEdge::Top;
    std::array<int, 8> square_indexes{};
};

constexpr std::array<CornerLocalSpec, 4> corner_local_specs{{
    {.corner_index = 0, .x_square_index = 9, .c_square_indexes = {1, 8}},
    {.corner_index = 7, .x_square_index = 14, .c_square_indexes = {6, 15}},
    {.corner_index = 56, .x_square_index = 49, .c_square_indexes = {57, 48}},
    {.corner_index = 63, .x_square_index = 54, .c_square_indexes = {62, 55}},
}};

constexpr std::array<EdgeCornerSpec, 4> edge_corner_specs{{
    {.corner_index = 0, .rays = {{{.first_index = 1, .step = 1},
                                  {.first_index = 8, .step = 8}}}},
    {.corner_index = 7, .rays = {{{.first_index = 6, .step = -1},
                                  {.first_index = 15, .step = 8}}}},
    {.corner_index = 56, .rays = {{{.first_index = 57, .step = 1},
                                   {.first_index = 48, .step = -8}}}},
    {.corner_index = 63, .rays = {{{.first_index = 62, .step = -1},
                                   {.first_index = 55, .step = -8}}}},
}};

// Canonical order is side-relative and mirrored for each corner:
// corner, horizontal C-square, horizontal far edge, vertical C-square, X-square,
// inner support. For a1 this is a1, b1, c1, a2, b2, c2.
constexpr std::array<Corner2x3PatternSpec, 4> corner_2x3_pattern_specs{{
    {.corner = Corner2x3PatternCorner::A1, .square_indexes = {0, 1, 2, 8, 9, 10}},
    {.corner = Corner2x3PatternCorner::H1, .square_indexes = {7, 6, 5, 15, 14, 13}},
    {.corner = Corner2x3PatternCorner::A8, .square_indexes = {56, 57, 58, 48, 49, 50}},
    {.corner = Corner2x3PatternCorner::H8, .square_indexes = {63, 62, 61, 55, 54, 53}},
}};

// Canonical edge order is stable and side-relative through cell state encoding:
// top a1..h1, bottom a8..h8, left a1..a8, right h1..h8.
constexpr std::array<Edge8PatternSpec, 4> edge_8_pattern_specs{{
    {.edge = Edge8PatternEdge::Top, .square_indexes = {0, 1, 2, 3, 4, 5, 6, 7}},
    {.edge = Edge8PatternEdge::Bottom, .square_indexes = {56, 57, 58, 59, 60, 61, 62, 63}},
    {.edge = Edge8PatternEdge::Left, .square_indexes = {0, 8, 16, 24, 32, 40, 48, 56}},
    {.edge = Edge8PatternEdge::Right, .square_indexes = {7, 15, 23, 31, 39, 47, 55, 63}},
}};

[[nodiscard]] constexpr Bitboard square_bit(int index) noexcept {
    return Bitboard{1} << index;
}

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

[[nodiscard]] constexpr Board with_side_to_move(const Board& board, Side side) noexcept {
    return Board{
        .black = board.black,
        .white = board.white,
        .side_to_move = side,
    };
}

[[nodiscard]] EvaluationPhase phase_for_occupied_count(
    int occupied_count, const EvaluationConfig& config) noexcept {
    if (occupied_count <= config.opening_max_occupied) {
        return EvaluationPhase::Opening;
    }
    if (occupied_count <= config.midgame_max_occupied) {
        return EvaluationPhase::Midgame;
    }
    return EvaluationPhase::Late;
}

[[nodiscard]] constexpr const EvaluationFeatureWeights&
weights_for_phase(const EvaluationConfig& config, EvaluationPhase phase) noexcept {
    switch (phase) {
    case EvaluationPhase::Opening:
        return config.opening;
    case EvaluationPhase::Midgame:
        return config.midgame;
    case EvaluationPhase::Late:
        return config.late;
    }

    return config.opening;
}

[[nodiscard]] int legal_move_count(const Board& board, Side side) noexcept {
    return std::popcount(legal_moves(with_side_to_move(board, side)));
}

[[nodiscard]] int corner_count(const Board& board, Side side) noexcept {
    return std::popcount(board.discs(side) & corner_squares);
}

[[nodiscard]] int legal_corner_move_count(const Board& board, Side side) noexcept {
    return std::popcount(legal_moves(with_side_to_move(board, side)) & corner_squares);
}

[[nodiscard]] int potential_mobility_count(const Board& board, Side side) noexcept {
    const Bitboard empty_squares = ~board.occupied();
    return std::popcount(adjacent_squares(board.discs(opponent(side))) & empty_squares);
}

[[nodiscard]] int frontier_count(const Board& board, Side side) noexcept {
    const Bitboard frontier_squares = adjacent_squares(~board.occupied());
    return std::popcount(board.discs(side) & frontier_squares);
}

[[nodiscard]] int dangerous_x_square_count(const Board& board, Side side) noexcept {
    int count = 0;
    const Bitboard occupied = board.occupied();
    const Bitboard discs = board.discs(side);

    const auto count_if_dangerous = [&](int x_square_index) noexcept {
        const Bitboard x_square = Bitboard{1} << x_square_index;
        if ((discs & x_square) != 0 &&
            is_x_square_next_to_empty_corner(x_square_index, occupied)) {
            ++count;
        }
    };

    count_if_dangerous(9);  // a1 / b2
    count_if_dangerous(14); // h1 / g2
    count_if_dangerous(49); // a8 / b7
    count_if_dangerous(54); // h8 / g7
    return count;
}

[[nodiscard]] constexpr int corner_2x3_signed_state(int state) noexcept {
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
        corner_2x3_signed_state(horizontal_c) + corner_2x3_signed_state(vertical_c);
    if (corner == 0) {
        value -= adjacent_support;
        value -= 3 * corner_2x3_signed_state(x_square);
        value -= corner_2x3_signed_state(horizontal_far);
    } else {
        value += adjacent_support;
        value += corner_2x3_signed_state(horizontal_far);
        value += corner_2x3_signed_state(inner_support);
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

[[nodiscard]] constexpr int edge_8_signed_state(int state) noexcept {
    if (state == 1) {
        return 1;
    }
    if (state == 2) {
        return -1;
    }
    return 0;
}

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
    const int cell0 = edge_8_signed_state(index % 3);
    const int cell1 = edge_8_signed_state((index / 3) % 3);
    const int cell2 = edge_8_signed_state((index / 9) % 3);
    const int cell3 = edge_8_signed_state((index / 27) % 3);
    const int cell4 = edge_8_signed_state((index / 81) % 3);
    const int cell5 = edge_8_signed_state((index / 243) % 3);
    const int cell6 = edge_8_signed_state((index / 729) % 3);
    const int cell7 = edge_8_signed_state((index / 2187) % 3);
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

[[nodiscard]] int corner_local_2x3_value_for_side(const Board& board, Side side) noexcept {
    int value = 0;
    const Bitboard occupied = board.occupied();
    const Bitboard discs = board.discs(side);

    for (const CornerLocalSpec& spec : corner_local_specs) {
        const Bitboard corner = square_bit(spec.corner_index);
        if ((occupied & corner) == 0) {
            if ((discs & square_bit(spec.x_square_index)) != 0) {
                value -= 2;
            }
            for (const int c_square_index : spec.c_square_indexes) {
                if ((discs & square_bit(c_square_index)) != 0) {
                    --value;
                }
            }
            continue;
        }

        if ((discs & corner) != 0) {
            for (const int c_square_index : spec.c_square_indexes) {
                if ((discs & square_bit(c_square_index)) != 0) {
                    ++value;
                }
            }
        }
    }

    return value;
}

[[nodiscard]] int corner_local_2x3_score(const Board& board, Side side) noexcept {
    return corner_local_2x3_value_for_side(board, side) -
           corner_local_2x3_value_for_side(board, opponent(side));
}

[[nodiscard]] Bitboard anchored_edge_ray_squares(const Board& board, Side side,
                                                 const EdgeRaySpec& ray) noexcept {
    Bitboard squares = 0;
    const Bitboard discs = board.discs(side);
    int index = ray.first_index;
    for (int distance = 0; distance < 7; ++distance) {
        const Bitboard square = square_bit(index);
        if ((discs & square) == 0) {
            break;
        }
        squares |= square;
        index += ray.step;
    }
    return squares;
}

[[nodiscard]] int edge_stability_lite_value_for_side(const Board& board, Side side) noexcept {
    Bitboard stable_edge_squares = 0;
    const Bitboard discs = board.discs(side);

    // This is a stability-lite approximation: collect continuous edge rays
    // anchored at owned corners, then count unique found squares only once. A
    // ray excludes its anchor corner but may include the opposite corner when
    // the whole edge is continuous.
    for (const EdgeCornerSpec& spec : edge_corner_specs) {
        if ((discs & square_bit(spec.corner_index)) == 0) {
            continue;
        }
        for (const EdgeRaySpec& ray : spec.rays) {
            stable_edge_squares |= anchored_edge_ray_squares(board, side, ray);
        }
    }

    return std::popcount(stable_edge_squares);
}

[[nodiscard]] int edge_stability_lite_score(const Board& board, Side side) noexcept {
    return edge_stability_lite_value_for_side(board, side) -
           edge_stability_lite_value_for_side(board, opponent(side));
}

} // namespace

int evaluate_disc_difference(const Board& board, Side side) noexcept {
    return score(board, side);
}

int evaluate_mobility(const Board& board, Side side) noexcept {
    return legal_move_count(board, side) - legal_move_count(board, opponent(side));
}

int corner_2x3_pattern_index(const Board& board, Side side,
                             Corner2x3PatternCorner corner) noexcept {
    const Corner2x3PatternSpec& spec = corner_2x3_pattern_spec(corner);
    const Bitboard own_discs = board.discs(side);
    const Bitboard opponent_discs = board.discs(opponent(side));

    int index = 0;
    int place_value = 1;
    for (const int square_index : spec.square_indexes) {
        const Bitboard square = square_bit(square_index);
        int state = 0;
        if ((own_discs & square) != 0) {
            state = 1;
        } else if ((opponent_discs & square) != 0) {
            state = 2;
        }
        index += state * place_value;
        place_value *= 3;
    }
    return index;
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

int edge_8_pattern_index(const Board& board, Side side, Edge8PatternEdge edge) noexcept {
    const Edge8PatternSpec& spec = edge_8_pattern_spec(edge);
    const Bitboard own_discs = board.discs(side);
    const Bitboard opponent_discs = board.discs(opponent(side));

    int index = 0;
    int place_value = 1;
    for (const int square_index : spec.square_indexes) {
        const Bitboard square = square_bit(square_index);
        int state = 0;
        if ((own_discs & square) != 0) {
            state = 1;
        } else if ((opponent_discs & square) != 0) {
            state = 2;
        }
        index += state * place_value;
        place_value *= 3;
    }
    return index;
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

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                             const EvaluationConfig& config) noexcept {
    const int occupied_count = std::popcount(board.occupied());
    const EvaluationPhase phase = phase_for_occupied_count(occupied_count, config);
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);

    EvaluationBreakdown breakdown{
        .phase = phase,
        .occupied_count = occupied_count,
        .empty_count = 64 - occupied_count,
        .disc_difference_weight = weights.disc_difference,
        .mobility_weight = weights.mobility,
        .corner_occupancy_weight = weights.corner_occupancy,
        .potential_mobility_weight = weights.potential_mobility,
        .corner_access_weight = weights.corner_access,
        .x_square_danger_weight = weights.x_square_danger,
        .frontier_weight = weights.frontier,
        .corner_local_2x3_weight = weights.corner_local_2x3,
        .corner_2x3_pattern_weight = weights.corner_2x3_pattern,
        .edge_stability_lite_weight = weights.edge_stability_lite,
        .edge_8_pattern_weight = weights.edge_8_pattern,
        .terminal_score_weight = terminal_score_weight,
    };

    if (is_game_over(board)) {
        breakdown.terminal = true;
        breakdown.terminal_disc_difference = score(board, side);
        breakdown.terminal_score =
            breakdown.terminal_disc_difference * breakdown.terminal_score_weight;
        breakdown.total = breakdown.terminal_score;
        return breakdown;
    }

    breakdown.disc_difference = evaluate_disc_difference(board, side);
    breakdown.disc_difference_score =
        breakdown.disc_difference * breakdown.disc_difference_weight;

    breakdown.mobility = evaluate_mobility(board, side);
    breakdown.mobility_score = breakdown.mobility * breakdown.mobility_weight;

    breakdown.corner_occupancy = corner_count(board, side) - corner_count(board, opponent(side));
    breakdown.corner_occupancy_score =
        breakdown.corner_occupancy * breakdown.corner_occupancy_weight;

    // Raw feature values are positive when they are good for `side`.
    breakdown.potential_mobility =
        potential_mobility_count(board, side) - potential_mobility_count(board, opponent(side));
    breakdown.potential_mobility_score =
        breakdown.potential_mobility * breakdown.potential_mobility_weight;

    breakdown.corner_access =
        legal_corner_move_count(board, side) - legal_corner_move_count(board, opponent(side));
    breakdown.corner_access_score = breakdown.corner_access * breakdown.corner_access_weight;

    // Owning an X-square next to an empty corner is dangerous, so the diff is
    // opponent dangerous X-squares minus our dangerous X-squares.
    breakdown.x_square_danger = dangerous_x_square_count(board, opponent(side)) -
                                dangerous_x_square_count(board, side);
    breakdown.x_square_danger_score =
        breakdown.x_square_danger * breakdown.x_square_danger_weight;

    // Fewer own frontier discs is usually better.
    breakdown.frontier = frontier_count(board, opponent(side)) - frontier_count(board, side);
    breakdown.frontier_score = breakdown.frontier * breakdown.frontier_weight;

    if (breakdown.corner_local_2x3_weight != 0) {
        breakdown.corner_local_2x3 = corner_local_2x3_score(board, side);
    }
    breakdown.corner_local_2x3_score =
        breakdown.corner_local_2x3 * breakdown.corner_local_2x3_weight;

    if (breakdown.corner_2x3_pattern_weight != 0) {
        breakdown.corner_2x3_pattern = corner_2x3_pattern_score(board, side);
    }
    breakdown.corner_2x3_pattern_score =
        breakdown.corner_2x3_pattern * breakdown.corner_2x3_pattern_weight;

    if (breakdown.edge_stability_lite_weight != 0) {
        breakdown.edge_stability_lite = edge_stability_lite_score(board, side);
    }
    breakdown.edge_stability_lite_score =
        breakdown.edge_stability_lite * breakdown.edge_stability_lite_weight;

    if (breakdown.edge_8_pattern_weight != 0) {
        breakdown.edge_8_pattern = edge_8_pattern_score(board, side);
    }
    breakdown.edge_8_pattern_score =
        breakdown.edge_8_pattern * breakdown.edge_8_pattern_weight;

    breakdown.total = breakdown.disc_difference_score + breakdown.mobility_score +
                      breakdown.corner_occupancy_score + breakdown.potential_mobility_score +
                      breakdown.corner_access_score + breakdown.x_square_danger_score +
                      breakdown.frontier_score + breakdown.corner_local_2x3_score +
                      breakdown.corner_2x3_pattern_score +
                      breakdown.edge_stability_lite_score + breakdown.edge_8_pattern_score;
    return breakdown;
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side) noexcept {
    return evaluate_basic_breakdown(board, side, default_evaluation_config());
}

int evaluate_with_config(const Board& board, Side side, const EvaluationConfig& config) noexcept {
    return evaluate_basic_breakdown(board, side, config).total;
}

int evaluate_basic(const Board& board, Side side) noexcept {
    return evaluate_with_config(board, side, default_evaluation_config());
}

} // namespace othello
