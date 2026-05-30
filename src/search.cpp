#include "hash_detail.hpp"
#include "search_common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
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
    return evaluation_config_for_preset(options.evaluation_preset);
}

namespace {

using search_detail::board_after_move;
using search_detail::corner_squares;
using search_detail::empty_count;
using search_detail::is_better_best_move;
using search_detail::is_corner;
using search_detail::is_edge;
using search_detail::is_x_square;
using search_detail::is_x_square_next_to_empty_corner;
using search_detail::node_result_from_transposition_entry;
using search_detail::NodeResult;
using search_detail::principal_variation_from_vector;
using search_detail::principal_variation_to_vector;
using search_detail::principal_variation_with_move;
using search_detail::PrincipalVariation;

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

enum class TranspositionBound {
    Exact,
    Lower,
    Upper,
};

struct TranspositionEntry {
    ZobristHash hash = 0;
    int depth = -1;
    int score = 0;
    int best_move_index = -1;
    TranspositionBound bound = TranspositionBound::Exact;
    bool occupied = false;
};

struct TranspositionLookup {
    std::optional<NodeResult> cutoff;
    std::optional<Square> best_move_hint;
};

class TranspositionTable {
public:
    explicit TranspositionTable(const SearchOptions& options) noexcept
        : bucket_count_{normalized_bucket_count(options)},
          buckets_{bucket_count_ == 0 ? nullptr
                                      : new (std::nothrow) Bucket[bucket_count_]} {}

    [[nodiscard]] TranspositionLookup lookup(ZobristHash hash, int depth, int alpha, int beta,
                                             bool collect_best_move_hint,
                                             SearchStats& stats) const noexcept {
        if (buckets_ == nullptr) {
            return {};
        }

        ++stats.tt_lookups;
        const Bucket& bucket = buckets_[bucket_index(hash)];
        const TranspositionEntry* matching_entry = nullptr;
        for (const TranspositionEntry& entry : bucket.entries) {
            if (entry.occupied && entry.hash == hash) {
                matching_entry = &entry;
                break;
            }
        }

        // The same depth guard is used for cutoff and ordering hints. A shallower
        // hint would be correctness-safe, but can perturb iterative search behavior.
        if (matching_entry == nullptr || matching_entry->depth < depth) {
            if (collect_best_move_hint) {
                ++stats.tt_move_ordering_probes;
            }
            return {};
        }

        const TranspositionEntry& entry = *matching_entry;
        if (entry.bound == TranspositionBound::Exact) {
            ++stats.tt_hits;
            ++stats.tt_exact_hits;
            return TranspositionLookup{.cutoff = node_result_from_entry(entry)};
        }
        if (entry.bound == TranspositionBound::Lower && entry.score >= beta) {
            ++stats.tt_hits;
            ++stats.tt_lower_hits;
            return TranspositionLookup{.cutoff = node_result_from_entry(entry)};
        }
        if (entry.bound == TranspositionBound::Upper && entry.score <= alpha) {
            ++stats.tt_hits;
            ++stats.tt_upper_hits;
            return TranspositionLookup{.cutoff = node_result_from_entry(entry)};
        }

        if (!collect_best_move_hint) {
            return {};
        }

        ++stats.tt_move_ordering_probes;
        return TranspositionLookup{.best_move_hint = best_move_hint_from_entry(entry, stats)};
    }

    void store(ZobristHash hash, int depth, int score, int original_alpha, int beta,
               const std::optional<Square>& best_move, SearchStats& stats) noexcept {
        if (buckets_ == nullptr) {
            return;
        }

        TranspositionEntry* entry = replacement_entry(buckets_[bucket_index(hash)], hash, depth);
        if (entry == nullptr) {
            ++stats.tt_rejected_stores;
            return;
        }

        TranspositionBound bound = TranspositionBound::Exact;
        if (score <= original_alpha) {
            bound = TranspositionBound::Upper;
        } else if (score >= beta) {
            bound = TranspositionBound::Lower;
        }

        ++stats.tt_stores;
        if (entry->occupied) {
            ++stats.tt_overwrites;
            if (entry->hash != hash) {
                ++stats.tt_collisions;
            }
        }

        *entry = TranspositionEntry{
            .hash = hash,
            .depth = depth,
            .score = score,
            .best_move_index = best_move.has_value() ? best_move->index() : -1,
            .bound = bound,
            .occupied = true,
        };
    }

private:
    static constexpr std::size_t bucket_width = 4;
    static constexpr std::size_t default_entry_count = 1 << 18;

