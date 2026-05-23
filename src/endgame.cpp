#include "hash_detail.hpp"
#include "search_common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <othello/endgame.hpp>
#include <othello/hash.hpp>
#include <othello/rules.hpp>
#include <vector>

namespace othello {
namespace {

using search_detail::board_after_move;
using search_detail::corner_squares;
using search_detail::empty_count;
using search_detail::is_better_best_move;
using search_detail::is_corner;
using search_detail::is_edge;
using search_detail::is_x_square_next_to_empty_corner;
using search_detail::node_result_from_transposition_entry;
using search_detail::NodeResult;
using search_detail::principal_variation_to_vector;
using search_detail::principal_variation_with_move;
using search_detail::PrincipalVariation;

enum class ExactTranspositionBound : std::uint8_t {
    Exact,
    Lower,
    Upper,
};

struct ExactTranspositionEntry {
    ZobristHash hash = 0;
    int score = 0;
    std::int8_t empties = -1;
    std::int8_t best_move_index = -1;
    ExactTranspositionBound bound = ExactTranspositionBound::Exact;
    bool occupied = false;
};

struct OrderedMoveIndexes {
    struct Move {
        int index = 0;
        int order_score = 0;
        ZobristHash hash = 0;
        Board next;
    };

    std::array<Move, 64> moves{};
    std::size_t size = 0;
};

class ExactTranspositionTable {
public:
    explicit ExactTranspositionTable(int root_empties) noexcept
        : entry_count_{entry_count_for_empties(root_empties)},
          entries_{entry_count_ == 0 ? nullptr
                                     : new (std::nothrow) ExactTranspositionEntry[entry_count_]} {}

    [[nodiscard]] std::optional<NodeResult> lookup(ZobristHash hash, int empties, int alpha,
                                                   int beta,
                                                   ExactEndgameStats& stats) const noexcept {
        if (entries_ == nullptr) {
            return std::nullopt;
        }

        ++stats.tt_lookups;
        const ExactTranspositionEntry& entry = entries_[entry_index(hash)];
        if (!entry.occupied || entry.hash != hash || entry.empties < empties) {
            return std::nullopt;
        }

        if (entry.bound == ExactTranspositionBound::Exact) {
            record_hit(stats, entry.bound);
            return node_result_from_entry(entry);
        }
        if (entry.bound == ExactTranspositionBound::Lower && entry.score >= beta) {
            record_hit(stats, entry.bound);
            return node_result_from_entry(entry);
        }
        if (entry.bound == ExactTranspositionBound::Upper && entry.score <= alpha) {
            record_hit(stats, entry.bound);
            return node_result_from_entry(entry);
        }

        return std::nullopt;
    }

    void store(ZobristHash hash, int empties, int score, int original_alpha, int beta,
               const std::optional<Square>& best_move, ExactEndgameStats& stats) noexcept {
        if (entries_ == nullptr) {
            return;
        }

        ExactTranspositionEntry& entry = entries_[entry_index(hash)];
        if (entry.occupied && entry.hash != hash && empties < entry.empties) {
            ++stats.tt_rejected_stores;
            return;
        }

        const bool overwrites_entry = entry.occupied;
        const bool collides_with_different_hash = entry.occupied && entry.hash != hash;
        ExactTranspositionBound bound = ExactTranspositionBound::Exact;
        if (score <= original_alpha) {
            bound = ExactTranspositionBound::Upper;
        } else if (score >= beta) {
            bound = ExactTranspositionBound::Lower;
        }

        entry = ExactTranspositionEntry{
            .hash = hash,
            .score = score,
            .empties = static_cast<std::int8_t>(empties),
            .best_move_index =
                static_cast<std::int8_t>(best_move.has_value() ? best_move->index() : -1),
            .bound = bound,
            .occupied = true,
        };
        ++stats.tt_stores;
        if (overwrites_entry) {
            ++stats.tt_overwrites;
        }
        if (collides_with_different_hash) {
            ++stats.tt_collisions;
        }
    }

private:
    [[nodiscard]] static constexpr std::size_t entry_count_for_empties(int empties) noexcept {
        if (empties <= 8) {
            return 0;
        }
        if (empties <= 10) {
            return 1 << 14;
        }
        if (empties <= 12) {
            return 1 << 16;
        }
        return 1 << 20;
    }

