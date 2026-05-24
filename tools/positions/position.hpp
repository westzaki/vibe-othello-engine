#pragma once

#include <othello/othello.hpp>
#include <string_view>

namespace othello::benchmarks {

struct Position {
    std::string_view name;
    std::string_view phase;
    std::string_view tags;
    std::string_view board_text;
    std::string_view notes;
    Board board;
};

} // namespace othello::benchmarks
