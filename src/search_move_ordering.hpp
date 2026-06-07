#pragma once

#include "search_context.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>

namespace othello::search_detail {

struct OrderedMoveIndexes {
    struct Move {
        int index = -1;
        Bitboard flips = 0;
        int order_score = 0;
    };

    std::array<Move, 64> moves{};
    std::size_t size = 0;
};

[[nodiscard]] constexpr bool
should_use_dynamic_move_ordering(bool enabled, std::size_t move_count, int depth,
                                 const MoveOrderingParams& params) noexcept {
    return enabled && depth >= params.dynamic_min_depth && move_count >= params.dynamic_min_moves;
}

[[nodiscard]] int move_order_score(const SearchPosition& position, int index, Bitboard move_bit,
                                   Bitboard flips, std::size_t move_count, int depth,
                                   bool dynamic_move_ordering, SearchContext& context) noexcept;

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(const SearchPosition& position,
                                                            Bitboard moves, int depth,
                                                            bool dynamic_move_ordering,
                                                            SearchContext& context) noexcept;

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(const SearchPosition& position,
                                                            Bitboard moves, int depth,
                                                            std::optional<Square> preferred_move,
                                                            std::optional<Square> tt_preferred_move,
                                                            bool tt_preferred_move_from_shallow,
                                                            bool dynamic_move_ordering,
                                                            SearchContext& context) noexcept;

[[nodiscard]] bool promote_preferred_move(OrderedMoveIndexes& candidates,
                                          Square preferred_move) noexcept;

void record_root_move_ordering_snapshot(SearchContext& context,
                                        const OrderedMoveIndexes& ordered_moves) noexcept;

[[nodiscard]] std::optional<Square> preferred_move_from_hint(PrincipalVariationHint hint) noexcept;

[[nodiscard]] PrincipalVariationHint child_hint_after_move(PrincipalVariationHint hint,
                                                           Square move) noexcept;

[[nodiscard]] PrincipalVariationHint child_hint_after_move_index(PrincipalVariationHint hint,
                                                                 int move_index) noexcept;

[[nodiscard]] PrincipalVariationHint child_hint_after_pass(PrincipalVariationHint hint) noexcept;

void record_history_killer_cutoff(SearchContext& context, Square move, int depth) noexcept;

void record_history_killer_cutoff_index(SearchContext& context, int move_index,
                                        int depth) noexcept;

} // namespace othello::search_detail
