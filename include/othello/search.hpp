#pragma once

#include <cstdint>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>

namespace othello {

struct SearchResult {
    std::optional<Square> best_move;
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
};

[[nodiscard]] SearchResult search_fixed_depth(const Board& board, int depth) noexcept;

} // namespace othello
