#include "search_move_ordering.hpp"

#include "bitboard_ops.hpp"
#include "search_core.hpp"

#include <algorithm>
#include <bit>

namespace othello::search_detail {
namespace {

using bitboard_detail::adjacent_squares;
using bitboard_detail::corner_squares;
using bitboard_detail::is_corner;
using bitboard_detail::is_edge;
using bitboard_detail::is_x_square;
using bitboard_detail::is_x_square_next_to_empty_corner;

[[nodiscard]] int history_killer_bonus(const SearchContext& context, int index,
                                       int depth) noexcept {
    return search_detail::history_killer_bonus(context.session.history_killers, index, depth,
                                               context.move_ordering_params.history_killer);
}

[[nodiscard]] int potential_mobility_after_move(const SearchPosition& next) noexcept {
    return std::popcount(adjacent_squares(next.opponent_discs) & ~next.occupied());
}

[[nodiscard]] int static_risk_for_move(const SearchPosition& position, int index) noexcept {
    return is_x_square_next_to_empty_corner(index, position.occupied()) ? 1 : 0;
}

} // namespace

int move_order_score(const SearchPosition& position, int index, Bitboard move_bit, Bitboard flips,
                     std::size_t move_count, int depth, bool dynamic_move_ordering,
                     SearchContext& context) noexcept {
    const MoveOrderingParams& params = context.move_ordering_params;
    if (!should_use_dynamic_move_ordering(dynamic_move_ordering, move_count, depth, params)) {
        if (is_corner(index)) {
            return params.static_corner_score;
        }
        if (is_edge(index)) {
            return params.static_edge_score;
        }
        if (is_x_square(index)) {
            return params.static_x_square_score;
        }
        return params.static_normal_score;
    }

    int score = 0;
    if (is_corner(index)) {
        score += params.dynamic_corner_bonus;
    }
    if (is_edge(index)) {
        score += params.dynamic_edge_bonus;
    }
    const SearchPosition next = position_after_move_bit(position, move_bit, flips);
    const Bitboard opponent_moves = legal_moves(next);
    if ((opponent_moves & corner_squares) != 0) {
        score -= params.dynamic_opponent_corner_penalty;
    }
    score -= std::popcount(opponent_moves) * params.dynamic_opponent_mobility_penalty;
    score -= potential_mobility_after_move(next) * params.dynamic_potential_mobility_penalty;
    score -= static_risk_for_move(position, index) * params.dynamic_static_risk_penalty;
    score += history_killer_bonus(context, index, depth);

    return score;
}

OrderedMoveIndexes ordered_legal_move_indexes(const SearchPosition& position, Bitboard moves,
                                              int depth, bool dynamic_move_ordering,
                                              SearchContext& context) noexcept {
    OrderedMoveIndexes candidates;
    const auto move_count = static_cast<std::size_t>(std::popcount(moves));

    while (moves != 0) {
        const int index = std::countr_zero(moves);
        const Bitboard move_bit = Bitboard{1} << index;
        moves &= moves - 1;

        const Bitboard flips = flips_for_known_empty_move(position, move_bit);
        if (flips == 0) {
            continue;
        }

        candidates.moves[candidates.size] = OrderedMoveIndexes::Move{
            .index = index,
            .flips = flips,
            .order_score = move_order_score(position, index, move_bit, flips, move_count, depth,
                                            dynamic_move_ordering, context),
        };
        ++candidates.size;
    }

    std::ranges::sort(candidates.moves.begin(), candidates.moves.begin() + candidates.size,
                      [](const OrderedMoveIndexes::Move& lhs, const OrderedMoveIndexes::Move& rhs) {
                          if (lhs.order_score != rhs.order_score) {
                              return lhs.order_score > rhs.order_score;
                          }
                          return lhs.index < rhs.index;
                      });

    return candidates;
}

bool promote_preferred_move(OrderedMoveIndexes& candidates, Square preferred_move) noexcept {
    const int preferred_index = preferred_move.index();
    for (std::size_t index = 0; index < candidates.size; ++index) {
        if (candidates.moves[index].index != preferred_index) {
            continue;
        }

        const OrderedMoveIndexes::Move preferred = candidates.moves[index];
        for (std::size_t shift = index; shift > 0; --shift) {
            candidates.moves[shift] = candidates.moves[shift - 1];
        }
        candidates.moves[0] = preferred;
        return true;
    }

    return false;
}

OrderedMoveIndexes ordered_legal_move_indexes(const SearchPosition& position, Bitboard moves,
                                              int depth, std::optional<Square> preferred_move,
                                              std::optional<Square> tt_preferred_move,
                                              bool tt_preferred_move_from_shallow,
                                              bool dynamic_move_ordering,
                                              SearchContext& context) noexcept {
    OrderedMoveIndexes candidates =
        ordered_legal_move_indexes(position, moves, depth, dynamic_move_ordering, context);

    if (tt_preferred_move.has_value() && (moves & tt_preferred_move->bit()) != 0 &&
        promote_preferred_move(candidates, *tt_preferred_move)) {
        ++context.stats.tt_move_ordering_used;
        if (tt_preferred_move_from_shallow) {
            ++context.stats.shallow_tt_move_ordering_used;
        }
    }

    if (preferred_move.has_value() && (moves & preferred_move->bit()) != 0) {
        static_cast<void>(promote_preferred_move(candidates, *preferred_move));
    }

    return candidates;
}

void record_root_move_ordering_snapshot(SearchContext& context,
                                        const OrderedMoveIndexes& ordered_moves) noexcept {
    if (context.diagnostics.root_move_ordering_snapshot == nullptr) {
        return;
    }

    context.diagnostics.root_move_ordering_snapshot->clear();
    context.diagnostics.root_move_ordering_snapshot->reserve(ordered_moves.size);
    for (std::size_t index = 0; index < ordered_moves.size; ++index) {
        const std::optional<Square> move = Square::from_index(ordered_moves.moves[index].index);
        if (!move.has_value()) {
            continue;
        }
        context.diagnostics.root_move_ordering_snapshot->push_back(RootMoveOrderingEntry{
            .move = *move,
            .order_score = ordered_moves.moves[index].order_score,
        });
    }
}

std::optional<Square> preferred_move_from_hint(PrincipalVariationHint hint) noexcept {
    if (hint.principal_variation == nullptr || !hint.matches_prefix ||
        hint.index >= hint.principal_variation->size) {
        return std::nullopt;
    }

    return Square::from_index(hint.principal_variation->indexes[hint.index]);
}

PrincipalVariationHint child_hint_after_move(PrincipalVariationHint hint, Square move) noexcept {
    return child_hint_after_move_index(hint, move.index());
}

PrincipalVariationHint child_hint_after_move_index(PrincipalVariationHint hint,
                                                   int move_index) noexcept {
    const std::optional<Square> preferred_move = preferred_move_from_hint(hint);
    if (preferred_move.has_value() && preferred_move->index() == move_index) {
        return PrincipalVariationHint{
            .principal_variation = hint.principal_variation,
            .index = hint.index + 1,
            .matches_prefix = true,
        };
    }

    return PrincipalVariationHint{};
}

PrincipalVariationHint child_hint_after_pass(PrincipalVariationHint hint) noexcept {
    return hint;
}

void record_history_killer_cutoff(SearchContext& context, Square move, int depth) noexcept {
    record_history_killer_cutoff_index(context, move.index(), depth);
}

void record_history_killer_cutoff_index(SearchContext& context, int move_index,
                                        int depth) noexcept {
    search_detail::record_history_killer_cutoff(context.session.history_killers, move_index,
                                                depth, context.move_ordering_params.history_killer);
}

} // namespace othello::search_detail
