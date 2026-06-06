#include "bitboard_ops.hpp"
#include "hash_update.hpp"
#include "search_aspiration.hpp"
#include "search_bounds.hpp"
#include "search_common.hpp"
#include "search_context.hpp"
#include "search_core.hpp"
#include "search_root_policy.hpp"
#include "search_runtime_options.hpp"
#include "search_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <othello/endgame.hpp>
#include <othello/evaluation.hpp>
#include <othello/hash.hpp>
#include <othello/rules.hpp>
#include <othello/search.hpp>
#include <utility>
#include <vector>

namespace othello {

namespace {

using bitboard_detail::corner_squares;
using bitboard_detail::adjacent_squares;
using bitboard_detail::is_corner;
using bitboard_detail::is_edge;
using bitboard_detail::is_x_square;
using bitboard_detail::is_x_square_next_to_empty_corner;
using hash_detail::hash_after_move;
using hash_detail::hash_after_pass;
using search_detail::empty_count;
using search_detail::flips_for_move;
using search_detail::is_better_best_move;
using search_detail::legal_moves;
using search_detail::node_result_from_transposition_entry;
using search_detail::NodeResult;
using search_detail::diagnostics_options_from;
using search_detail::engine_options_from;
using search_detail::position_after_move;
using search_detail::position_after_pass;
using search_detail::principal_variation_from_vector;
using search_detail::principal_variation_to_vector;
using search_detail::principal_variation_with_move;
using search_detail::MoveOrderingParams;
using search_detail::PrincipalVariation;
using search_detail::PrincipalVariationHint;
using search_detail::SearchContext;
using search_detail::SearchPosition;
using search_detail::SearchDiagnosticsOptions;
using search_detail::SearchEngineOptions;
using search_detail::SearchSessionState;
using search_detail::TranspositionLookup;
using search_detail::TranspositionScope;
using search_detail::TranspositionTable;

struct OrderedMoveIndexes {
    struct Move {
        int index = -1;
        Bitboard flips = 0;
        int order_score = 0;
    };