    std::size_t entry_count_ = 0;
    std::unique_ptr<ExactTranspositionEntry[]> entries_; // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                         // modernize-avoid-c-arrays)

    [[nodiscard]] std::size_t entry_index(ZobristHash hash) const noexcept {
        return static_cast<std::size_t>(hash) & (entry_count_ - 1);
    }

    [[nodiscard]] static NodeResult
    node_result_from_entry(const ExactTranspositionEntry& entry) noexcept {
        return node_result_from_transposition_entry(entry.score,
                                                    static_cast<int>(entry.best_move_index));
    }

    static void record_hit(ExactEndgameStats& stats, ExactTranspositionBound bound) noexcept {
        ++stats.tt_hits;
        switch (bound) {
        case ExactTranspositionBound::Exact:
            ++stats.tt_exact_hits;
            break;
        case ExactTranspositionBound::Lower:
            ++stats.tt_lower_hits;
            break;
        case ExactTranspositionBound::Upper:
            ++stats.tt_upper_hits;
            break;
        }
    }
};

struct ExactEndgameContext {
    explicit ExactEndgameContext(int root_empties) noexcept : transpositions{root_empties} {}

    ExactEndgameStats stats;
    ExactTranspositionTable transpositions;
};

struct EndgameMoveOrderingParams {
    int corner_bonus = 100'000;
    int edge_bonus = 1'000;
    int x_square_empty_corner_penalty = 30'000;
    int opponent_corner_penalty = 80'000;
    int opponent_mobility_penalty = 800;
    int opponent_pass_bonus = 30'000;
};

// Final disc margins are always in [-64, 64]; this leaves a simple generous alpha-beta window.
constexpr int exact_score_min = -1'000;
constexpr int exact_score_max = 1'000;
constexpr int final_disc_margin_max = 64;
constexpr int root_pvs_min_empties = 16;
// The final 0-3 empties avoid TT lookup/store and full move ordering overhead.
// Four-empty specialization was benchmarked separately and rejected because it helped 20-empty
// tail latency a bit more but made 14-empty tail behavior noisier.
constexpr int last_n_specialized_empties = 3;
constexpr EndgameMoveOrderingParams default_move_ordering_params{};

[[nodiscard]] ZobristHash hash_after_pass(ZobristHash hash, const Board& board) noexcept {
    hash ^= detail::zobrist_side_hash(board.side_to_move);
    hash ^= detail::zobrist_side_hash(opponent(board.side_to_move));
    return hash;
}

[[nodiscard]] ZobristHash hash_after_move(ZobristHash hash, const Board& board, Square square,
                                          Bitboard flips) noexcept {
    const Side side = board.side_to_move;
    const Side other = opponent(side);
    hash ^= detail::zobrist_side_hash(side);
    hash ^= detail::zobrist_side_hash(other);
    hash ^= detail::zobrist_piece_hash(side, square.index());

    while (flips != 0) {
        const int index = std::countr_zero(flips);
        flips &= flips - 1;
        hash ^= detail::zobrist_piece_hash(other, index);
        hash ^= detail::zobrist_piece_hash(side, index);
    }

    return hash;
}

[[nodiscard]] int move_order_score(const Board& board, int index, const Board& next,
                                   const EndgameMoveOrderingParams& params) noexcept {
    int score = 0;
    if (is_corner(index)) {
        score += params.corner_bonus;
    }
    if (is_edge(index)) {
        score += params.edge_bonus;
    }
    if (is_x_square_next_to_empty_corner(index, board.occupied())) {
        score -= params.x_square_empty_corner_penalty;
    }

    const Bitboard opponent_moves = legal_moves(next);
    if ((opponent_moves & corner_squares) != 0) {
        score -= params.opponent_corner_penalty;
    }
    // Keep the count as an int to match the rest of the search code's scoring arithmetic.
    // NOLINTNEXTLINE(readability-redundant-casting)
    const int opponent_move_count = static_cast<int>(std::popcount(opponent_moves));
    score -= opponent_move_count * params.opponent_mobility_penalty;

    if (opponent_moves == 0 && pass_turn(next).has_value()) {
        score += params.opponent_pass_bonus;
    }

    return score;
}

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(const Board& board, ZobristHash hash,
                                                            Bitboard moves) noexcept {
    OrderedMoveIndexes result;

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
            const Board next = board_after_move(board, *square, flips);
            const ZobristHash next_hash = hash_after_move(hash, board, *square, flips);
#ifndef NDEBUG
            assert(next_hash == zobrist_hash(next));
#endif

            result.moves[result.size] = OrderedMoveIndexes::Move{
                .index = index,
                .order_score = move_order_score(board, index, next, default_move_ordering_params),
                .hash = next_hash,
                .next = next,
            };
            ++result.size;
        }
    }

