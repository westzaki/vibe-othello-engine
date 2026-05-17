#include "hash_detail.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <othello/evaluation.hpp>
#include <othello/hash.hpp>
#include <othello/rules.hpp>
#include <othello/search.hpp>
#include <vector>

namespace othello {
namespace {

struct PrincipalVariation {
    std::array<int, 64> indexes{};
    std::size_t size = 0;
};

struct NodeResult {
    std::optional<Square> best_move;
    int score = 0;
    PrincipalVariation principal_variation;
};

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

class TranspositionTable {
public:
    explicit TranspositionTable(const SearchOptions& options) noexcept
        : entry_count_{normalized_entry_count(options)},
          entries_{entry_count_ == 0 ? nullptr
                                     : new (std::nothrow) TranspositionEntry[entry_count_]} {}

    [[nodiscard]] std::optional<NodeResult> lookup(ZobristHash hash, int depth, int alpha, int beta,
                                                   SearchStats& stats) const noexcept {
        if (entries_ == nullptr) {
            return std::nullopt;
        }

        ++stats.tt_lookups;
        const TranspositionEntry& entry = entries_[entry_index(hash)];
        if (!entry.occupied || entry.hash != hash || entry.depth < depth) {
            return std::nullopt;
        }

        if (entry.bound == TranspositionBound::Exact) {
            ++stats.tt_hits;
            ++stats.tt_exact_hits;
            return node_result_from_entry(entry);
        }
        if (entry.bound == TranspositionBound::Lower && entry.score >= beta) {
            ++stats.tt_hits;
            ++stats.tt_lower_hits;
            return node_result_from_entry(entry);
        }
        if (entry.bound == TranspositionBound::Upper && entry.score <= alpha) {
            ++stats.tt_hits;
            ++stats.tt_upper_hits;
            return node_result_from_entry(entry);
        }

        return std::nullopt;
    }

    void store(ZobristHash hash, int depth, int score, int original_alpha, int beta,
               const std::optional<Square>& best_move, SearchStats& stats) noexcept {
        if (entries_ == nullptr) {
            return;
        }

        TranspositionEntry& entry = entries_[entry_index(hash)];
        // Keep deeper entries when a different position collides in the same direct-mapped slot.
        // Same-position updates are still allowed so newer bounds can refresh the entry.
        if (entry.occupied && entry.hash != hash && depth < entry.depth) {
            return;
        }

        TranspositionBound bound = TranspositionBound::Exact;
        if (score <= original_alpha) {
            bound = TranspositionBound::Upper;
        } else if (score >= beta) {
            bound = TranspositionBound::Lower;
        }

        ++stats.tt_stores;
        if (entry.occupied) {
            ++stats.tt_overwrites;
            if (entry.hash != hash) {
                ++stats.tt_collisions;
            }
        }

        entry = TranspositionEntry{
            .hash = hash,
            .depth = depth,
            .score = score,
            .best_move_index = best_move.has_value() ? best_move->index() : -1,
            .bound = bound,
            .occupied = true,
        };
    }

private:
    static constexpr std::size_t default_entry_count = 1 << 18;

    // Direct-mapped indexing uses a bit mask, so non-power-of-two requests are
    // rounded up to the next power of two. Oversized requests fall back to the default.
    [[nodiscard]] static constexpr std::size_t
    normalized_entry_count(const SearchOptions& options) noexcept {
        if (!options.use_transposition_table || options.transposition_table_entries == 0) {
            return 0;
        }

        const std::size_t requested = options.transposition_table_entries;
        if (std::has_single_bit(requested)) {
            return requested;
        }

        constexpr std::size_t max_power_of_two = std::size_t{1}
                                                 << (std::numeric_limits<std::size_t>::digits - 1);
        if (requested > max_power_of_two) {
            return default_entry_count;
        }

        std::size_t normalized = 1;
        while (normalized < requested) {
            normalized <<= 1;
        }
        return normalized;
    }

    std::size_t entry_count_ = 0;
    std::unique_ptr<TranspositionEntry[]> entries_; // NOLINT(cppcoreguidelines-avoid-c-arrays,
                                                    // modernize-avoid-c-arrays)

    [[nodiscard]] std::size_t entry_index(ZobristHash hash) const noexcept {
        return static_cast<std::size_t>(hash) & (entry_count_ - 1);
    }

    [[nodiscard]] static std::optional<Square>
    square_from_entry(const TranspositionEntry& entry) noexcept {
        if (entry.best_move_index < Square::min_index ||
            entry.best_move_index > Square::max_index) {
            return std::nullopt;
        }
        return Square::from_index(entry.best_move_index);
    }

    [[nodiscard]] static NodeResult
    node_result_from_entry(const TranspositionEntry& entry) noexcept {
        const std::optional<Square> best_move = square_from_entry(entry);
        NodeResult result{
            .best_move = best_move,
            .score = entry.score,
        };

        // TT entries intentionally store only a score and root move, not a full line.
        // A cached result therefore reports the conservative PV fragment we know.
        if (best_move.has_value()) {
            result.principal_variation.indexes[0] = best_move->index();
            result.principal_variation.size = 1;
        }
        return result;
    }
};

struct SearchContext {
    explicit SearchContext(const SearchOptions& options, bool enable_dynamic_move_ordering) noexcept
        : transpositions{options}, dynamic_move_ordering{enable_dynamic_move_ordering} {}

