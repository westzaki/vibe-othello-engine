#pragma once

#include "search_context.hpp"

#include <optional>
#include <othello/hash.hpp>
#include <othello/square.hpp>

namespace othello::search_detail {

[[nodiscard]] NodeResult search_node(const SearchPosition& position, ZobristHash hash, int depth,
                                     int alpha, int beta, SearchContext& context,
                                     std::optional<Square> root_preferred_move,
                                     PrincipalVariationHint pv_hint, bool is_root) noexcept;

} // namespace othello::search_detail