    std::sort(result.moves.begin(), result.moves.begin() + result.size,
              [](const OrderedMoveIndexes::Move& lhs, const OrderedMoveIndexes::Move& rhs) {
                  if (lhs.order_score != rhs.order_score) {
                      return lhs.order_score > rhs.order_score;
                  }
                  return lhs.index < rhs.index;
              });

    return result;
}

[[nodiscard]] bool should_use_root_pvs(int empties, std::size_t legal_move_count) noexcept {
    return empties >= root_pvs_min_empties && legal_move_count > 1;
}

[[nodiscard]] int root_candidate_required_score(int best_score, Square candidate,
                                                Square best_move) noexcept {
    return candidate.index() < best_move.index() ? best_score : best_score + 1;
}

[[nodiscard]] bool root_scout_rejects_candidate(const NodeResult& scout_result,
                                                int required_score) noexcept {
    return -scout_result.score < required_score;
}

[[nodiscard]] NodeResult solve_root_candidate_full(const Board& board, ZobristHash hash,
                                                   ExactEndgameContext& context) noexcept;

[[nodiscard]] bool root_candidate_needs_full_search(const Board& board, ZobristHash hash,
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

[[nodiscard]] NodeResult solve_last_0(const Board& board, ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    return NodeResult{.score = score(board, board.side_to_move)};
}

[[nodiscard]] NodeResult solve_last_n_node(const Board& board, int alpha, int beta,
                                           ExactEndgameContext& context) noexcept;

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult solve_last_n_dispatch(const Board& board, int alpha, int beta,
                                               ExactEndgameContext& context, int empties) noexcept {
    switch (empties) {
    case 0:
        return solve_last_0(board, context);
    case 1:
    case 2:
    case 3:
        return solve_last_n_node(board, alpha, beta, context);
    default:
        assert(false);
        return NodeResult{.score = score(board, board.side_to_move)};
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult solve_last_n_node(const Board& board, int alpha, int beta,
                                           ExactEndgameContext& context) noexcept {
    ++context.stats.nodes;
    const int empties = empty_count(board);
    if (empties == 0 || is_game_over(board)) {
        return NodeResult{.score = score(board, board.side_to_move)};
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const std::optional<Board> next = pass_turn(board);
        if (!next.has_value()) {
            return NodeResult{.score = score(board, board.side_to_move)};
        }

        const NodeResult child = solve_last_n_dispatch(*next, -beta, -alpha, context, empties);
        return NodeResult{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Bitboard move_bit = Bitboard{1} << index;
        if ((moves & move_bit) == 0) {
            continue;
        }

        const std::optional<Square> square = Square::from_index(index);
        if (!square.has_value()) {
            continue;
        }

        const Bitboard flips = flips_for_move(board, *square);
        if (flips == 0) {
            continue;
        }

        const Board next = board_after_move(board, *square, flips);
        const NodeResult child = solve_last_n_dispatch(next, -beta, -alpha, context, empties - 1);
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, *square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(*square, child.principal_variation);
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            break;
        }
    }

    return NodeResult{
        .best_move = best_move,
        .score = best_score.value_or(score(board, board.side_to_move)),
        .principal_variation = best_principal_variation,
    };
}

// Exact negamax searches to game-over leaves only.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult solve_node(const Board& board, ZobristHash hash, int alpha, int beta,
                                    ExactEndgameContext& context, bool is_root) noexcept {
    const int empties = empty_count(board);
    if (empties <= last_n_specialized_empties) {
        return solve_last_n_dispatch(board, alpha, beta, context, empties);
    }

    ++context.stats.nodes;
    const int original_alpha = alpha;

    const std::optional<NodeResult> cached =
        context.transpositions.lookup(hash, empties, alpha, beta, context.stats);
    if (cached.has_value()) {
        return *cached;
    }

    if (is_game_over(board)) {
        const NodeResult result{.score = score(board, board.side_to_move)};
        context.transpositions.store(hash, empties, result.score, original_alpha, beta,
                                     result.best_move, context.stats);
        return result;
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const std::optional<Board> next = pass_turn(board);
        if (!next.has_value()) {
            const NodeResult result{.score = score(board, board.side_to_move)};
            context.transpositions.store(hash, empties, result.score, original_alpha, beta,
                                         result.best_move, context.stats);
            return result;
        }

        // At a root pass, the after-pass child is still the first real move choice in the public
        // PV. Treat only that child as a root traversal so root PVS can help without exposing a
        // fake pass move or changing best_move=nullopt semantics.
        const ZobristHash next_hash = hash_after_pass(hash, board);
#ifndef NDEBUG
        assert(next_hash == zobrist_hash(*next));
#endif
        const bool use_after_pass_root_pvs = is_root && empty_count(*next) >= root_pvs_min_empties;
        const NodeResult child =
            solve_node(*next, next_hash, -beta, -alpha, context, use_after_pass_root_pvs);
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

    const OrderedMoveIndexes ordered_moves = ordered_legal_move_indexes(board, hash, moves);
    const bool use_root_pvs = is_root && should_use_root_pvs(empties, ordered_moves.size);
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
        .score = best_score.value_or(score(board, board.side_to_move)),
        .principal_variation = best_principal_variation,
    };
    context.transpositions.store(hash, empties, result.score, original_alpha, beta,
                                 result.best_move, context.stats);
    return result;
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult solve_root_candidate_full(const Board& board, ZobristHash hash,
                                                   ExactEndgameContext& context) noexcept {
    return solve_node(board, hash, exact_score_min, exact_score_max, context, false);
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] bool root_candidate_needs_full_search(const Board& board, ZobristHash hash,
                                                    int required_score,
                                                    ExactEndgameContext& context) noexcept {
    // Root candidate score is -child_score. The null window [a, a + 1), where
    // a = -required_score, proves rejection when child_score > a, because then
    // root_score < required_score. Equality is handled by required_score:
    // lower-index candidates need only tie, higher-index candidates must beat.
    const int scout_alpha = -required_score;
    const NodeResult scout = solve_node(board, hash, scout_alpha, scout_alpha + 1, context, false);
    return !root_scout_rejects_candidate(scout, required_score);
}

} // namespace

ExactEndgameResult solve_exact_endgame(const Board& board) noexcept {
    ExactEndgameContext context{empty_count(board)};
    const NodeResult result =
        solve_node(board, zobrist_hash(board), exact_score_min, exact_score_max, context, true);

    return ExactEndgameResult{
        .best_move = result.best_move,
        .disc_margin = result.score,
        .empties = empty_count(board),
        .nodes = context.stats.nodes,
        .principal_variation = principal_variation_to_vector(result.principal_variation),
        .stats = context.stats,
    };
}

} // namespace othello
