#pragma once

#include <othello/board.hpp>
#include <othello/search.hpp>

namespace othello {

[[nodiscard]] bool should_solve_exact_endgame_at_root(const Board& board,
                                                      const SearchOptions& options) noexcept;

} // namespace othello
