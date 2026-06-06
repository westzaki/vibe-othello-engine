#pragma once

#include <othello/endgame.hpp>
#include <othello/search.hpp>

namespace othello::search_detail {

// Keep exact root endgame scores comparable with evaluate_basic terminal scores.
// If the terminal score weight in evaluation.cpp changes, update this scale too.
inline constexpr int exact_endgame_score_scale = 1'000;

[[nodiscard]] SearchResult exact_endgame_search_result(const Board& board,
                                                       const ExactEndgameOptions& options) noexcept;

} // namespace othello::search_detail
