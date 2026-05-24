#pragma once

#include <cstdint>
#include <othello/othello.hpp>

namespace othello::benchmarks {

[[nodiscard]] bool same_board(const Board& left, const Board& right) noexcept;
[[nodiscard]] int empty_count(const Board& board) noexcept;
[[nodiscard]] int legal_move_count(const Board& board) noexcept;

} // namespace othello::benchmarks
