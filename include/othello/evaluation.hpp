#pragma once

#include <othello/board.hpp>
#include <othello/types.hpp>

namespace othello {

enum class EvaluationPhase {
    Opening,
    Midgame,
    Late,
};

// Component view of the current basic evaluator. This is intended for developer
// tooling and tests; fields may evolve as the evaluator itself evolves. The
// total field always matches evaluate_basic(board, side). For terminal boards,
// the current evaluator uses only the terminal fields and leaves non-terminal
// component scores at zero.
struct EvaluationBreakdown {
    EvaluationPhase phase = EvaluationPhase::Opening;
    int occupied_count = 0;
    int empty_count = 64;

    int disc_difference = 0;
    int disc_difference_weight = 0;
    int disc_difference_score = 0;

    int mobility = 0;
    int mobility_weight = 0;
    int mobility_score = 0;

    int corner_occupancy = 0;
    int corner_occupancy_weight = 0;
    int corner_occupancy_score = 0;

    int potential_mobility = 0;
    int potential_mobility_weight = 0;
    int potential_mobility_score = 0;

    int corner_access = 0;
    int corner_access_weight = 0;
    int corner_access_score = 0;

    int x_square_danger = 0;
    int x_square_danger_weight = 0;
    int x_square_danger_score = 0;

    int frontier = 0;
    int frontier_weight = 0;
    int frontier_score = 0;

    bool terminal = false;
    int terminal_disc_difference = 0;
    int terminal_score_weight = 1000;
    int terminal_score = 0;

    int total = 0;
};

[[nodiscard]] int evaluate_disc_difference(const Board& board, Side side) noexcept;
[[nodiscard]] int evaluate_mobility(const Board& board, Side side) noexcept;
[[nodiscard]] EvaluationBreakdown evaluate_basic_breakdown(const Board& board,
                                                           Side side) noexcept;
[[nodiscard]] int evaluate_basic(const Board& board, Side side) noexcept;

} // namespace othello
