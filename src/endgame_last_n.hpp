#pragma once

#include "endgame_tt.hpp"
#include "search_common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <optional>
#include <othello/rules.hpp>

namespace othello::endgame_detail {

using search_detail::empty_count;
using search_detail::flips_for_move;
using search_detail::flips_for_known_empty_move;
using search_detail::is_better_best_move;
using search_detail::legal_moves;
using search_detail::NodeResult;
using search_detail::position_after_move;
using search_detail::position_after_pass;
using search_detail::principal_variation_with_move;
using search_detail::PrincipalVariation;
using search_detail::score_for_player;
using search_detail::SearchPosition;

// The final 0-4 empties avoid TT lookup/store and full move ordering overhead.
// The tail solver remains exact alpha-beta; it only skips machinery whose cost dominates at
// these depths.
constexpr int last_n_specialized_empties = 4;

[[nodiscard]] inline NodeResult solve_last_0(const SearchPosition& position,
                                             ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    return NodeResult{.score = score_for_player(position)};
}

[[nodiscard]] inline NodeResult solve_last_1(const SearchPosition& position,
                                             ExactEndgameContext& context) noexcept;

[[nodiscard]] inline NodeResult solve_last_2(const SearchPosition& position, int alpha, int beta,
                                             ExactEndgameContext& context) noexcept;

[[nodiscard]] inline NodeResult solve_last_3(const SearchPosition& position, int alpha, int beta,
                                             ExactEndgameContext& context) noexcept;

[[nodiscard]] inline NodeResult solve_last_4(const SearchPosition& position, int alpha, int beta,
                                             ExactEndgameContext& context) noexcept;

[[nodiscard]] inline NodeResult solve_last_n_generic_node(const SearchPosition& position, int alpha,
                                                          int beta,
                                                          ExactEndgameContext& context) noexcept;

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] inline NodeResult solve_last_n_dispatch(const SearchPosition& position, int alpha,
                                                      int beta, ExactEndgameContext& context,
                                                      int empties) noexcept {
    switch (empties) {
    case 0:
        return solve_last_0(position, context);
    case 1:
        return solve_last_1(position, context);
    case 2:
        return solve_last_2(position, alpha, beta, context);
    case 3:
        return solve_last_3(position, alpha, beta, context);
    case 4:
        return solve_last_4(position, alpha, beta, context);
    default:
        assert(false);
        return NodeResult{.score = score_for_player(position)};
    }
}

[[nodiscard]] inline PrincipalVariation principal_variation_single_move(Square move) noexcept {
    PrincipalVariation principal_variation;
    principal_variation.indexes[0] = move.index();
    principal_variation.size = 1;
    return principal_variation;
}

[[nodiscard]] inline std::optional<Square> first_empty_square(Bitboard empty) noexcept {
    if (empty == 0) {
        return std::nullopt;
    }
    return Square::from_index(std::countr_zero(empty));
}

[[nodiscard]] inline Bitboard flips_for_empty_square(const SearchPosition& position,
                                                     Square square) noexcept {
    return flips_for_known_empty_move(position, square.bit());
}

[[nodiscard]] inline NodeResult solve_last_1(const SearchPosition& position,
                                             ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;

    const std::optional<Square> square = first_empty_square(position.empty());
    if (!square.has_value()) {
        return NodeResult{.score = score_for_player(position)};
    }

    const Bitboard current_flips = flips_for_empty_square(position, *square);
    if (current_flips != 0) {
        const SearchPosition next = position_after_move(position, *square, current_flips);
        ++context.stats.nodes;
        return NodeResult{
            .best_move = square,
            .score = -score_for_player(next),
            .principal_variation = principal_variation_single_move(*square),
        };
    }

    const SearchPosition after_pass = position_after_pass(position);
    const Bitboard opponent_flips = flips_for_empty_square(after_pass, *square);
    if (opponent_flips == 0) {
        return NodeResult{.score = score_for_player(position)};
    }

    const SearchPosition next = position_after_move(after_pass, *square, opponent_flips);
    context.stats.nodes += 2;
    return NodeResult{
        .score = score_for_player(next),
        .principal_variation = principal_variation_single_move(*square),
    };
}

[[nodiscard]] inline std::array<Square, 2> last_2_empty_squares(Bitboard empty) noexcept {
    assert(std::popcount(empty) == 2);
    const int first_index = std::countr_zero(empty);
    empty &= empty - 1;
    const int second_index = std::countr_zero(empty);
    return {*Square::from_index(first_index), *Square::from_index(second_index)};
}

