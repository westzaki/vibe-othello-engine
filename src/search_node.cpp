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
                                         collect_best_move_hint, context.stats);
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

} // namespace

// Fixed-depth negamax is easiest to audit as direct recursion at this stage.
// NOLINTNEXTLINE(misc-no-recursion)
NodeResult search_node(const SearchPosition& position, ZobristHash hash, int depth, int alpha,
                       int beta, SearchContext& context, std::optional<Square> root_preferred_move,
                       PrincipalVariationHint pv_hint, bool is_root) noexcept {
    ++context.stats.nodes;

    const int original_alpha = alpha;

    const bool collect_tt_best_move_hint = context.dynamic_move_ordering && !is_root &&
                                           depth >= context.move_ordering_params.dynamic_min_depth;
    const TranspositionLookup cached =
        probe_transposition(context, hash, depth, alpha, beta, collect_tt_best_move_hint);
    if (cached.cutoff.has_value()) {
        return *cached.cutoff;
    }

    if (depth <= 0) {
        const NodeResult result{.score = evaluate_for_search(position, context)};
        store_leaf_transposition(context, hash, depth, result.score, original_alpha, beta,
                                 result.best_move);
        return result;
    }

    const Bitboard moves = legal_moves(position);
    if (moves == 0) {
        const SearchPosition next = position_after_pass(position);
        if (legal_moves(next) == 0) {
            ++context.stats.game_over_nodes;
            const NodeResult result{.score = evaluate_for_search(position, context)};
            store_transposition(context, hash, depth, result.score, original_alpha, beta,
                                result.best_move);
            return result;
        }

        ++context.stats.pass_nodes;
        const ZobristHash next_hash = hash_after_pass(hash, position.side_to_move);
#ifndef NDEBUG
        assert(next_hash == zobrist_hash(next.to_board()));
#endif

        const NodeResult child = search_node(next, next_hash, depth - 1, -beta, -alpha, context,
                                             std::nullopt, child_hint_after_pass(pv_hint), false);
        const NodeResult result{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
        store_transposition(context, hash, depth, result.score, original_alpha, beta,
                            result.best_move);
        return result;
    }

    ++context.stats.legal_move_nodes;
    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    const bool use_dynamic_ordering = context.dynamic_move_ordering && !is_root;
    const auto move_count = static_cast<std::size_t>(std::popcount(moves));
    if (should_use_dynamic_move_ordering(use_dynamic_ordering, move_count, depth,
                                         context.move_ordering_params)) {
        ++context.stats.dynamic_ordering_nodes;
        context.stats.dynamic_ordering_moves += move_count;
    }
    std::optional<Square> preferred_move = preferred_move_from_hint(pv_hint);
    if (!preferred_move.has_value() && is_root) {
        preferred_move = root_preferred_move;
    }

    const OrderedMoveIndexes ordered_moves =
        ordered_legal_move_indexes(position, moves, depth, preferred_move, cached.best_move_hint,
                                   use_dynamic_ordering, context);
    if (is_root) {
        record_root_move_ordering_snapshot(context, ordered_moves);
    }
    // Avoid very shallow scouts; with the current ordering, depth-2 null windows tend
    // to add overhead while giving little pruning back.
    const bool use_pvs_at_node = context.use_pvs && ordered_moves.size > 1 && depth >= 3;
    for (std::size_t move = 0; move < ordered_moves.size; ++move) {
        const auto& ordered_move = ordered_moves.moves[move];
        const std::optional<Square> square = Square::from_index(ordered_move.index);
        if (!square.has_value()) {
            continue;
        }

        const Bitboard flips = ordered_move.flips;
        if (flips == 0) {
            continue;
        }

        const SearchPosition next = position_after_move(position, *square, flips);
        const ZobristHash next_hash = hash_after_move(hash, position.side_to_move, *square, flips);
#ifndef NDEBUG
        assert(next_hash == zobrist_hash(next.to_board()));
#endif
        ++context.stats.searched_moves;

        const PrincipalVariationHint child_hint = child_hint_after_move(pv_hint, *square);
        NodeResult child;
        if (!use_pvs_at_node || move == 0) {
            child = search_node(next, next_hash, depth - 1, -beta, -alpha, context, std::nullopt,
                                child_hint, false);
        } else {
            ++context.stats.pvs_scouts;
            child = search_node(next, next_hash, depth - 1, -(alpha + 1), -alpha, context,
                                std::nullopt, child_hint, false);
            const int scout_score = -child.score;
            if (scout_score > alpha && scout_score < beta) {
                ++context.stats.pvs_researches;
                child = search_node(next, next_hash, depth - 1, -beta, -alpha, context,
                                    std::nullopt, child_hint, false);
            } else {
                // Counts null-window searches that did not need full-window re-search,
                // including both fail-low scouts and scout beta cutoffs.
                ++context.stats.pvs_scout_cutoffs;
            }
        }
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, *square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(*square, child.principal_variation);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            ++context.stats.beta_cutoffs;
            if (move == 0) {
                ++context.stats.beta_cutoffs_first_move;
            }
            record_history_killer_cutoff(context, *square, depth);
            break;
        }
    }

    const NodeResult result{
        .best_move = best_move,
        .score = best_score.value_or(evaluate_for_search(position, context)),
        .principal_variation = best_principal_variation,
    };
    store_transposition(context, hash, depth, result.score, original_alpha, beta, result.best_move);
    return result;
}

SearchResult search_with_context(const Board& board, int depth, SearchContext& context, int alpha,
                                 int beta, std::optional<Square> root_preferred_move,
                                 PrincipalVariationHint pv_hint) noexcept {
    const SearchPosition position = SearchPosition::from_board(board);
    const ZobristHash root_hash = zobrist_hash(board);
    const NodeResult result = search_node(position, root_hash, depth, alpha, beta, context,
                                          root_preferred_move, pv_hint, true);
    PrincipalVariation principal_variation = result.principal_variation;
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
