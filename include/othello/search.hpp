#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello {

struct SearchResult {
    std::optional<Square> best_move;
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
    std::vector<Square> principal_variation;
};

struct SearchOptions {
    int max_depth = 5;
    bool use_transposition_table = false;
    std::size_t transposition_table_entries = 1 << 18;
};

[[nodiscard]] SearchResult search(const Board& board, const SearchOptions& options) noexcept;
[[nodiscard]] SearchResult search_fixed_depth(const Board& board, int depth) noexcept;
[[nodiscard]] SearchResult search_iterative(const Board& board,
                                            const SearchOptions& options) noexcept;

} // namespace othello
