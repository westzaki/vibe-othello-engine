#include "search_root_policy.hpp"

#include "search_common.hpp"
#include "search_runtime_options.hpp"

#include <bit>
#include <othello/rules.hpp>

namespace othello {
namespace {

using search_detail::empty_count;
using search_detail::engine_options_from;

struct ExactRootPolicyParams {
    int always_exact_max_empties = 14;
    int adaptive_max_empties = 16;
    int max_legal_moves = 10;
    int max_opponent_legal_moves = 10;
};

constexpr ExactRootPolicyParams adaptive16_exact_root_policy_params{};

} // namespace

ExactEndgameRootDecision decide_exact_endgame_root(const Board& board,
                                                   const SearchOptions& options) noexcept {
    const search_detail::SearchEngineOptions engine_options = engine_options_from(options);
    Board opponent_board = board;
    opponent_board.side_to_move = opponent(board.side_to_move);

    ExactEndgameRootDecision decision{
        .empty_count = empty_count(board),
        .legal_moves_current = static_cast<int>(std::popcount(legal_moves(board))),
        .legal_moves_opponent = static_cast<int>(std::popcount(legal_moves(opponent_board))),
    };

    if (engine_options.exact_endgame_empty_threshold <= 0) {
        decision.skip_reason = ExactEndgameRootSkipReason::Disabled;
        return decision;
    }

    if (engine_options.exact_endgame_root_policy == ExactEndgameRootPolicy::FixedThreshold) {
        if (decision.empty_count > engine_options.exact_endgame_empty_threshold) {
            decision.skip_reason = ExactEndgameRootSkipReason::AboveThreshold;
            return decision;
        }

        decision.solve_exact = true;
        return decision;
    }

    constexpr ExactRootPolicyParams policy = adaptive16_exact_root_policy_params;

    if (decision.empty_count <= policy.always_exact_max_empties) {
        decision.solve_exact = true;
        return decision;
    }
    if (decision.empty_count > policy.adaptive_max_empties) {
        decision.skip_reason = ExactEndgameRootSkipReason::AboveThreshold;
        return decision;
    }

    const bool root_pass = decision.legal_moves_current == 0 && pass_turn(board).has_value();
    if (root_pass) {
        decision.skip_reason = ExactEndgameRootSkipReason::AdaptiveRootPass;
        return decision;
    }
    if (decision.legal_moves_current > policy.max_legal_moves) {
        decision.skip_reason = ExactEndgameRootSkipReason::AdaptiveTooManyLegalMoves;
        return decision;
    }
    if (decision.legal_moves_opponent > policy.max_opponent_legal_moves) {
        decision.skip_reason = ExactEndgameRootSkipReason::AdaptiveOpponentTooManyLegalMoves;
        return decision;
    }

    decision.solve_exact = true;
    return decision;
}

bool should_solve_exact_endgame_at_root(const Board& board,
                                        const SearchOptions& options) noexcept {
    return decide_exact_endgame_root(board, options).solve_exact;
}

} // namespace othello
