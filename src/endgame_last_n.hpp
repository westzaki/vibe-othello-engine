#pragma once

#include "endgame_tt.hpp"
#include "search_common.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <optional>
#include <othello/rules.hpp>

namespace othello::endgame_detail {

using search_detail::board_after_move;
using search_detail::board_after_pass;
using search_detail::empty_count;
using search_detail::is_better_best_move;
using search_detail::NodeResult;
using search_detail::principal_variation_with_move;
using search_detail::PrincipalVariation;

// The final 0-3 empties avoid TT lookup/store and full move ordering overhead.
// Four-empty specialization was benchmarked separately and rejected because it helped 20-empty
// tail latency a bit more but made 14-empty tail behavior noisier.
constexpr int last_n_specialized_empties = 3;

[[nodiscard]] inline NodeResult solve_last_0(const Board& board,
                                             ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    return NodeResult{.score = score(board, board.side_to_move)};
}

[[nodiscard]] inline NodeResult solve_last_n_node(const Board& board, int alpha, int beta,
                                                  ExactEndgameContext& context) noexcept;

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] inline NodeResult solve_last_n_dispatch(const Board& board, int alpha, int beta,
                                                      ExactEndgameContext& context,
                                                      int empties) noexcept {
    switch (empties) {
    case 0:
        return solve_last_0(board, context);
    case 1:
    case 2:
    case 3:
        return solve_last_n_node(board, alpha, beta, context);
    default:
        assert(false);
        return NodeResult{.score = score(board, board.side_to_move)};
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] inline NodeResult solve_last_n_node(const Board& board, int alpha, int beta,
                                                  ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    const int empties = empty_count(board);
    if (empties == 0) {
        return NodeResult{.score = score(board, board.side_to_move)};
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const Board next = board_after_pass(board);
        if (legal_moves(next) == 0) {
            return NodeResult{.score = score(board, board.side_to_move)};
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

        const Bitboard flips = flips_for_move(board, *square);
        if (flips == 0) {
            continue;
        }

        const Board next = board_after_move(board, *square, flips);
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
        .score = best_score.value_or(score(board, board.side_to_move)),
        .principal_variation = best_principal_variation,
    };
}

} // namespace othello::endgame_detail
