#pragma once

#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>

namespace othello {

[[nodiscard]] Bitboard legal_moves(const Board& board) noexcept;
[[nodiscard]] bool has_legal_move(const Board& board) noexcept;
[[nodiscard]] Bitboard flips_for_move(const Board& board, Square square) noexcept;
[[nodiscard]] std::optional<Board> apply_move(const Board& board, Square square) noexcept;
[[nodiscard]] std::optional<Board> pass_turn(const Board& board) noexcept;
[[nodiscard]] bool is_game_over(const Board& board) noexcept;
[[nodiscard]] int disc_count(const Board& board, Side side) noexcept;
[[nodiscard]] int score(const Board& board, Side side) noexcept;

} // namespace othello
