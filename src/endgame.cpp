#include "endgame_last_n.hpp"
#include "endgame_ordering.hpp"
#include "hash_update.hpp"
#include "search_common.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <othello/endgame.hpp>
#include <othello/hash.hpp>
#include <othello/rules.hpp>

namespace othello {
namespace {

using endgame_detail::EndgameMoveOrderingPolicy;
using endgame_detail::ExactEndgameContext;
using endgame_detail::last_n_specialized_empties;
using endgame_detail::ordered_legal_move_indexes;
using endgame_detail::OrderedMoveIndexes;
using endgame_detail::solve_last_n_dispatch;
using hash_detail::hash_after_pass;
using search_detail::empty_count;
using search_detail::is_better_best_move;
using search_detail::legal_moves;
using search_detail::NodeResult;
using search_detail::position_after_pass;
using search_detail::principal_variation_to_vector;
using search_detail::principal_variation_with_move;
using search_detail::PrincipalVariation;
using search_detail::score_for_player;
using search_detail::SearchPosition;

// Final disc margins are always in [-64, 64]; this leaves a simple generous alpha-beta window.
constexpr int exact_score_min = -1'000;
constexpr int exact_score_max = 1'000;
constexpr int final_disc_margin_max = 64;

struct EndgameSearchPolicy {
    int root_pvs_min_empties = 16;
    int full_parity_max_root_empties = 14;
    int reduced_parity_max_current_empties = 4;
};

constexpr EndgameSearchPolicy default_endgame_search_policy{};

[[nodiscard]] bool should_use_root_pvs(int empties, std::size_t legal_move_count,
                                       const EndgameSearchPolicy& policy) noexcept {
    return empties >= policy.root_pvs_min_empties && legal_move_count > 1;
}

[[nodiscard]] EndgameMoveOrderingPolicy
empty_region_parity_ordering_policy(int root_empties, int empties,
                                    const EndgameSearchPolicy& search_policy) noexcept {
    EndgameMoveOrderingPolicy policy;
    if (root_empties <= search_policy.full_parity_max_root_empties) {
        policy.use_empty_region_parity = true;
        return policy;
    }

    // Broader activation made 16/18-empty roots noisier; keep only the last ordered layer weakly
    // parity-aware. The 0-3 empty tail uses the specialized solver instead of this ordering path.
    policy.use_empty_region_parity = empties <= search_policy.reduced_parity_max_current_empties;
    policy.params.singleton_region_bonus = 4'000;
    policy.params.odd_region_bonus = 1'000;
    policy.params.even_region_penalty = 0;
    return policy;
}

[[nodiscard]] int root_candidate_required_score(int best_score, Square candidate,
                                                Square best_move) noexcept {
    return candidate.index() < best_move.index() ? best_score : best_score + 1;
}

[[nodiscard]] bool root_scout_rejects_candidate(const NodeResult& scout_result,
                                                int required_score) noexcept {
    return -scout_result.score < required_score;
}

[[nodiscard]] NodeResult solve_root_candidate_full(const SearchPosition& position, ZobristHash hash,
                                                   ExactEndgameContext& context) noexcept;

[[nodiscard]] bool root_candidate_needs_full_search(const SearchPosition& position,
                                                    ZobristHash hash,
                                                    int required_score,
                                                    ExactEndgameContext& context) noexcept;

// A null-window scout can reject equality with alpha. Use a full search when equality could change
// the deterministic lower-index tie-break for this node's stored best move and PV.
[[nodiscard]] bool interior_candidate_needs_full_search(Square candidate,
                                                        const std::optional<int>& best_score,
                                                        const std::optional<Square>& best_move,
                                                        int alpha) noexcept {
    return best_score.has_value() && best_move.has_value() &&
           candidate.index() < best_move->index() && *best_score <= alpha;
}

// Exact negamax searches to game-over leaves only.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult solve_node(const SearchPosition& position, ZobristHash hash, int alpha,
                                    int beta, ExactEndgameContext& context,
                                    bool is_root) noexcept {
    const int empties = empty_count(position);
    if (empties <= last_n_specialized_empties) {
        return solve_last_n_dispatch(position, alpha, beta, context, empties);
    }

    ++context.stats.nodes;
    const int original_alpha = alpha;

    const auto cached =
        context.transpositions.lookup(hash, empties, alpha, beta, true, context.stats);
    if (cached.cutoff.has_value()) {
        return *cached.cutoff;
    }

    const Bitboard moves = legal_moves(position);
    if (moves == 0) {
        const SearchPosition next = position_after_pass(position);
        if (legal_moves(next) == 0) {
            const NodeResult result{.score = score_for_player(position)};
            context.transpositions.store(hash, empties, result.score, original_alpha, beta,
                                         result.best_move, context.stats);
            return result;
        }

        // At a root pass, the after-pass child is still the first real move choice in the public
        // PV. Treat only that child as a root traversal so root PVS can help without exposing a
        // fake pass move or changing best_move=nullopt semantics.
        const ZobristHash next_hash = hash_after_pass(hash, position.side_to_move);
#ifndef NDEBUG
        assert(next_hash == zobrist_hash(next.to_board()));
#endif
        constexpr EndgameSearchPolicy search_policy = default_endgame_search_policy;
        const bool use_after_pass_root_pvs =
            is_root && empty_count(next) >= search_policy.root_pvs_min_empties;
        const NodeResult child =
            solve_node(next, next_hash, -beta, -alpha, context, use_after_pass_root_pvs);
        const NodeResult result{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
        context.transpositions.store(hash, empties, result.score, original_alpha, beta,
                                     result.best_move, context.stats);
        return result;
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    const OrderedMoveIndexes ordered_moves = ordered_legal_move_indexes(
        position, hash, moves, cached.best_move_hint, context.stats,
        empty_region_parity_ordering_policy(context.root_empties, empties,
                                            default_endgame_search_policy));
    const bool use_root_pvs =
        is_root && should_use_root_pvs(empties, ordered_moves.size, default_endgame_search_policy);
    for (std::size_t move = 0; move < ordered_moves.size; ++move) {
        const OrderedMoveIndexes::Move& ordered_move = ordered_moves.moves[move];
        const std::optional<Square> square = Square::from_index(ordered_move.index);
        if (!square.has_value()) {
            continue;
        }

        // Root results are public API, so compare exact root candidate scores instead of
        // alpha-beta bounds. Root PVS only uses a scout to reject candidates that provably cannot
        // beat the current exact best; candidates that may become best are re-searched full-window.
        const ZobristHash next_hash = ordered_move.hash;
        std::optional<NodeResult> child;
        if (is_root) {
            if (!use_root_pvs || !best_score.has_value() || !best_move.has_value()) {
                child = solve_root_candidate_full(ordered_move.next, next_hash, context);
            } else {
                const int required_score =
                    root_candidate_required_score(*best_score, *square, *best_move);
                if (required_score > final_disc_margin_max) {
                    continue;
                }
                if (!root_candidate_needs_full_search(ordered_move.next, next_hash, required_score,
                                                      context)) {
                    continue;
                }
                child = solve_root_candidate_full(ordered_move.next, next_hash, context);
            }
        } else if (move == 0 ||
                   interior_candidate_needs_full_search(*square, best_score, best_move, alpha)) {
            child = solve_node(ordered_move.next, next_hash, -beta, -alpha, context, false);
        } else {
            // Interior PVS tests whether this move can improve alpha. A root-side scout window
            // [alpha, alpha + 1) becomes child-side [-(alpha + 1), -alpha] through negamax.
            child = solve_node(ordered_move.next, next_hash, -(alpha + 1), -alpha, context, false);
            const int scout_score = -child->score;
            if (scout_score > alpha && scout_score < beta) {
                child = solve_node(ordered_move.next, next_hash, -beta, -alpha, context, false);
            }
        }
        const int candidate_score = -child->score;
        if (is_better_best_move(candidate_score, *square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(*square, child->principal_variation);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            break;
        }
    }

    const NodeResult result{
        .best_move = best_move,
        .score = best_score.value_or(score_for_player(position)),
        .principal_variation = best_principal_variation,
    };
    context.transpositions.store(hash, empties, result.score, original_alpha, beta,
                                 result.best_move, context.stats);
    return result;
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult solve_root_candidate_full(const SearchPosition& position, ZobristHash hash,
                                                   ExactEndgameContext& context) noexcept {
    return solve_node(position, hash, exact_score_min, exact_score_max, context, false);
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] bool root_candidate_needs_full_search(const SearchPosition& position,
                                                    ZobristHash hash,
                                                    int required_score,
                                                    ExactEndgameContext& context) noexcept {
    // Root candidate score is -child_score. The null window [a, a + 1), where
    // a = -required_score, proves rejection when child_score > a, because then
    // root_score < required_score. Equality is handled by required_score:
    // lower-index candidates need only tie, higher-index candidates must beat.
    const int scout_alpha = -required_score;
    const NodeResult scout =
        solve_node(position, hash, scout_alpha, scout_alpha + 1, context, false);
    return !root_scout_rejects_candidate(scout, required_score);
}

} // namespace

ExactEndgameResult solve_exact_endgame(const Board& board,
                                       const ExactEndgameOptions& options) noexcept {
    ExactEndgameContext context{empty_count(board), options};
    const SearchPosition position = SearchPosition::from_board(board);
    const NodeResult result =
        solve_node(position, zobrist_hash(board), exact_score_min, exact_score_max, context, true);

    return ExactEndgameResult{
        .best_move = result.best_move,
        .disc_margin = result.score,
        .empties = empty_count(board),
        .nodes = context.stats.nodes,
        .principal_variation = principal_variation_to_vector(result.principal_variation),
        .stats = context.stats,
    };
}

ExactEndgameResult solve_exact_endgame(const Board& board) noexcept {
    return solve_exact_endgame(board, ExactEndgameOptions{});
}

} // namespace othello