    std::array<Move, 64> moves{};
    std::size_t size = 0;
};

// Keep exact root endgame scores comparable with evaluate_basic terminal scores.
// If the terminal score weight in evaluation.cpp changes, update this scale too.
constexpr int exact_endgame_score_scale = 1'000;

[[nodiscard]] int evaluate_for_search(const SearchPosition& position,
                                      SearchContext& context) noexcept {
    ++context.stats.eval_calls;
    const Board board = position.to_board();
    return evaluate_with_config(board, position.side_to_move, context.evaluation_config);
}

[[nodiscard]] SearchStats stats_delta(const SearchStats& after, const SearchStats& before) noexcept {
    return SearchStats{
        .nodes = after.nodes - before.nodes,
        .beta_cutoffs = after.beta_cutoffs - before.beta_cutoffs,
        .beta_cutoffs_first_move =
            after.beta_cutoffs_first_move - before.beta_cutoffs_first_move,
        .searched_moves = after.searched_moves - before.searched_moves,
        .legal_move_nodes = after.legal_move_nodes - before.legal_move_nodes,
        .eval_calls = after.eval_calls - before.eval_calls,
        .pass_nodes = after.pass_nodes - before.pass_nodes,
        .game_over_nodes = after.game_over_nodes - before.game_over_nodes,
        .tt_lookups = after.tt_lookups - before.tt_lookups,
        .tt_hits = after.tt_hits - before.tt_hits,
        .tt_exact_hits = after.tt_exact_hits - before.tt_exact_hits,
        .tt_lower_hits = after.tt_lower_hits - before.tt_lower_hits,
        .tt_upper_hits = after.tt_upper_hits - before.tt_upper_hits,
        .tt_stores = after.tt_stores - before.tt_stores,
        .tt_leaf_stores = after.tt_leaf_stores - before.tt_leaf_stores,
        .tt_overwrites = after.tt_overwrites - before.tt_overwrites,
        .tt_collisions = after.tt_collisions - before.tt_collisions,
        .tt_rejected_stores = after.tt_rejected_stores - before.tt_rejected_stores,
        .tt_move_ordering_probes =
            after.tt_move_ordering_probes - before.tt_move_ordering_probes,
        .tt_move_ordering_hits = after.tt_move_ordering_hits - before.tt_move_ordering_hits,
        .tt_move_ordering_used = after.tt_move_ordering_used - before.tt_move_ordering_used,
        .pvs_scouts = after.pvs_scouts - before.pvs_scouts,
        .pvs_researches = after.pvs_researches - before.pvs_researches,
        .pvs_scout_cutoffs = after.pvs_scout_cutoffs - before.pvs_scout_cutoffs,
        .aspiration_searches = after.aspiration_searches - before.aspiration_searches,
        .aspiration_researches = after.aspiration_researches - before.aspiration_researches,
        .aspiration_fail_lows = after.aspiration_fail_lows - before.aspiration_fail_lows,
        .aspiration_fail_highs = after.aspiration_fail_highs - before.aspiration_fail_highs,
        .aspiration_full_window_fallbacks =
            after.aspiration_full_window_fallbacks - before.aspiration_full_window_fallbacks,
        .aspiration_fail_low_distance_sum =
            after.aspiration_fail_low_distance_sum - before.aspiration_fail_low_distance_sum,
        .aspiration_fail_high_distance_sum =
            after.aspiration_fail_high_distance_sum - before.aspiration_fail_high_distance_sum,
        .aspiration_fail_low_distance_max =
            after.aspiration_fail_low_distance_max > before.aspiration_fail_low_distance_max
                ? after.aspiration_fail_low_distance_max
                : 0,
        .aspiration_fail_high_distance_max =
            after.aspiration_fail_high_distance_max > before.aspiration_fail_high_distance_max
                ? after.aspiration_fail_high_distance_max
                : 0,
        .dynamic_ordering_nodes = after.dynamic_ordering_nodes - before.dynamic_ordering_nodes,
        .dynamic_ordering_moves = after.dynamic_ordering_moves - before.dynamic_ordering_moves,
    };
}

void record_root_move_ordering_snapshot(SearchContext& context,
                                        const OrderedMoveIndexes& ordered_moves) noexcept {
    if (context.diagnostics.root_move_ordering_snapshot == nullptr) {
        return;
    }

    context.diagnostics.root_move_ordering_snapshot->clear();
    context.diagnostics.root_move_ordering_snapshot->reserve(ordered_moves.size);
    for (std::size_t index = 0; index < ordered_moves.size; ++index) {
        const std::optional<Square> move =
            Square::from_index(ordered_moves.moves[index].index);
        if (!move.has_value()) {
            continue;
        }
        context.diagnostics.root_move_ordering_snapshot->push_back(RootMoveOrderingEntry{
            .move = *move,
            .order_score = ordered_moves.moves[index].order_score,
        });
    }
}

void store_leaf_transposition(SearchContext& context, ZobristHash hash, int depth, int score,
                              int original_alpha, int beta,
                              const std::optional<Square>& best_move) noexcept {
    if (!context.store_leaf_tt_entries) {
        return;
    }
    if (context.transpositions.store(hash, context.transposition_scope,
                                     context.session.generation, depth, score, original_alpha,
                                     beta, best_move, context.stats)) {
        ++context.stats.tt_leaf_stores;
    }
}

[[nodiscard]] constexpr bool
should_use_dynamic_move_ordering(bool enabled, std::size_t move_count, int depth,
                                 const MoveOrderingParams& params) noexcept {
    return enabled && depth >= params.dynamic_min_depth && move_count >= params.dynamic_min_moves;
}

[[nodiscard]] int history_killer_bonus(const SearchContext& context, int index,
                                       int depth) noexcept {
    return search_detail::history_killer_bonus(context.session.history_killers, index, depth,
                                               context.move_ordering_params.history_killer);
}

void record_history_killer_cutoff(SearchContext& context, Square move, int depth) noexcept {
    search_detail::record_history_killer_cutoff(context.session.history_killers, move.index(),
                                                depth,
                                                context.move_ordering_params.history_killer);
}

[[nodiscard]] int potential_mobility_after_move(const SearchPosition& next) noexcept {
    return std::popcount(adjacent_squares(next.opponent_discs) & ~next.occupied());
}

[[nodiscard]] int static_risk_for_move(const SearchPosition& position, int index) noexcept {
    return is_x_square_next_to_empty_corner(index, position.occupied()) ? 1 : 0;
}

[[nodiscard]] int move_order_score(const SearchPosition& position, Square square, Bitboard flips,
                                   std::size_t move_count, int depth, bool dynamic_move_ordering,
                                   SearchContext& context) noexcept {
    const MoveOrderingParams& params = context.move_ordering_params;
    const int index = square.index();
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
    const SearchPosition next = position_after_move(position, square, flips);
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

[[nodiscard]] OrderedMoveIndexes
ordered_legal_move_indexes(const SearchPosition& position, Bitboard moves, int depth,
                           bool dynamic_move_ordering, SearchContext& context) noexcept {
    OrderedMoveIndexes candidates;
    const auto move_count = static_cast<std::size_t>(std::popcount(moves));

    while (moves != 0) {
        const int index = std::countr_zero(moves);
        moves &= moves - 1;

        const std::optional<Square> square = Square::from_index(index);
        if (!square.has_value()) {
            continue;
        }

        const Bitboard flips = flips_for_move(position, *square);
        if (flips == 0) {
            continue;
        }

        candidates.moves[candidates.size] = OrderedMoveIndexes::Move{
            .index = index,
            .flips = flips,
            .order_score = move_order_score(position, *square, flips, move_count, depth,
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

[[nodiscard]] bool promote_preferred_move(OrderedMoveIndexes& candidates,
                                          Square preferred_move) noexcept {
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

[[nodiscard]] OrderedMoveIndexes
ordered_legal_move_indexes(const SearchPosition& position, Bitboard moves, int depth,
                           std::optional<Square> preferred_move,
                           std::optional<Square> tt_preferred_move, bool dynamic_move_ordering,
                           SearchContext& context) noexcept {
    OrderedMoveIndexes candidates =
        ordered_legal_move_indexes(position, moves, depth, dynamic_move_ordering, context);

    if (tt_preferred_move.has_value() && (moves & tt_preferred_move->bit()) != 0 &&
        promote_preferred_move(candidates, *tt_preferred_move)) {
        ++context.stats.tt_move_ordering_used;
    }

    if (preferred_move.has_value() && (moves & preferred_move->bit()) != 0) {
        static_cast<void>(promote_preferred_move(candidates, *preferred_move));
    }

    return candidates;
}

[[nodiscard]] std::optional<Square> preferred_move_from_hint(PrincipalVariationHint hint) noexcept {
    if (hint.principal_variation == nullptr || !hint.matches_prefix ||
        hint.index >= hint.principal_variation->size) {
        return std::nullopt;
    }

    return Square::from_index(hint.principal_variation->indexes[hint.index]);
}

[[nodiscard]] PrincipalVariationHint child_hint_after_move(PrincipalVariationHint hint,
                                                           Square move) noexcept {
    const std::optional<Square> preferred_move = preferred_move_from_hint(hint);
    if (preferred_move.has_value() && *preferred_move == move) {
        return PrincipalVariationHint{
            .principal_variation = hint.principal_variation,
            .index = hint.index + 1,
            .matches_prefix = true,
        };
    }

    return PrincipalVariationHint{};
}

[[nodiscard]] PrincipalVariationHint child_hint_after_pass(PrincipalVariationHint hint) noexcept {
    return hint;
}

// Fixed-depth negamax is easiest to audit as direct recursion at this stage.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult search_node(const SearchPosition& position, ZobristHash hash, int depth,
                                     int alpha, int beta, SearchContext& context,
                                     std::optional<Square> root_preferred_move,
                                     PrincipalVariationHint pv_hint, bool is_root) noexcept {
    ++context.stats.nodes;

    const int original_alpha = alpha;

    const bool collect_tt_best_move_hint = context.dynamic_move_ordering && !is_root &&
                                           depth >= context.move_ordering_params.dynamic_min_depth;
    const TranspositionLookup cached = context.transpositions.lookup(
        hash, context.transposition_scope, depth, alpha, beta, collect_tt_best_move_hint,
        context.stats);
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
            context.transpositions.store(hash, context.transposition_scope,
                                         context.session.generation, depth, result.score,
                                         original_alpha, beta, result.best_move, context.stats);
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
        context.transpositions.store(hash, context.transposition_scope,
                                     context.session.generation, depth, result.score,
                                     original_alpha, beta, result.best_move, context.stats);
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

    const OrderedMoveIndexes ordered_moves = ordered_legal_move_indexes(
        position, moves, depth, preferred_move, cached.best_move_hint, use_dynamic_ordering,
        context);
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
        const ZobristHash next_hash =
            hash_after_move(hash, position.side_to_move, *square, flips);
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
    context.transpositions.store(hash, context.transposition_scope, context.session.generation,
                                 depth, result.score, original_alpha, beta, result.best_move,
                                 context.stats);
    return result;
}

} // namespace

namespace search_detail {

SearchResult search_with_context(const Board& board, int depth,
                                 SearchContext& context, int alpha, int beta,
                                 std::optional<Square> root_preferred_move,
                                 PrincipalVariationHint pv_hint) noexcept {
    const SearchPosition position = SearchPosition::from_board(board);
    const ZobristHash root_hash = zobrist_hash(board);
    const NodeResult result = search_node(position, root_hash, depth, alpha, beta,
                                          context, root_preferred_move, pv_hint, true);
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

} // namespace search_detail

namespace {

[[nodiscard]] SearchResult exact_endgame_search_result(const Board& board) noexcept {
    ExactEndgameResult exact = solve_exact_endgame(board);
    const SearchStats stats{
        .nodes = exact.nodes,
        .tt_lookups = exact.stats.tt_lookups,
        .tt_hits = exact.stats.tt_hits,
        .tt_exact_hits = exact.stats.tt_exact_hits,
        .tt_lower_hits = exact.stats.tt_lower_hits,
        .tt_upper_hits = exact.stats.tt_upper_hits,
        .tt_stores = exact.stats.tt_stores,
        .tt_overwrites = exact.stats.tt_overwrites,
        .tt_collisions = exact.stats.tt_collisions,
        .tt_rejected_stores = exact.stats.tt_rejected_stores,
        .tt_move_ordering_probes = exact.stats.tt_move_ordering_probes,
        .tt_move_ordering_hits = exact.stats.tt_move_ordering_hits,
        .tt_move_ordering_used = exact.stats.tt_move_ordering_used,
    };

    return SearchResult{
        .best_move = exact.best_move,
        .score = exact.disc_margin * exact_endgame_score_scale,
        .depth = exact.empties,
        .nodes = exact.nodes,
        .principal_variation = std::move(exact.principal_variation),
        .stats = stats,
        .score_kind = SearchScoreKind::ExactDiscMarginScaled,
        .used_exact_endgame = true,
        .exact_disc_margin = exact.disc_margin,
    };
}

} // namespace

SearchSession::SearchSession() : state_{std::make_unique<search_detail::SearchSessionState>()} {}

SearchSession::SearchSession(const SearchOptions& options)
    : state_{std::make_unique<search_detail::SearchSessionState>(options)} {}

SearchSession::SearchSession(SearchSession&&) noexcept = default;

SearchSession& SearchSession::operator=(SearchSession&&) noexcept = default;

SearchSession::~SearchSession() = default;

void SearchSession::reset() noexcept {
    if (state_ != nullptr) {
        state_->reset();
    }
}

std::uint32_t SearchSession::generation() const noexcept {
    return state_ == nullptr ? 0 : state_->generation;
}

SearchMode SearchSession::mode() const noexcept {
    return state_ == nullptr ? SearchMode::FixedDepth : state_->mode;
}

std::optional<Square> SearchSession::previous_best_move() const noexcept {
    return state_ == nullptr ? std::nullopt : state_->previous_best_move;
}

std::vector<Square> SearchSession::root_principal_variation() const {
    if (state_ == nullptr) {
        return {};
    }
    return search_detail::principal_variation_to_vector(state_->root_principal_variation);
}

SearchResult search(SearchSession& session, const Board& board,
                    const SearchOptions& options) noexcept {
    if (session.state_ == nullptr) {
        session.state_ = std::make_unique<search_detail::SearchSessionState>();
    }

    const SearchEngineOptions engine_options = engine_options_from(options);
    const SearchDiagnosticsOptions diagnostics_options = diagnostics_options_from(options);
    begin_session_search(*session.state_, engine_options, resolve_evaluation_config(options),
                         SearchMode::FixedDepth);

    if (should_solve_exact_endgame_at_root(board, options)) {
        session.state_->mode = SearchMode::ExactEndgame;
        SearchResult result = exact_endgame_search_result(board);
        finish_session_search(*session.state_, zobrist_hash(board), result);
        return result;
    }

    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{*session.state_, engine_options, diagnostics_options, true};
    const PrincipalVariationHint pv_hint{
        .principal_variation = session.state_->root_principal_variation.size == 0
                                   ? nullptr
                                   : &session.state_->root_principal_variation,
        .index = 0,
        .matches_prefix = session.state_->root_principal_variation.size > 0,
    };
    SearchResult result = search_with_context(board, search_depth, context,
                                              session.state_->previous_best_move, pv_hint);
    finish_session_search(*session.state_, zobrist_hash(board), result);
    return result;
}

SearchResult search(const Board& board, const SearchOptions& options) noexcept {
    SearchSession session{options};
    return search(session, board, options);
}

SearchResult search_fixed_depth(const Board& board, int depth) noexcept {
    return search(board, SearchOptions{.max_depth = depth});
}

SearchResult search_iterative(SearchSession& session, const Board& board,
                              const SearchOptions& options) noexcept {
    if (session.state_ == nullptr) {
        session.state_ = std::make_unique<search_detail::SearchSessionState>();
    }

    const SearchEngineOptions engine_options = engine_options_from(options);
    const SearchDiagnosticsOptions diagnostics_options = diagnostics_options_from(options);
    begin_session_search(*session.state_, engine_options, resolve_evaluation_config(options),
                         SearchMode::Iterative);

    if (should_solve_exact_endgame_at_root(board, options)) {
        session.state_->mode = SearchMode::ExactEndgame;
        SearchResult result = exact_endgame_search_result(board);
        finish_session_search(*session.state_, zobrist_hash(board), result);
        return result;
    }

    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{*session.state_, engine_options, diagnostics_options, true};

    if (search_depth == 0) {
        SearchResult result = search_with_context(board, 0, context,
                                                  session.state_->previous_best_move,
                                                  PrincipalVariationHint{});
        finish_session_search(*session.state_, zobrist_hash(board), result);
        return result;
    }

    SearchResult result;
    std::optional<Square> previous_best_move = session.state_->previous_best_move;
    std::optional<int> previous_score;
    std::optional<int> previous_score_delta;
    PrincipalVariation previous_principal_variation = session.state_->root_principal_variation;
    for (int depth = 1; depth <= search_depth; ++depth) {
        const SearchStats stats_before_depth = context.stats;
        const auto depth_start = std::chrono::steady_clock::now();
        const PrincipalVariationHint pv_hint{
            .principal_variation =
                previous_principal_variation.size == 0 ? nullptr : &previous_principal_variation,
            .index = 0,
            .matches_prefix = previous_principal_variation.size > 0,
        };
        if (engine_options.use_aspiration_window && previous_score.has_value()) {
            const int initial_window =
                aspiration_initial_window(engine_options, previous_score_delta);
            result = search_aspirated_with_context(board, depth, context, previous_best_move,
                                                   pv_hint, *previous_score, initial_window,
                                                   engine_options);
        } else {
            result = search_with_context(board, depth, context, previous_best_move, pv_hint);
        }
        const auto depth_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - depth_start);
        if (diagnostics_options.iterative_depth_observer != nullptr) {
            const SearchStats depth_stats = stats_delta(context.stats, stats_before_depth);
            const IterativeSearchDepthInfo info{
                .requested_depth = search_depth,
                .completed_depth = depth,
                .previous_score = previous_score,
                .score = result.score,
                .previous_score_delta =
                    previous_score.has_value() ? result.score - *previous_score : 0,
                .best_move = result.best_move,
                .principal_variation = result.principal_variation,
                .stats = depth_stats,
                .elapsed_ns = static_cast<std::uint64_t>(depth_elapsed.count()),
            };
            diagnostics_options.iterative_depth_observer(
                info, diagnostics_options.iterative_depth_observer_user_data);
        }
        previous_score_delta = previous_score.has_value()
                                   ? std::optional<int>{result.score - *previous_score}
                                   : std::nullopt;
        previous_best_move = result.best_move;
        previous_score = result.score;
        previous_principal_variation = principal_variation_from_vector(result.principal_variation);
    }

    finish_session_search(*session.state_, zobrist_hash(board), result);
    return result;
}

SearchResult search_iterative(const Board& board, const SearchOptions& options) noexcept {
    SearchSession session{options};
    return search_iterative(session, board, options);
}

} // namespace othello