template <std::size_t N>
[[nodiscard]] inline std::array<Square, N> last_empty_squares(Bitboard empty) noexcept {
    static_assert(N == 3 || N == 4);
    assert(std::popcount(empty) == N);

    const int first_index = std::countr_zero(empty);
    empty &= empty - 1;
    const int second_index = std::countr_zero(empty);
    empty &= empty - 1;
    const int third_index = std::countr_zero(empty);

    if constexpr (N == 3) {
        return {*Square::from_index(first_index), *Square::from_index(second_index),
                *Square::from_index(third_index)};
    } else {
        empty &= empty - 1;
        const int fourth_index = std::countr_zero(empty);
        return {*Square::from_index(first_index), *Square::from_index(second_index),
                *Square::from_index(third_index), *Square::from_index(fourth_index)};
    }
}

[[nodiscard]] inline bool
has_move_on_any_last_2_empty(const SearchPosition& position,
                             const std::array<Square, 2>& squares) noexcept {
    return flips_for_empty_square(position, squares[0]) != 0 ||
           flips_for_empty_square(position, squares[1]) != 0;
}

template <std::size_t N>
struct LastEmptyFlips {
    std::array<Bitboard, N> by_square{};
    bool has_move = false;
};

template <std::size_t N>
[[nodiscard]] inline LastEmptyFlips<N>
flips_for_last_empty_squares(const SearchPosition& position,
                             const std::array<Square, N>& squares) noexcept {
    LastEmptyFlips<N> result;
    for (std::size_t index = 0; index < N; ++index) {
        result.by_square[index] = flips_for_empty_square(position, squares[index]);
        result.has_move = result.has_move || result.by_square[index] != 0;
    }
    return result;
}

template <std::size_t N>
[[nodiscard]] inline std::array<Square, N - 1>
last_empty_squares_without(const std::array<Square, N>& squares,
                           std::size_t removed_index) noexcept {
    static_assert(N > 1);
    std::array<Square, N - 1> result = [&]() noexcept {
        if constexpr (N == 2) {
            return std::array<Square, 1>{squares[removed_index == 0 ? 1U : 0U]};
        } else if constexpr (N == 3) {
            return removed_index == 0 ? std::array<Square, 2>{squares[1], squares[2]}
                   : removed_index == 1 ? std::array<Square, 2>{squares[0], squares[2]}
                                        : std::array<Square, 2>{squares[0], squares[1]};
        } else {
            return removed_index == 0 ? std::array<Square, 3>{squares[1], squares[2], squares[3]}
                   : removed_index == 1 ? std::array<Square, 3>{squares[0], squares[2], squares[3]}
                   : removed_index == 2 ? std::array<Square, 3>{squares[0], squares[1], squares[3]}
                                        : std::array<Square, 3>{squares[0], squares[1], squares[2]};
        }
    }();
    return result;
}

