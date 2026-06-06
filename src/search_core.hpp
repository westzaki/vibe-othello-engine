#pragma once

#include "search_context.hpp"

#include <optional>
#include <othello/board.hpp>
#include <othello/search.hpp>
#include <othello/square.hpp>

namespace othello::search_detail {

[[nodiscard]] SearchResult search_with_context(
    const Board& board, int depth, SearchContext& context, int alpha, int beta,
    std::optional<Square> root_preferred_move, PrincipalVariationHint pv_hint) noexcept;

[[nodiscard]] SearchResult search_with_context(
    const Board& board, int depth, SearchContext& context,
    std::optional<Square> root_preferred_move, PrincipalVariationHint pv_hint) noexcept;

} // namespace othello::search_detail
