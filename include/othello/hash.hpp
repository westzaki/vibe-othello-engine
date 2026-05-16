#pragma once

#include <cstdint>
#include <othello/board.hpp>

namespace othello {

using ZobristHash = std::uint64_t;

[[nodiscard]] ZobristHash zobrist_hash(const Board& board) noexcept;

} // namespace othello
