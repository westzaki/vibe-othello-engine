#include "bitboard_ops.hpp"
#include "hash_update.hpp"
#include "search_common.hpp"
#include "search_runtime_options.hpp"
#include "search_tt.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <othello/endgame.hpp>
#include <othello/evaluation.hpp>
#include <othello/hash.hpp>
#include <othello/rules.hpp>
#include <othello/search.hpp>
#include <utility>
#include <vector>

namespace othello {

EvaluationConfig resolve_evaluation_config(const SearchOptions& options) noexcept {
    if (options.evaluation_config_override.has_value()) {
        return *options.evaluation_config_override;
    }
    return default_evaluation_config();
}

namespace {

using bitboard_detail::corner_squares;
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
using search_detail::PrincipalVariation;
using search_detail::SearchPosition;
using search_detail::SearchDiagnosticsOptions;
using search_detail::SearchEngineOptions;
using search_detail::TranspositionLookup;
using search_detail::TranspositionTable;

struct PrincipalVariationHint {
    const PrincipalVariation* principal_variation = nullptr;
    std::size_t index = 0;
    bool matches_prefix = false;
};

struct OrderedMoveIndexes {
    struct Move {
        int index = -1;
        Bitboard flips = 0;
        int order_score = 0;
    };

    std::array<Move, 64> moves{};
    std::size_t size = 0;
};

struct MoveOrderingParams {
    int static_corner_score = 3'000;
    int static_edge_score = 2'000;
    int static_normal_score = 1'000;
    int static_x_square_score = 0;

    int dynamic_corner_bonus = 100'000;
    int dynamic_edge_bonus = 1'000;
    int dynamic_x_square_empty_corner_penalty = 30'000;
    int dynamic_opponent_corner_penalty = 80'000;
    int dynamic_opponent_mobility_penalty = 500;

    int dynamic_min_depth = 3;
    std::size_t dynamic_min_moves = 5;
};

constexpr MoveOrderingParams default_move_ordering_params{};

struct SearchContext {
    explicit SearchContext(SearchEngineOptions engine, SearchDiagnosticsOptions diagnostics_options,
                           EvaluationConfig config,
                           bool enable_dynamic_move_ordering) noexcept
        : engine_options{engine}, transpositions{engine_options},
          dynamic_move_ordering{enable_dynamic_move_ordering},
          use_pvs{engine_options.use_pvs},
          store_leaf_tt_entries{engine_options.store_leaf_tt_entries},
          evaluation_config{std::move(config)}, diagnostics{diagnostics_options} {}

