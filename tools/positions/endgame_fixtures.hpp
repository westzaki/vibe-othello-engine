#pragma once

#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <string_view>
#include <vector>

namespace othello::benchmarks {

struct EndgamePosition {
    std::string_view name;
    int empties = 0;
    std::string_view tags;
    std::string_view board_text;
    std::string_view notes;
    Board board;
    bool smoke = false;
};

[[nodiscard]] bool add_endgame_position(std::vector<EndgamePosition>& positions,
                                        std::string_view name, int empties,
                                        std::string_view tags, std::string_view board_text,
                                        std::string_view notes, bool smoke = false);
[[nodiscard]] std::optional<std::vector<EndgamePosition>> make_endgame_positions();

} // namespace othello::benchmarks