[[nodiscard]] inline NodeResult solve_last_2(const SearchPosition& position, int alpha, int beta,
                                             ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    assert(empty_count(position) == 2);

    const std::array<Square, 2> squares = last_2_empty_squares(position.empty());
    if (flips_for_empty_square(position, squares[0]) == 0 &&
        flips_for_empty_square(position, squares[1]) == 0) {
        const SearchPosition after_pass = position_after_pass(position);
        if (!has_move_on_any_last_2_empty(after_pass, squares)) {
            return NodeResult{.score = score_for_player(position)};
        }

        const NodeResult child = solve_last_2(after_pass, -beta, -alpha, context);
        return NodeResult{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    for (Square square : squares) {
        const Bitboard flips = flips_for_empty_square(position, square);
        if (flips == 0) {
            continue;
        }

        const SearchPosition next = position_after_move(position, square, flips);
        const NodeResult child = solve_last_1(next, context);
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(square, child.principal_variation);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            break;
        }
    }

    return NodeResult{
        .best_move = best_move,
        .score = best_score.value_or(score_for_player(position)),
        .principal_variation = best_principal_variation,
    };
}

template <std::size_t N>
[[nodiscard]] inline bool
has_move_on_any_last_empty(const SearchPosition& position,
                           const std::array<Square, N>& squares) noexcept {
    for (Square square : squares) {
        if (flips_for_empty_square(position, square) != 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline NodeResult
solve_last_2_with_squares(const SearchPosition& position, const std::array<Square, 2>& squares,
                          int alpha, int beta, ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    assert(empty_count(position) == 2);

    const LastEmptyFlips<2> current_flips = flips_for_last_empty_squares(position, squares);
    if (!current_flips.has_move) {
        const SearchPosition after_pass = position_after_pass(position);
        if (!has_move_on_any_last_empty(after_pass, squares)) {
            return NodeResult{.score = score_for_player(position)};
        }

        const NodeResult child =
            solve_last_2_with_squares(after_pass, squares, -beta, -alpha, context);
        return NodeResult{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    for (std::size_t index = 0; index < squares.size(); ++index) {
        const Bitboard flips = current_flips.by_square[index];
        if (flips == 0) {
            continue;
        }

        const Square square = squares[index];
        const SearchPosition next = position_after_move(position, square, flips);
        const NodeResult child = solve_last_1(next, context);
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(square, child.principal_variation);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            break;
        }
    }

    return NodeResult{
        .best_move = best_move,
        .score = best_score.value_or(score_for_player(position)),
        .principal_variation = best_principal_variation,
    };
}

template <std::size_t N>
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] inline NodeResult
solve_last_fixed_with_squares(const SearchPosition& position,
                              const std::array<Square, N>& squares, int alpha, int beta,
                              ExactEndgameContext& context) noexcept {
    static_assert(N == 3 || N == 4);
    ++context.stats.nodes;
    assert(empty_count(position) == static_cast<int>(N));

    const LastEmptyFlips<N> current_flips = flips_for_last_empty_squares(position, squares);
    if (!current_flips.has_move) {
        const SearchPosition after_pass = position_after_pass(position);
        if (!has_move_on_any_last_empty(after_pass, squares)) {
            return NodeResult{.score = score_for_player(position)};
        }

        const NodeResult child =
            solve_last_fixed_with_squares<N>(after_pass, squares, -beta, -alpha, context);
        return NodeResult{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    for (std::size_t index = 0; index < squares.size(); ++index) {
        const Bitboard flips = current_flips.by_square[index];
        if (flips == 0) {
            continue;
        }

        const Square square = squares[index];
        const SearchPosition next = position_after_move(position, square, flips);
        const std::array<Square, N - 1> child_squares = last_empty_squares_without(squares, index);
        const NodeResult child = [&]() noexcept {
            if constexpr (N == 3) {
                return solve_last_2_with_squares(next, child_squares, -beta, -alpha, context);
            } else {
                return solve_last_fixed_with_squares<3>(next, child_squares, -beta, -alpha,
                                                        context);
            }
        }();
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(square, child.principal_variation);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            break;
        }
    }

    return NodeResult{
        .best_move = best_move,
        .score = best_score.value_or(score_for_player(position)),
        .principal_variation = best_principal_variation,
    };
}

template <std::size_t N>
[[nodiscard]] inline NodeResult solve_last_fixed(const SearchPosition& position, int alpha,
                                                 int beta,
                                                 ExactEndgameContext& context) noexcept {
    return solve_last_fixed_with_squares<N>(position, last_empty_squares<N>(position.empty()),
                                            alpha, beta, context);
}

[[nodiscard]] inline NodeResult solve_last_3(const SearchPosition& position, int alpha, int beta,
                                             ExactEndgameContext& context) noexcept {
    // Search meaning is unchanged: this only removes generic move-generation, optional, and
    // Square::from_index machinery from the exact 0-4 empty tail.
    return solve_last_fixed<3>(position, alpha, beta, context);
}

[[nodiscard]] inline NodeResult solve_last_4(const SearchPosition& position, int alpha, int beta,
                                             ExactEndgameContext& context) noexcept {
    // Search meaning is unchanged: this only removes generic move-generation, optional, and
    // Square::from_index machinery from the exact 0-4 empty tail.
    return solve_last_fixed<4>(position, alpha, beta, context);
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] inline NodeResult solve_last_n_generic_node(const SearchPosition& position, int alpha,
                                                          int beta,
                                                          ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    const int empties = empty_count(position);
    if (empties == 0) {
        return NodeResult{.score = score_for_player(position)};
    }

    const Bitboard moves = legal_moves(position);
    if (moves == 0) {
        const SearchPosition next = position_after_pass(position);
        if (legal_moves(next) == 0) {
            return NodeResult{.score = score_for_player(position)};
        }

        const NodeResult child = solve_last_n_dispatch(next, -beta, -alpha, context, empties);
        return NodeResult{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    Bitboard remaining_moves = moves;
    while (remaining_moves != 0) {
        const int index = std::countr_zero(remaining_moves);
        remaining_moves &= remaining_moves - 1;

        const std::optional<Square> square = Square::from_index(index);
        if (!square.has_value()) {
            continue;
        }

        const Bitboard flips = flips_for_move(position, *square);
        if (flips == 0) {
            continue;
        }

        const SearchPosition next = position_after_move(position, *square, flips);
        const NodeResult child = solve_last_n_dispatch(next, -beta, -alpha, context, empties - 1);
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, *square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(*square, child.principal_variation);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            break;
        }
    }

    return NodeResult{
        .best_move = best_move,
        .score = best_score.value_or(score_for_player(position)),
        .principal_variation = best_principal_variation,
    };
}

} // namespace othello::endgame_detail
