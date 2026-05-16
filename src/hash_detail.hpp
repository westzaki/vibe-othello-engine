#pragma once

#include <othello/hash.hpp>

namespace othello::detail {

[[nodiscard]] ZobristHash zobrist_piece_hash(Side side, int square_index) noexcept;
[[nodiscard]] ZobristHash zobrist_side_hash(Side side) noexcept;

} // namespace othello::detail
