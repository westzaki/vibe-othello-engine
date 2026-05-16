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

int evaluate_basic(const Board& board, Side side) noexcept {
    if (is_game_over(board)) {
        return score(board, side) * terminal_score_weight;
    }

    const int disc_difference = evaluate_disc_difference(board, side);
    const int mobility = evaluate_mobility(board, side);
    const int corner_occupancy = corner_count(board, side) - corner_count(board, opponent(side));

    return (disc_difference * disc_difference_weight) + (mobility * mobility_weight) +
           (corner_occupancy * corner_occupancy_weight);
}

} // namespace othello
