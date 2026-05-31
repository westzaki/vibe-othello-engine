#pragma once

#include <othello/othello.hpp>
#include <string>

namespace othello::benchmarks {

struct Position {
    std::string name;
    std::string phase;
    std::string tags;
    std::string board_text;
    std::string notes;
    Board board;
};

} // namespace othello::benchmarks
