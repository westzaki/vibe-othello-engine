#pragma once

#include <cstdint>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello {

struct ExactEndgameResult {
    std::optional<Square> best_move;
    int disc_margin = 0;
    int empties = 0;
    std::uint64_t nodes = 0;
    std::vector<Square> principal_variation;
};

[[nodiscard]] ExactEndgameResult solve_exact_endgame(const Board& board) noexcept;

} // namespace othello
