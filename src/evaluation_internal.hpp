#pragma once

#include <othello/board.hpp>
#include <othello/evaluation.hpp>
#include <othello/evaluation_patterns.hpp>
#include <othello/types.hpp>

namespace othello::evaluation_detail {

struct EvaluationScratch {
    Bitboard player = 0;
    Bitboard opponent = 0;
    Bitboard occupied = 0;
    Bitboard empty = 0;
    Bitboard own_moves = 0;
    Bitboard opp_moves = 0;
    Bitboard adjacent_empty = 0;
    Bitboard own_adjacent_empty = 0;
    Bitboard opp_adjacent_empty = 0;
    int own_mobility = 0;
    int opp_mobility = 0;
    int occupied_count = 0;
    int empty_count = 64;
    bool game_over = false;
};

[[nodiscard]] constexpr Bitboard square_bit(int index) noexcept {
    return Bitboard{1} << index;
}

[[nodiscard]] EvaluationScratch make_evaluation_scratch(const Board& board,
                                                        Side side) noexcept;
[[nodiscard]] EvaluationScratch make_evaluation_scratch(Bitboard player,
                                                        Bitboard opponent) noexcept;
[[nodiscard]] int evaluate_with_config(Bitboard player, Bitboard opponent,
                                       const EvaluationConfig& config) noexcept;
[[nodiscard]] int disc_difference_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int mobility_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int corner_occupancy_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int potential_mobility_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int corner_access_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int x_square_danger_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int frontier_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int corner_local_2x3_score(const EvaluationScratch& scratch) noexcept;
[[nodiscard]] int edge_stability_lite_score(const EvaluationScratch& scratch) noexcept;

[[nodiscard]] int corner_occupancy_score(const Board& board, Side side) noexcept;
[[nodiscard]] int potential_mobility_score(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_access_score(const Board& board, Side side) noexcept;
[[nodiscard]] int x_square_danger_score(const Board& board, Side side) noexcept;
[[nodiscard]] int frontier_score(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_local_2x3_score(const Board& board, Side side) noexcept;
[[nodiscard]] int edge_stability_lite_score(const Board& board, Side side) noexcept;

[[nodiscard]] int corner_2x3_pattern_score(Bitboard player, Bitboard opponent) noexcept;
[[nodiscard]] int edge_8_pattern_score(Bitboard player, Bitboard opponent) noexcept;
[[nodiscard]] int evaluation_pattern_table_score(Bitboard player, Bitboard opponent,
                                                 const PatternTableBundle& tables) noexcept;

} // namespace othello::evaluation_detail
