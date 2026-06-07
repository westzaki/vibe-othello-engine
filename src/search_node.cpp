#include "search_node.hpp"

#include "evaluation_internal.hpp"
#include "hash_update.hpp"
#include "search_bounds.hpp"
#include "search_core.hpp"
#include "search_move_ordering.hpp"
#include "search_session.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <othello/evaluation.hpp>
#include <othello/hash.hpp>
#include <othello/search.hpp>

namespace othello::search_detail {
namespace {

using hash_detail::hash_after_move;
using hash_detail::hash_after_pass;

[[nodiscard]] int evaluate_for_search(const SearchPosition& position,
                                      SearchContext& context) noexcept {
    ++context.stats.eval_calls;
    return evaluation_detail::evaluate_with_config(position.player, position.opponent_discs,
                                                   context.evaluation_config);
}

[[nodiscard]] TranspositionLookup probe_transposition(SearchContext& context, ZobristHash hash,
                                                      int depth, int alpha, int beta,
                                                      bool collect_best_move_hint) noexcept {
    if (context.engine_options.use_transposition_table && depth < context.tt_min_probe_depth) {
        ++context.stats.tt_probe_skipped_by_depth;
        return {};
    }
    return context.transpositions.lookup(hash, context.transposition_scope, depth, alpha, beta,
                                         collect_best_move_hint,
                                         context.engine_options
                                             .use_shallow_tt_move_ordering_hint,
                                         context.stats);
}

bool store_transposition(SearchContext& context, ZobristHash hash, int depth, int score,
                         int original_alpha, int beta,
                         const std::optional<Square>& best_move) noexcept {
    if (context.engine_options.use_transposition_table && depth < context.tt_min_store_depth) {
        ++context.stats.tt_store_skipped_by_depth;
        return false;
    }
    return context.transpositions.store(hash, context.transposition_scope,
                                        context.session.generation, depth, score, original_alpha,
                                        beta, best_move, context.stats);
}

void store_leaf_transposition(SearchContext& context, ZobristHash hash, int depth, int score,
                              int original_alpha, int beta,
                              const std::optional<Square>& best_move) noexcept {
    if (!context.store_leaf_tt_entries) {
        if (context.engine_options.use_transposition_table) {
            ++context.stats.tt_leaf_store_skipped;
        }
        return;
    }
    if (store_transposition(context, hash, depth, score, original_alpha, beta, best_move)) {
        ++context.stats.tt_leaf_stores;
    }
}

[[nodiscard]] std::optional<Square>
preferred_move_for_node(std::optional<Square> principal_variation_move,
                        std::optional<Square> root_preferred_move,
                        std::optional<Square> tt_preferred_move, bool is_root) noexcept {
    if (principal_variation_move.has_value()) {
        return principal_variation_move;
    }
    if (is_root && root_preferred_move.has_value()) {
        return root_preferred_move;
    }
    return tt_preferred_move;
}

[[nodiscard]] bool same_move(std::optional<Square> lhs, std::optional<Square> rhs) noexcept {
    return lhs.has_value() && rhs.has_value() && *lhs == *rhs;
}

} // namespace

// Fixed-depth negamax is easiest to audit as direct recursion at this stage.
// NOLINTNEXTLINE(misc-no-recursion)
SearchNodeResult search_node(const SearchPosition& position, ZobristHash hash, int depth,
                             int alpha, int beta, SearchContext& context,
                             std::optional<Square> root_preferred_move,
                             PrincipalVariationHint pv_hint, bool is_root,
                             MidgamePvTable& pv_table, std::size_t ply) noexcept {
    ++context.stats.nodes;

    const int original_alpha = alpha;

    const bool collect_tt_best_move_hint =
        !is_root && ((context.dynamic_move_ordering &&
                      depth >= context.move_ordering_params.dynamic_min_depth) ||
                     context.use_lazy_first_move_ordering);
    const TranspositionLookup cached =
        probe_transposition(context, hash, depth, alpha, beta, collect_tt_best_move_hint);
    if (cached.cutoff.has_value()) {
        if (cached.cutoff->best_move.has_value()) {
            pv_table.set_single_move(ply, *cached.cutoff->best_move);
        } else {
            pv_table.clear(ply);
        }
        return *cached.cutoff;
    }

    if (depth <= 0) {
        pv_table.clear(ply);
        const SearchNodeResult result{.score = evaluate_for_search(position, context)};
        store_leaf_transposition(context, hash, depth, result.score, original_alpha, beta,
                                 result.best_move);
        return result;
    }

    const Bitboard moves = legal_moves(position);
    if (moves == 0) {
        const SearchPosition next = position_after_pass(position);
        if (legal_moves(next) == 0) {
            ++context.stats.game_over_nodes;
            pv_table.clear(ply);
            const SearchNodeResult result{.score = evaluate_for_search(position, context)};
            store_transposition(context, hash, depth, result.score, original_alpha, beta,
                                result.best_move);
            return result;
        }

        ++context.stats.pass_nodes;
        const ZobristHash next_hash = hash_after_pass(hash, position.side_to_move);
#ifndef NDEBUG
        assert(next_hash == zobrist_hash(next.to_board()));
#endif

        const SearchNodeResult child = search_node(next, next_hash, depth - 1, -beta, -alpha,
                                                   context, std::nullopt,
                                                   child_hint_after_pass(pv_hint), false, pv_table,
                                                   ply + 1);
        pv_table.copy_from_child(ply);
        const SearchNodeResult result{
            .score = -child.score,
        };
        store_transposition(context, hash, depth, result.score, original_alpha, beta,
                            result.best_move);
        return result;
    }

    ++context.stats.legal_move_nodes;
    std::optional<int> best_score;
    std::optional<int> best_move_index;
    pv_table.clear(ply);

    const bool use_dynamic_ordering = context.dynamic_move_ordering && !is_root;
    const auto move_count = static_cast<std::size_t>(std::popcount(moves));
    const std::optional<Square> principal_variation_move = preferred_move_from_hint(pv_hint);
    const std::optional<Square> promoted_preferred_move =
        principal_variation_move.has_value()
            ? principal_variation_move
            : (is_root ? root_preferred_move : std::optional<Square>{});
    const std::optional<Square> lazy_preferred_move = preferred_move_for_node(
        principal_variation_move, root_preferred_move, cached.best_move_hint, is_root);
    const bool lazy_first_enabled = context.use_lazy_first_move_ordering &&
                                    lazy_preferred_move.has_value() &&
                                    (moves & lazy_preferred_move->bit()) != 0;

    Bitboard remaining_moves = moves;
    std::size_t searched_move_count = 0;
    const bool use_pvs_at_node = context.use_pvs && move_count > 1 && depth >= 3;

    if (lazy_first_enabled) {
        const Bitboard lazy_move_bit = lazy_preferred_move->bit();
        const Bitboard flips = flips_for_known_empty_move(position, lazy_move_bit);
        if (flips != 0) {
            ++context.stats.preferred_move_legal_count;
            ++context.stats.ordering_lazy_first_hits;
            if (same_move(lazy_preferred_move, cached.best_move_hint)) {
                ++context.stats.tt_move_ordering_used;
                if (cached.best_move_hint_from_shallow_entry) {
                    ++context.stats.shallow_tt_move_ordering_used;
                }
            }

            const SearchPosition next = position_after_move_bit(position, lazy_move_bit, flips);
            const ZobristHash next_hash =
                hash_after_move(hash, position.side_to_move, *lazy_preferred_move, flips);
#ifndef NDEBUG
            assert(next_hash == zobrist_hash(next.to_board()));
#endif
            ++context.stats.searched_moves;

            const PrincipalVariationHint child_hint =
                child_hint_after_move(pv_hint, *lazy_preferred_move);
            const SearchNodeResult child = search_node(next, next_hash, depth - 1, -beta, -alpha,
                                                       context, std::nullopt, child_hint, false,
                                                       pv_table, ply + 1);
            const int candidate_score = -child.score;
            best_score = candidate_score;
            best_move_index = lazy_preferred_move->index();
            pv_table.update_with_move_index(ply, *best_move_index);

            ++searched_move_count;
            alpha = std::max(alpha, candidate_score);
            remaining_moves &= ~lazy_move_bit;
            if (alpha >= beta) {
                ++context.stats.beta_cutoffs;
                ++context.stats.beta_cutoffs_first_move;
                ++context.stats.preferred_move_beta_cut_count;
                ++context.stats.ordering_lazy_cut_before_full_sort;
                context.stats.ordering_scored_moves_saved += move_count - 1;
                record_history_killer_cutoff(context, *lazy_preferred_move, depth);
                const std::optional<Square> best_move =
                    square_from_transposition_index(*best_move_index);
                store_transposition(context, hash, depth, candidate_score, original_alpha, beta,
                                    best_move);
                return SearchNodeResult{
                    .best_move = best_move,
                    .score = candidate_score,
                };
            }
        }
    }

    if (remaining_moves == 0) {
        const std::optional<Square> best_move =
            best_move_index.has_value() ? square_from_transposition_index(*best_move_index)
                                        : std::nullopt;
        const SearchNodeResult result{
            .best_move = best_move,
            .score = best_score.value_or(evaluate_for_search(position, context)),
        };
        store_transposition(context, hash, depth, result.score, original_alpha, beta,
                            result.best_move);
        return result;
    }

    const auto remaining_move_count = static_cast<std::size_t>(std::popcount(remaining_moves));
    if (should_use_dynamic_move_ordering(use_dynamic_ordering, remaining_move_count, depth,
                                         context.move_ordering_params)) {
        ++context.stats.dynamic_ordering_nodes;
        context.stats.dynamic_ordering_moves += remaining_move_count;
    }
    ++context.stats.ordering_full_builds;
    const std::optional<Square> remaining_preferred_move =
        searched_move_count == 0 ? promoted_preferred_move : std::nullopt;
    const std::optional<Square> remaining_tt_preferred_move =
        searched_move_count > 0 && same_move(lazy_preferred_move, cached.best_move_hint)
            ? std::nullopt
            : cached.best_move_hint;
    const bool remaining_tt_preferred_move_from_shallow =
        remaining_tt_preferred_move.has_value() && cached.best_move_hint_from_shallow_entry;
    const OrderedMoveIndexes ordered_moves =
        ordered_legal_move_indexes(position, remaining_moves, depth, remaining_preferred_move,
                                   remaining_tt_preferred_move,
                                   remaining_tt_preferred_move_from_shallow, use_dynamic_ordering,
                                   context);
    if (is_root) {
        record_root_move_ordering_snapshot(context, ordered_moves);
    }
    // Avoid very shallow scouts; with the current ordering, depth-2 null windows tend
    // to add overhead while giving little pruning back.
    for (std::size_t move = 0; move < ordered_moves.size; ++move) {
        const auto& ordered_move = ordered_moves.moves[move];

        const Bitboard flips = ordered_move.flips;
        if (flips == 0) {
            continue;
        }

        const Bitboard move_bit = Bitboard{1} << ordered_move.index;
        const SearchPosition next =
            position_after_move_bit(position, move_bit, flips);
        const ZobristHash next_hash =
            hash_after_move(hash, position.side_to_move, ordered_move.index, flips);
#ifndef NDEBUG
        assert(next_hash == zobrist_hash(next.to_board()));
#endif
        ++context.stats.searched_moves;

        const PrincipalVariationHint child_hint =
            child_hint_after_move_index(pv_hint, ordered_move.index);
        SearchNodeResult child;
        if (!use_pvs_at_node || searched_move_count == 0) {
            child = search_node(next, next_hash, depth - 1, -beta, -alpha, context, std::nullopt,
                                child_hint, false, pv_table, ply + 1);
        } else {
            ++context.stats.pvs_scouts;
            child = search_node(next, next_hash, depth - 1, -(alpha + 1), -alpha, context,
                                std::nullopt, child_hint, false, pv_table, ply + 1);
            const int scout_score = -child.score;
            if (scout_score > alpha && scout_score < beta) {
                ++context.stats.pvs_researches;
                child = search_node(next, next_hash, depth - 1, -beta, -alpha, context,
                                    std::nullopt, child_hint, false, pv_table, ply + 1);
            } else {
                // Counts null-window searches that did not need full-window re-search,
                // including both fail-low scouts and scout beta cutoffs.
                ++context.stats.pvs_scout_cutoffs;
            }
        }
        const int candidate_score = -child.score;
        if (is_better_best_move_index(candidate_score, ordered_move.index, best_score,
                                      best_move_index)) {
            best_score = candidate_score;
            best_move_index = ordered_move.index;
            pv_table.update_with_move_index(ply, ordered_move.index);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            ++context.stats.beta_cutoffs;
            if (searched_move_count == 0) {
                ++context.stats.beta_cutoffs_first_move;
            }
            record_history_killer_cutoff_index(context, ordered_move.index, depth);
            break;
        }
        ++searched_move_count;
    }

    const std::optional<Square> best_move =
        best_move_index.has_value() ? square_from_transposition_index(*best_move_index)
                                    : std::nullopt;
    const SearchNodeResult result{
        .best_move = best_move,
        .score = best_score.value_or(evaluate_for_search(position, context)),
    };
    store_transposition(context, hash, depth, result.score, original_alpha, beta, result.best_move);
    return result;
}

SearchResult search_with_context(const Board& board, int depth, SearchContext& context, int alpha,
                                 int beta, std::optional<Square> root_preferred_move,
                                 PrincipalVariationHint pv_hint) noexcept {
    const SearchPosition position = SearchPosition::from_board(board);
    const ZobristHash root_hash = zobrist_hash(board);
    MidgamePvTable pv_table;
    const SearchNodeResult result = search_node(position, root_hash, depth, alpha, beta, context,
                                                root_preferred_move, pv_hint, true, pv_table, 0);
    PrincipalVariation principal_variation = pv_table.lines[0];
    if (principal_variation.size < context.session.root_principal_variation.size &&
        context.session.root_hash == root_hash && context.session.previous_score == result.score &&
        context.session.previous_best_move == result.best_move) {
        principal_variation = context.session.root_principal_variation;
    }

    return SearchResult{
        .best_move = result.best_move,
        .score = result.score,
        .depth = depth,
        .nodes = context.stats.nodes,
        .principal_variation = principal_variation_to_vector(principal_variation),
        .stats = context.stats,
        .score_kind = SearchScoreKind::Heuristic,
        .used_exact_endgame = false,
        .exact_disc_margin = std::nullopt,
    };
}

SearchResult search_with_context(const Board& board, int depth, SearchContext& context,
                                 std::optional<Square> root_preferred_move,
                                 PrincipalVariationHint pv_hint) noexcept {
    return search_with_context(board, depth, context, search_score_min, search_score_max,
                               root_preferred_move, pv_hint);
}

} // namespace othello::search_detail
