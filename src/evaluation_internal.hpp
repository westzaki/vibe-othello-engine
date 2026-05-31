#pragma once

#include <othello/board.hpp>
#include <othello/types.hpp>

namespace othello::evaluation_detail {

[[nodiscard]] constexpr Bitboard square_bit(int index) noexcept {
    return Bitboard{1} << index;
}

[[nodiscard]] int corner_occupancy_score(const Board& board, Side side) noexcept;
[[nodiscard]] int potential_mobility_score(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_access_score(const Board& board, Side side) noexcept;
[[nodiscard]] int x_square_danger_score(const Board& board, Side side) noexcept;
[[nodiscard]] int frontier_score(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_local_2x3_score(const Board& board, Side side) noexcept;
[[nodiscard]] int edge_stability_lite_score(const Board& board, Side side) noexcept;

} // namespace othello::evaluation_detail
