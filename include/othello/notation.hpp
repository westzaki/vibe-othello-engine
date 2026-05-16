#pragma once

#include <optional>
#include <othello/board.hpp>
#include <string>
#include <string_view>

namespace othello {

[[nodiscard]] std::optional<Board> board_from_string(std::string_view text) noexcept;
[[nodiscard]] std::string to_string(const Board& board);

} // namespace othello
