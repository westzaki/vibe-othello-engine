#pragma once

#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>

namespace othello {

[[nodiscard]] std::optional<Square> first_legal_move(const Board& board) noexcept;

} // namespace othello