    struct Bucket {
        std::array<TranspositionEntry, bucket_width> entries{};
    };

    // SearchOptions requests approximate entry count, not bucket count. Bucket indexing uses a
    // bit mask, so we round the requested bucket count up to a power of two. Very small positive
    // requests allocate one full bucket; oversized requests fall back to the default capacity.
    [[nodiscard]] static constexpr std::size_t
    normalized_bucket_count(const SearchOptions& options) noexcept {
        if (!options.use_transposition_table || options.transposition_table_entries == 0) {
            return 0;
        }

        const std::size_t requested = options.transposition_table_entries;
        constexpr std::size_t max_power_of_two = std::size_t{1}
                                                 << (std::numeric_limits<std::size_t>::digits - 1);
        if (requested > max_power_of_two / bucket_width) {
            return default_entry_count / bucket_width;
        }

        const std::size_t requested_buckets =
            std::max(std::size_t{1}, (requested + bucket_width - 1) / bucket_width);
        if (std::has_single_bit(requested_buckets)) {
            return requested_buckets;
        }

        std::size_t bucket_count = 1;
        while (bucket_count < requested_buckets) {
            bucket_count <<= 1;
        }
        return bucket_count;
    }

    std::size_t bucket_count_ = 0;
    std::unique_ptr<Bucket[]> buckets_; // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                        // modernize-avoid-c-arrays)

    [[nodiscard]] std::size_t bucket_index(ZobristHash hash) const noexcept {
        return static_cast<std::size_t>(hash) & (bucket_count_ - 1);
    }

    [[nodiscard]] static TranspositionEntry*
    replacement_entry(Bucket& bucket, ZobristHash hash, int depth) noexcept {
        TranspositionEntry* empty_slot = nullptr;
        TranspositionEntry* shallowest = &bucket.entries.front();
        for (TranspositionEntry& entry : bucket.entries) {
            if (entry.occupied && entry.hash == hash) {
                return &entry;
            }
            if (!entry.occupied) {
                if (empty_slot == nullptr) {
                    empty_slot = &entry;
                }
                continue;
            }
            if (entry.depth < shallowest->depth) {
                shallowest = &entry;
            }
        }

        if (empty_slot != nullptr) {
            return empty_slot;
        }

        // Keep deeper entries stable under pressure. Equal-depth stores replace the first
        // shallowest slot deterministically; strictly shallower stores are rejected.
        if (depth < shallowest->depth) {
            return nullptr;
        }
        return shallowest;
    }

    [[nodiscard]] static NodeResult
    node_result_from_entry(const TranspositionEntry& entry) noexcept {
        return node_result_from_transposition_entry(entry.score, entry.best_move_index);
    }

    [[nodiscard]] static std::optional<Square>
    best_move_hint_from_entry(const TranspositionEntry& entry, SearchStats& stats) noexcept {
        if (entry.best_move_index < Square::min_index ||
            entry.best_move_index > Square::max_index) {
            return std::nullopt;
        }

        std::optional<Square> best_move = Square::from_index(entry.best_move_index);
        if (!best_move.has_value()) {
            return std::nullopt;
        }

        ++stats.tt_move_ordering_hits;
        return best_move;
    }
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
    explicit SearchContext(const SearchOptions& options, bool enable_dynamic_move_ordering) noexcept
        : transpositions{options}, dynamic_move_ordering{enable_dynamic_move_ordering},
          use_pvs{options.use_pvs}, evaluation_config{resolve_evaluation_config(options)} {}

