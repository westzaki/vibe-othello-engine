#pragma once

#include "protocols/nboard/parser.hpp"

#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::nboard {

struct MoveListParseResult {
    bool ok = false;
    std::string error;
    Board board = Board::initial();
    std::vector<std::string> moves;
};

[[nodiscard]] MoveListParseResult parse_move_list(std::string_view moves_text);
[[nodiscard]] MoveListParseResult parse_ggf_game(std::string_view ggf_text);
[[nodiscard]] std::string format_ggf_game(const std::vector<std::string>& moves);
[[nodiscard]] std::string format_set_game_command(const std::vector<std::string>& moves);
[[nodiscard]] bool is_legal_response(const Board& board, const NBoardMove& move) noexcept;

} // namespace othello::tools::nboard
