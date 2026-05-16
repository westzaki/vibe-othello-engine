#pragma once

#include <othello/board.hpp>
#include <othello/types.hpp>

namespace othello {

[[nodiscard]] int evaluate_disc_difference(const Board& board, Side side) noexcept;
[[nodiscard]] int evaluate_mobility(const Board& board, Side side) noexcept;
[[nodiscard]] int evaluate_basic(const Board& board, Side side) noexcept;

} // namespace othello