    SearchStats stats;
    SearchEngineOptions engine_options;
    TranspositionTable transpositions;
    MoveOrderingParams move_ordering_params = default_move_ordering_params;
    bool dynamic_move_ordering = false;
    bool use_pvs = false;
    bool store_leaf_tt_entries = true;
    EvaluationConfig evaluation_config = default_evaluation_config();
    SearchDiagnosticsOptions diagnostics;
};

constexpr int search_score_min = -1'000'000'000;
constexpr int search_score_max = 1'000'000'000;
// Keep exact root endgame scores comparable with evaluate_basic terminal scores.
// If the terminal score weight in evaluation.cpp changes, update this scale too.
constexpr int exact_endgame_score_scale = 1'000;

struct ExactRootPolicyParams {
    int always_exact_max_empties = 14;
    int adaptive_max_empties = 16;
    int max_legal_moves = 10;
    int max_opponent_legal_moves = 10;
};

constexpr ExactRootPolicyParams adaptive16_exact_root_policy_params{};

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

void record_aspiration_fail_low_distance(SearchContext& context, int score, int alpha) noexcept {
    const std::uint64_t distance =
        static_cast<std::uint64_t>(alpha > score ? alpha - score : 0);
    context.stats.aspiration_fail_low_distance_sum += distance;
    context.stats.aspiration_fail_low_distance_max =
        std::max(context.stats.aspiration_fail_low_distance_max, distance);
}

void record_aspiration_fail_high_distance(SearchContext& context, int score, int beta) noexcept {
    const std::uint64_t distance =
        static_cast<std::uint64_t>(score > beta ? score - beta : 0);
    context.stats.aspiration_fail_high_distance_sum += distance;
    context.stats.aspiration_fail_high_distance_max =
        std::max(context.stats.aspiration_fail_high_distance_max, distance);
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
    if (context.transpositions.store(hash, depth, score, original_alpha, beta, best_move,
                                     context.stats)) {
        ++context.stats.tt_leaf_stores;
    }
}

[[nodiscard]] bool should_solve_exact_endgame_at_root(const Board& board,
                                                      const SearchOptions& options) noexcept {
    return decide_exact_endgame_root(board, options).solve_exact;
}

[[nodiscard]] constexpr bool
should_use_dynamic_move_ordering(bool enabled, std::size_t move_count, int depth,
                                 const MoveOrderingParams& params) noexcept {
    return enabled && depth >= params.dynamic_min_depth && move_count >= params.dynamic_min_moves;
}

[[nodiscard]] int move_order_score(const SearchPosition& position, Square square, Bitboard flips,
                                   std::size_t move_count, int depth, bool dynamic_move_ordering,
                                   const MoveOrderingParams& params) noexcept {
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
    if (is_x_square_next_to_empty_corner(index, position.occupied())) {
        score -= params.dynamic_x_square_empty_corner_penalty;
    }

    const SearchPosition next = position_after_move(position, square, flips);
    const Bitboard opponent_moves = legal_moves(next);
    if ((opponent_moves & corner_squares) != 0) {
        score -= params.dynamic_opponent_corner_penalty;
    }
    score -= std::popcount(opponent_moves) * params.dynamic_opponent_mobility_penalty;

    return score;
}

[[nodiscard]] OrderedMoveIndexes
ordered_legal_move_indexes(const SearchPosition& position, Bitboard moves, int depth,
                           bool dynamic_move_ordering, const MoveOrderingParams& params) noexcept {
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
                                            dynamic_move_ordering, params),
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
                           const MoveOrderingParams& params, SearchStats& stats) noexcept {
    OrderedMoveIndexes candidates =
        ordered_legal_move_indexes(position, moves, depth, dynamic_move_ordering, params);

    if (tt_preferred_move.has_value() && (moves & tt_preferred_move->bit()) != 0 &&
        promote_preferred_move(candidates, *tt_preferred_move)) {
        ++stats.tt_move_ordering_used;
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
        hash, depth, alpha, beta, collect_tt_best_move_hint, context.stats);
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
            context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                         result.best_move, context.stats);
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
        context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                     result.best_move, context.stats);
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
        context.move_ordering_params, context.stats);
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
            break;
        }
    }

    const NodeResult result{
        .best_move = best_move,
        .score = best_score.value_or(evaluate_for_search(position, context)),
        .principal_variation = best_principal_variation,
    };
    context.transpositions.store(hash, depth, result.score, original_alpha, beta, result.best_move,
                                 context.stats);
    return result;
}

[[nodiscard]] SearchResult search_with_context(const Board& board, int depth,
                                               SearchContext& context, int alpha, int beta,
                                               std::optional<Square> root_preferred_move,
                                               PrincipalVariationHint pv_hint) noexcept {
    const SearchPosition position = SearchPosition::from_board(board);
    const NodeResult result = search_node(position, zobrist_hash(board), depth, alpha, beta,
                                          context, root_preferred_move, pv_hint, true);

    return SearchResult{
        .best_move = result.best_move,
        .score = result.score,
        .depth = depth,
        .nodes = context.stats.nodes,
        .principal_variation = principal_variation_to_vector(result.principal_variation),
        .stats = context.stats,
        .score_kind = SearchScoreKind::Heuristic,
        .used_exact_endgame = false,
        .exact_disc_margin = std::nullopt,
    };
}