    SearchStats stats;
    TranspositionTable transpositions;
    MoveOrderingParams move_ordering_params = default_move_ordering_params;
    bool dynamic_move_ordering = false;
    bool use_pvs = false;
    EvaluationConfig evaluation_config = default_evaluation_config();
};

constexpr int search_score_min = -1'000'000'000;
constexpr int search_score_max = 1'000'000'000;
// Keep exact root endgame scores comparable with evaluate_basic terminal scores.
// If the terminal score weight in evaluation.cpp changes, update this scale too.
constexpr int exact_endgame_score_scale = 1'000;

[[nodiscard]] int evaluate_for_search(const Board& board, SearchContext& context) noexcept {
    ++context.stats.eval_calls;
    return evaluate_with_config(board, board.side_to_move, context.evaluation_config);
}

[[nodiscard]] bool should_solve_exact_endgame_at_root(const Board& board,
                                                      const SearchOptions& options) noexcept {
    return options.exact_endgame_empty_threshold > 0 &&
           empty_count(board) <= options.exact_endgame_empty_threshold;
}

[[nodiscard]] constexpr bool
should_use_dynamic_move_ordering(bool enabled, std::size_t move_count, int depth,
                                 const MoveOrderingParams& params) noexcept {
    return enabled && depth >= params.dynamic_min_depth && move_count >= params.dynamic_min_moves;
}

[[nodiscard]] int move_order_score(const Board& board, Square square, Bitboard flips,
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
    if (is_x_square_next_to_empty_corner(index, board.occupied())) {
        score -= params.dynamic_x_square_empty_corner_penalty;
    }

    const Board next = board_after_move(board, square, flips);
    const Bitboard opponent_moves = legal_moves(next);
    if ((opponent_moves & corner_squares) != 0) {
        score -= params.dynamic_opponent_corner_penalty;
    }
    score -= std::popcount(opponent_moves) * params.dynamic_opponent_mobility_penalty;

    return score;
}

[[nodiscard]] OrderedMoveIndexes
ordered_legal_move_indexes(const Board& board, Bitboard moves, int depth,
                           bool dynamic_move_ordering, const MoveOrderingParams& params) noexcept {
    OrderedMoveIndexes candidates;
    const auto move_count = static_cast<std::size_t>(std::popcount(moves));

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Bitboard move_bit = Bitboard{1} << index;
        if ((moves & move_bit) != 0) {
            const std::optional<Square> square = Square::from_index(index);
            if (!square.has_value()) {
                continue;
            }

            const Bitboard flips = flips_for_move(board, *square);
            if (flips == 0) {
                continue;
            }

            candidates.moves[candidates.size] = OrderedMoveIndexes::Move{
                .index = index,
                .flips = flips,
                .order_score = move_order_score(board, *square, flips, move_count, depth,
                                                dynamic_move_ordering, params),
            };
            ++candidates.size;
        }
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
ordered_legal_move_indexes(const Board& board, Bitboard moves, int depth,
                           std::optional<Square> preferred_move,
                           std::optional<Square> tt_preferred_move, bool dynamic_move_ordering,
                           const MoveOrderingParams& params, SearchStats& stats) noexcept {
    OrderedMoveIndexes candidates =
        ordered_legal_move_indexes(board, moves, depth, dynamic_move_ordering, params);

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

[[nodiscard]] ZobristHash hash_after_pass(ZobristHash hash, Side side_to_move) noexcept {
    hash ^= detail::zobrist_side_hash(side_to_move);
    hash ^= detail::zobrist_side_hash(opponent(side_to_move));
    return hash;
}

[[nodiscard]] ZobristHash hash_after_move(ZobristHash hash, Side side_to_move, Square square,
                                          Bitboard flips) noexcept {
    const Side next_side = opponent(side_to_move);

    hash ^= detail::zobrist_side_hash(side_to_move);
    hash ^= detail::zobrist_side_hash(next_side);
    hash ^= detail::zobrist_piece_hash(side_to_move, square.index());

    while (flips != 0) {
        const int square_index = std::countr_zero(flips);
        hash ^= detail::zobrist_piece_hash(next_side, square_index);
        hash ^= detail::zobrist_piece_hash(side_to_move, square_index);
        flips &= flips - 1;
    }

    return hash;
}

// Fixed-depth negamax is easiest to audit as direct recursion at this stage.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult search_node(const Board& board, ZobristHash hash, int depth, int alpha,
                                     int beta, SearchContext& context,
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
        const NodeResult result{.score = evaluate_for_search(board, context)};
        context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                     result.best_move, context.stats);
        return result;
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const std::optional<Board> next = pass_turn(board);
        if (!next.has_value()) {
            ++context.stats.game_over_nodes;
            const NodeResult result{.score = evaluate_for_search(board, context)};
            context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                         result.best_move, context.stats);
            return result;
        }

