#include <bit>
#include <othello/evaluation.hpp>
#include <othello/rules.hpp>

namespace othello {
namespace {

constexpr Bitboard corner_squares =
    (Bitboard{1} << 0) | (Bitboard{1} << 7) | (Bitboard{1} << 56) | (Bitboard{1} << 63);

// Deliberately simple, untuned weights for early leaf evaluation.
constexpr int disc_difference_weight = 1;
constexpr int mobility_weight = 5;
constexpr int corner_occupancy_weight = 25;
constexpr int terminal_score_weight = 1000;

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

} // namespace

int evaluate_disc_difference(const Board& board, Side side) noexcept {
    return score(board, side);
}

int evaluate_mobility(const Board& board, Side side) noexcept {
    return legal_move_count(board, side) - legal_move_count(board, opponent(side));
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side) noexcept {
    EvaluationBreakdown breakdown{
        .disc_difference_weight = disc_difference_weight,
        .mobility_weight = mobility_weight,
        .corner_occupancy_weight = corner_occupancy_weight,
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

    breakdown.total = breakdown.disc_difference_score + breakdown.mobility_score +
                      breakdown.corner_occupancy_score;
    return breakdown;
}

int evaluate_basic(const Board& board, Side side) noexcept {
    return evaluate_basic_breakdown(board, side).total;
}

} // namespace othello