[[nodiscard]] SearchResult search_with_context(const Board& board, int depth,
                                               SearchContext& context,
                                               std::optional<Square> root_preferred_move,
                                               PrincipalVariationHint pv_hint) noexcept {
    return search_with_context(board, depth, context, search_score_min, search_score_max,
                               root_preferred_move, pv_hint);
}

[[nodiscard]] constexpr int positive_aspiration_window(int window) noexcept {
    return window <= 0 ? 1 : window;
}

[[nodiscard]] constexpr int non_negative_research_limit(int researches) noexcept {
    return researches < 0 ? 0 : researches;
}

[[nodiscard]] constexpr int clamp_search_score(long long score) noexcept {
    if (score < search_score_min) {
        return search_score_min;
    }
    if (score > search_score_max) {
        return search_score_max;
    }
    return static_cast<int>(score);
}

[[nodiscard]] constexpr int aspiration_alpha(int previous_score, int window) noexcept {
    return clamp_search_score(static_cast<long long>(previous_score) - window);
}

[[nodiscard]] constexpr int aspiration_beta(int previous_score, int window) noexcept {
    return clamp_search_score(static_cast<long long>(previous_score) + window);
}

[[nodiscard]] constexpr int widened_aspiration_window(int window) noexcept {
    if (window >= search_score_max / 2) {
        return search_score_max;
    }
    return window * 2;
}

[[nodiscard]] constexpr int aspiration_initial_window(
    const SearchEngineOptions& options, std::optional<int> previous_score_delta) noexcept {
    const int base_window = positive_aspiration_window(options.aspiration_window);
    if (options.aspiration_profile == AspirationProfile::Fixed ||
        !previous_score_delta.has_value()) {
        return base_window;
    }

    long long delta = *previous_score_delta;
    if (delta < 0) {
        delta = -delta;
    }
    if (delta <= static_cast<long long>(base_window) * 2) {
        return base_window;
    }

    const long long widened = delta + base_window / 2;
    const long long max_dynamic_window = static_cast<long long>(base_window) * 2;
    return clamp_search_score(std::min(widened, max_dynamic_window));
}

[[nodiscard]] bool failed_low(const SearchResult& result, int alpha) noexcept {
    return result.score <= alpha;
}

[[nodiscard]] bool failed_high(const SearchResult& result, int beta) noexcept {
    return result.score >= beta;
}

[[nodiscard]] SearchResult search_aspirated_with_context(
    const Board& board, int depth, SearchContext& context, std::optional<Square> previous_best_move,
    PrincipalVariationHint pv_hint, int previous_score, int initial_window,
    const SearchEngineOptions& options) noexcept {
    ++context.stats.aspiration_searches;

    int window = positive_aspiration_window(initial_window);
    const int max_researches = non_negative_research_limit(options.aspiration_max_researches);
    int researches = 0;

    while (true) {
        const int alpha = aspiration_alpha(previous_score, window);
        const int beta = aspiration_beta(previous_score, window);
        const SearchResult result =
            search_with_context(board, depth, context, alpha, beta, previous_best_move, pv_hint);

        if (!failed_low(result, alpha) && !failed_high(result, beta)) {
            return result;
        }

        if (failed_low(result, alpha)) {
            ++context.stats.aspiration_fail_lows;
            record_aspiration_fail_low_distance(context, result.score, alpha);
        } else {
            ++context.stats.aspiration_fail_highs;
            record_aspiration_fail_high_distance(context, result.score, beta);
        }

        ++context.stats.aspiration_researches;
        if (researches >= max_researches) {
            ++context.stats.aspiration_full_window_fallbacks;
            return search_with_context(board, depth, context, search_score_min, search_score_max,
                                       previous_best_move, pv_hint);
        }

        ++researches;
        // Keep the policy easy to audit: re-center on the previous iteration's score
        // and double the symmetric half-window on each failed attempt.
        window = widened_aspiration_window(window);
    }
}

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