    SearchStats stats;
    TranspositionTable transpositions;
    bool dynamic_move_ordering = false;
};

constexpr int search_score_min = -1'000'000'000;
constexpr int search_score_max = 1'000'000'000;
constexpr Bitboard corner_squares =
    (Bitboard{1} << 0) | (Bitboard{1} << 7) | (Bitboard{1} << 56) | (Bitboard{1} << 63);

[[nodiscard]] constexpr bool is_corner(int index) noexcept {
    return index == 0 || index == 7 || index == 56 || index == 63;
}

[[nodiscard]] constexpr bool is_x_square(int index) noexcept {
    return index == 9 || index == 14 || index == 49 || index == 54;
}

[[nodiscard]] constexpr bool is_edge(int index) noexcept {
    const int file = index % 8;
    const int rank = index / 8;
    return file == 0 || file == 7 || rank == 0 || rank == 7;
}

[[nodiscard]] Board board_after_move(const Board& board, Square square, Bitboard flips) noexcept;

[[nodiscard]] constexpr bool is_x_square_next_to_empty_corner(int index,
                                                              Bitboard occupied) noexcept {
    switch (index) {
    case 9:
        return (occupied & (Bitboard{1} << 0)) == 0;
    case 14:
        return (occupied & (Bitboard{1} << 7)) == 0;
    case 49:
        return (occupied & (Bitboard{1} << 56)) == 0;
    case 54:
        return (occupied & (Bitboard{1} << 63)) == 0;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool should_use_dynamic_move_ordering(bool enabled, std::size_t move_count,
                                                              int depth) noexcept {
    return enabled && depth >= 3 && move_count >= 5;
}

[[nodiscard]] int move_order_score(const Board& board, Square square, Bitboard flips,
                                   std::size_t move_count, int depth,
                                   bool dynamic_move_ordering) noexcept {
    const int index = square.index();
    if (!should_use_dynamic_move_ordering(dynamic_move_ordering, move_count, depth)) {
        if (is_corner(index)) {
            return 3'000;
        }
        if (is_edge(index)) {
            return 2'000;
        }
        if (is_x_square(index)) {
            return 0;
        }
        return 1'000;
    }

    int score = 0;
    if (is_corner(index)) {
        score += 100'000;
    }
    if (is_edge(index)) {
        score += 1'000;
    }
    if (is_x_square_next_to_empty_corner(index, board.occupied())) {
        score -= 30'000;
    }

    const Board next = board_after_move(board, square, flips);
    const Bitboard opponent_moves = legal_moves(next);
    if ((opponent_moves & corner_squares) != 0) {
        score -= 80'000;
    }
    score -= static_cast<int>(std::popcount(opponent_moves)) * 300;

    return score;
}

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(const Board& board, Bitboard moves,
                                                            int depth,
                                                            bool dynamic_move_ordering) noexcept {
    OrderedMoveIndexes candidates;
    const std::size_t move_count = static_cast<std::size_t>(std::popcount(moves));

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
                                                dynamic_move_ordering),
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

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(const Board& board, Bitboard moves,
                                                            int depth,
                                                            std::optional<Square> preferred_move,
                                                            bool dynamic_move_ordering) noexcept {
    OrderedMoveIndexes candidates =
        ordered_legal_move_indexes(board, moves, depth, dynamic_move_ordering);
    if (!preferred_move.has_value() || (moves & preferred_move->bit()) == 0) {
        return candidates;
    }

    const int preferred_index = preferred_move->index();
    for (std::size_t index = 0; index < candidates.size; ++index) {
        if (candidates.moves[index].index != preferred_index) {
            continue;
        }

        const OrderedMoveIndexes::Move preferred = candidates.moves[index];
        for (std::size_t shift = index; shift > 0; --shift) {
            candidates.moves[shift] = candidates.moves[shift - 1];
        }
        candidates.moves[0] = preferred;
        break;
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

[[nodiscard]] bool is_better_best_move(int candidate_score, Square candidate,
                                       const std::optional<int>& best_score,
                                       const std::optional<Square>& best_move) noexcept {
    if (!best_score.has_value()) {
        return true;
    }
    if (candidate_score != *best_score) {
        return candidate_score > *best_score;
    }
    return !best_move.has_value() || candidate.index() < best_move->index();
}

[[nodiscard]] PrincipalVariation
principal_variation_with_move(Square move, const PrincipalVariation& child_variation) noexcept {
    PrincipalVariation principal_variation;
    principal_variation.indexes[0] = move.index();
    principal_variation.size = 1;

    const std::size_t child_size =
        std::min(child_variation.size, principal_variation.indexes.size() - 1);
    for (std::size_t index = 0; index < child_size; ++index) {
        principal_variation.indexes[index + 1] = child_variation.indexes[index];
    }
    principal_variation.size += child_size;

    return principal_variation;
}

[[nodiscard]] std::vector<Square>
principal_variation_to_vector(const PrincipalVariation& principal_variation) noexcept {
    std::vector<Square> squares;
    try {
        squares.reserve(principal_variation.size);
        for (std::size_t index = 0; index < principal_variation.size; ++index) {
            const std::optional<Square> square =
                Square::from_index(principal_variation.indexes[index]);
            if (square.has_value()) {
                squares.push_back(*square);
            }
        }
    } catch (...) {
        return {};
    }
    return squares;
}

[[nodiscard]] PrincipalVariation
principal_variation_from_vector(const std::vector<Square>& principal_variation) noexcept {
    PrincipalVariation result;
    const std::size_t size = std::min(principal_variation.size(), result.indexes.size());
    for (std::size_t index = 0; index < size; ++index) {
        result.indexes[index] = principal_variation[index].index();
    }
    result.size = size;
    return result;
}

[[nodiscard]] Board board_after_move(const Board& board, Square square, Bitboard flips) noexcept {
    const Bitboard move_bit = square.bit();

    Board next = board;
    if (board.side_to_move == Side::Black) {
        next.black = board.black | move_bit | flips;
        next.white = board.white & ~flips;
    } else {
        next.white = board.white | move_bit | flips;
        next.black = board.black & ~flips;
    }
    next.side_to_move = opponent(board.side_to_move);

    return next;
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

    const std::optional<NodeResult> cached =
        context.transpositions.lookup(hash, depth, alpha, beta, context.stats);
    if (cached.has_value()) {
        return *cached;
    }

    if (depth <= 0 || is_game_over(board)) {
        const NodeResult result{.score = evaluate_basic(board, board.side_to_move)};
        context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                     result.best_move, context.stats);
        return result;
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const std::optional<Board> next = pass_turn(board);
        if (!next.has_value()) {
            const NodeResult result{.score = evaluate_basic(board, board.side_to_move)};
            context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                         result.best_move, context.stats);
            return result;
        }

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

    std::optional<int> best_score;
    std::optional<Square> best_move;
    PrincipalVariation best_principal_variation;

    const bool use_dynamic_ordering = context.dynamic_move_ordering && !is_root;
    const std::size_t move_count = static_cast<std::size_t>(std::popcount(moves));
    if (should_use_dynamic_move_ordering(use_dynamic_ordering, move_count, depth)) {
        ++context.stats.dynamic_ordering_nodes;
        context.stats.dynamic_ordering_moves += move_count;
    }
    std::optional<Square> preferred_move = preferred_move_from_hint(pv_hint);
    if (!preferred_move.has_value() && is_root) {
        preferred_move = root_preferred_move;
    }
    const OrderedMoveIndexes ordered_moves =
        ordered_legal_move_indexes(board, moves, depth, preferred_move, use_dynamic_ordering);
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

        const NodeResult child =
            search_node(next, next_hash, depth - 1, -beta, -alpha, context, std::nullopt,
                        child_hint_after_move(pv_hint, *square), false);
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

    const NodeResult result{
        .best_move = best_move,
        .score = best_score.value_or(evaluate_basic(board, board.side_to_move)),
        .principal_variation = best_principal_variation,
    };
    context.transpositions.store(hash, depth, result.score, original_alpha, beta, result.best_move,
                                 context.stats);
    return result;
}

[[nodiscard]] SearchResult search_with_context(const Board& board, int depth,
                                               SearchContext& context,
                                               std::optional<Square> root_preferred_move,
                                               PrincipalVariationHint pv_hint) noexcept {
    const NodeResult result =
        search_node(board, zobrist_hash(board), depth, search_score_min, search_score_max, context,
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

} // namespace

SearchResult search(const Board& board, const SearchOptions& options) noexcept {
    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{options, true};
    return search_with_context(board, search_depth, context, std::nullopt,
                               PrincipalVariationHint{});
}

SearchResult search_fixed_depth(const Board& board, int depth) noexcept {
    return search(board, SearchOptions{.max_depth = depth});
}

SearchResult search_iterative(const Board& board, const SearchOptions& options) noexcept {
    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{options, true};

    if (search_depth == 0) {
        return search_with_context(board, 0, context, std::nullopt, PrincipalVariationHint{});
    }

    SearchResult result;
    std::optional<Square> previous_best_move;
    PrincipalVariation previous_principal_variation;
    for (int depth = 1; depth <= search_depth; ++depth) {
        const PrincipalVariationHint pv_hint{
            .principal_variation =
                previous_principal_variation.size == 0 ? nullptr : &previous_principal_variation,
            .index = 0,
            .matches_prefix = previous_principal_variation.size > 0,
        };
        result = search_with_context(board, depth, context, previous_best_move, pv_hint);
        previous_best_move = result.best_move;
        previous_principal_variation = principal_variation_from_vector(result.principal_variation);
    }

    return result;
}

} // namespace othello