        ++context.stats.pass_nodes;
        const ZobristHash next_hash = hash_after_pass(hash, board.side_to_move);
        assert(next_hash == zobrist_hash(*next));

        const NodeResult child = search_node(*next, next_hash, depth - 1, -beta, -alpha, context,
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
        board, moves, depth, preferred_move, cached.best_move_hint, use_dynamic_ordering,
        context.move_ordering_params, context.stats);
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

        const Board next = board_after_move(board, *square, flips);
        const ZobristHash next_hash = hash_after_move(hash, board.side_to_move, *square, flips);
        assert(next_hash == zobrist_hash(next));
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
        .score = best_score.value_or(evaluate_for_search(board, context)),
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
    const NodeResult result = search_node(board, zobrist_hash(board), depth, alpha, beta, context,
                                          root_preferred_move, pv_hint, true);

    return SearchResult{
        .best_move = result.best_move,
        .score = result.score,
        .depth = depth,
        .nodes = context.stats.nodes,
        .principal_variation = principal_variation_to_vector(result.principal_variation),
        .stats = context.stats,
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

[[nodiscard]] bool failed_low(const SearchResult& result, int alpha) noexcept {
    return result.score <= alpha;
}

[[nodiscard]] bool failed_high(const SearchResult& result, int beta) noexcept {
    return result.score >= beta;
}

[[nodiscard]] SearchResult search_aspirated_with_context(
    const Board& board, int depth, SearchContext& context, std::optional<Square> previous_best_move,
    PrincipalVariationHint pv_hint, int previous_score, const SearchOptions& options) noexcept {
    ++context.stats.aspiration_searches;

    int window = positive_aspiration_window(options.aspiration_window);
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
        } else {
            ++context.stats.aspiration_fail_highs;
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
    };

    return SearchResult{
        .best_move = exact.best_move,
        .score = exact.disc_margin * exact_endgame_score_scale,
        .depth = exact.empties,
        .nodes = exact.nodes,
        .principal_variation = std::move(exact.principal_variation),
        .stats = stats,
    };
}

} // namespace

SearchResult search(const Board& board, const SearchOptions& options) noexcept {
    if (should_solve_exact_endgame_at_root(board, options)) {
        return exact_endgame_search_result(board);
    }

    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{options, true};
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

    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{options, true};

    if (search_depth == 0) {
        return search_with_context(board, 0, context, std::nullopt, PrincipalVariationHint{});
    }

    SearchResult result;
    std::optional<Square> previous_best_move;
    std::optional<int> previous_score;
    PrincipalVariation previous_principal_variation;
    for (int depth = 1; depth <= search_depth; ++depth) {
        const PrincipalVariationHint pv_hint{
            .principal_variation =
                previous_principal_variation.size == 0 ? nullptr : &previous_principal_variation,
            .index = 0,
            .matches_prefix = previous_principal_variation.size > 0,
        };
        if (options.use_aspiration_window && previous_score.has_value()) {
            result = search_aspirated_with_context(board, depth, context, previous_best_move,
                                                   pv_hint, *previous_score, options);
        } else {
            result = search_with_context(board, depth, context, previous_best_move, pv_hint);
        }
        previous_best_move = result.best_move;
        previous_score = result.score;
        previous_principal_variation = principal_variation_from_vector(result.principal_variation);
    }

    return result;
}

} // namespace othello