ExactEndgameRootDecision decide_exact_endgame_root(const Board& board,
                                                   const SearchOptions& options) noexcept {
    const SearchEngineOptions engine_options = engine_options_from(options);
    Board opponent_board = board;
    opponent_board.side_to_move = opponent(board.side_to_move);

    ExactEndgameRootDecision decision{
        .empty_count = empty_count(board),
        .legal_moves_current = static_cast<int>(std::popcount(legal_moves(board))),
        .legal_moves_opponent = static_cast<int>(std::popcount(legal_moves(opponent_board))),
    };

    if (engine_options.exact_endgame_empty_threshold <= 0) {
        decision.skip_reason = ExactEndgameRootSkipReason::Disabled;
        return decision;
    }

    if (engine_options.exact_endgame_root_policy == ExactEndgameRootPolicy::FixedThreshold) {
        if (decision.empty_count > engine_options.exact_endgame_empty_threshold) {
            decision.skip_reason = ExactEndgameRootSkipReason::AboveThreshold;
            return decision;
        }

        decision.solve_exact = true;
        return decision;
    }

    constexpr ExactRootPolicyParams policy = adaptive16_exact_root_policy_params;

    if (decision.empty_count <= policy.always_exact_max_empties) {
        decision.solve_exact = true;
        return decision;
    }
    if (decision.empty_count > policy.adaptive_max_empties) {
        decision.skip_reason = ExactEndgameRootSkipReason::AboveThreshold;
        return decision;
    }

    const bool root_pass = decision.legal_moves_current == 0 && pass_turn(board).has_value();
    if (root_pass) {
        decision.skip_reason = ExactEndgameRootSkipReason::AdaptiveRootPass;
        return decision;
    }
    if (decision.legal_moves_current > policy.max_legal_moves) {
        decision.skip_reason = ExactEndgameRootSkipReason::AdaptiveTooManyLegalMoves;
        return decision;
    }
    if (decision.legal_moves_opponent > policy.max_opponent_legal_moves) {
        decision.skip_reason = ExactEndgameRootSkipReason::AdaptiveOpponentTooManyLegalMoves;
        return decision;
    }

    decision.solve_exact = true;
    return decision;
}

SearchResult search(const Board& board, const SearchOptions& options) noexcept {
    if (should_solve_exact_endgame_at_root(board, options)) {
        return exact_endgame_search_result(board);
    }

    const SearchEngineOptions engine_options = engine_options_from(options);
    const SearchDiagnosticsOptions diagnostics_options = diagnostics_options_from(options);
    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{engine_options, diagnostics_options, resolve_evaluation_config(options),
                          true};
    return search_with_context(board, search_depth, context, std::nullopt,
                               PrincipalVariationHint{});
}

SearchResult search_fixed_depth(const Board& board, int depth) noexcept {
    return search(board, SearchOptions{.max_depth = depth});
}

SearchResult search_iterative(const Board& board, const SearchOptions& options) noexcept {
    if (should_solve_exact_endgame_at_root(board, options)) {
        return exact_endgame_search_result(board);
    }

    const SearchEngineOptions engine_options = engine_options_from(options);
    const SearchDiagnosticsOptions diagnostics_options = diagnostics_options_from(options);
    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{engine_options, diagnostics_options, resolve_evaluation_config(options),
                          true};

    if (search_depth == 0) {
        return search_with_context(board, 0, context, std::nullopt, PrincipalVariationHint{});
    }

    SearchResult result;
    std::optional<Square> previous_best_move;
    std::optional<int> previous_score;
    std::optional<int> previous_score_delta;
    PrincipalVariation previous_principal_variation;
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

    return result;
}

} // namespace othello
