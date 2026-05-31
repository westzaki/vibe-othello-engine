#include "bitboard_ops.hpp"
#include "evaluation_internal.hpp"

#include <bit>
#include <othello/evaluation.hpp>
#include <othello/rules.hpp>

#include <array>

namespace othello {
namespace {

using bitboard_detail::adjacent_squares;
using bitboard_detail::corner_squares;
using bitboard_detail::is_x_square_next_to_empty_corner;
using evaluation_detail::square_bit;

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

[[nodiscard]] constexpr Board with_side_to_move(const Board& board, Side side) noexcept {
    return Board{
        .black = board.black,
        .white = board.white,
        .side_to_move = side,
    };
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
        const Bitboard x_square = square_bit(x_square_index);
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

} // namespace

int evaluate_disc_difference(const Board& board, Side side) noexcept {
    return score(board, side);
}

int evaluate_mobility(const Board& board, Side side) noexcept {
    return legal_move_count(board, side) - legal_move_count(board, opponent(side));
}

namespace evaluation_detail {

int corner_occupancy_score(const Board& board, Side side) noexcept {
    return corner_count(board, side) - corner_count(board, opponent(side));
}

int potential_mobility_score(const Board& board, Side side) noexcept {
    return potential_mobility_count(board, side) -
           potential_mobility_count(board, opponent(side));
}

int corner_access_score(const Board& board, Side side) noexcept {
    return legal_corner_move_count(board, side) -
           legal_corner_move_count(board, opponent(side));
}

int x_square_danger_score(const Board& board, Side side) noexcept {
    // Owning an X-square next to an empty corner is dangerous, so the score is
    // opponent dangerous X-squares minus our dangerous X-squares.
    return dangerous_x_square_count(board, opponent(side)) - dangerous_x_square_count(board, side);
}

int frontier_score(const Board& board, Side side) noexcept {
    // Fewer own frontier discs is usually better.
    return frontier_count(board, opponent(side)) - frontier_count(board, side);
}

int corner_local_2x3_score(const Board& board, Side side) noexcept {
    return corner_local_2x3_value_for_side(board, side) -
           corner_local_2x3_value_for_side(board, opponent(side));
}

int edge_stability_lite_score(const Board& board, Side side) noexcept {
    return edge_stability_lite_value_for_side(board, side) -
           edge_stability_lite_value_for_side(board, opponent(side));
}

} // namespace evaluation_detail
} // namespace othello
