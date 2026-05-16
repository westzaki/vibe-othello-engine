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

namespace othello {
namespace {

struct NodeResult {
    std::optional<Square> best_move;
    int score = 0;
};

struct OrderedMoveIndexes {
    std::array<int, 64> indexes{};
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

    [[nodiscard]] std::optional<NodeResult> lookup(ZobristHash hash, int depth, int alpha,
                                                   int beta) const noexcept {
        if (entries_ == nullptr) {
            return std::nullopt;
        }

        const TranspositionEntry& entry = entries_[entry_index(hash)];
        if (!entry.occupied || entry.hash != hash || entry.depth < depth) {
            return std::nullopt;
        }

        if (entry.bound == TranspositionBound::Exact) {
            return node_result_from_entry(entry);
        }
        if (entry.bound == TranspositionBound::Lower && entry.score >= beta) {
            return node_result_from_entry(entry);
        }
        if (entry.bound == TranspositionBound::Upper && entry.score <= alpha) {
            return node_result_from_entry(entry);
        }

        return std::nullopt;
    }

    void store(ZobristHash hash, int depth, int score, int original_alpha, int beta,
               const std::optional<Square>& best_move) noexcept {
        if (entries_ == nullptr) {
            return;
        }

        TranspositionBound bound = TranspositionBound::Exact;
        if (score <= original_alpha) {
            bound = TranspositionBound::Upper;
        } else if (score >= beta) {
            bound = TranspositionBound::Lower;
        }

        entries_[entry_index(hash)] = TranspositionEntry{
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
        return NodeResult{
            .best_move = square_from_entry(entry),
            .score = entry.score,
        };
    }
};

struct SearchContext {
    explicit SearchContext(const SearchOptions& options) noexcept : transpositions{options} {}

    std::uint64_t nodes = 0;
    TranspositionTable transpositions;
};

constexpr int search_score_min = -1'000'000'000;
constexpr int search_score_max = 1'000'000'000;

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

[[nodiscard]] constexpr int move_order_priority(int index) noexcept {
    if (is_corner(index)) {
        return 0;
    }
    if (is_edge(index)) {
        return 1;
    }
    if (is_x_square(index)) {
        return 3;
    }
    return 2;
}

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(Bitboard moves) noexcept {
    OrderedMoveIndexes candidates;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Bitboard move_bit = Bitboard{1} << index;
        if ((moves & move_bit) != 0) {
            candidates.indexes[candidates.size] = index;
            ++candidates.size;
        }
    }

    std::ranges::sort(candidates.indexes.begin(), candidates.indexes.begin() + candidates.size,
                      [](int lhs, int rhs) {
                          const int lhs_priority = move_order_priority(lhs);
                          const int rhs_priority = move_order_priority(rhs);
                          if (lhs_priority != rhs_priority) {
                              return lhs_priority < rhs_priority;
                          }
                          return lhs < rhs;
                      });

    return candidates;
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
                                     int beta, SearchContext& context) noexcept {
    ++context.nodes;

    const int original_alpha = alpha;

    const std::optional<NodeResult> cached =
        context.transpositions.lookup(hash, depth, alpha, beta);
    if (cached.has_value()) {
        return *cached;
    }

    if (depth <= 0 || is_game_over(board)) {
        const NodeResult result{.score = evaluate_basic(board, board.side_to_move)};
        context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                     result.best_move);
        return result;
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const std::optional<Board> next = pass_turn(board);
        if (!next.has_value()) {
            const NodeResult result{.score = evaluate_basic(board, board.side_to_move)};
            context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                         result.best_move);
            return result;
        }

        const ZobristHash next_hash = hash_after_pass(hash, board.side_to_move);
        assert(next_hash == zobrist_hash(*next));

        const NodeResult child = search_node(*next, next_hash, depth - 1, -beta, -alpha, context);
        const NodeResult result{.score = -child.score};
        context.transpositions.store(hash, depth, result.score, original_alpha, beta,
                                     result.best_move);
        return result;
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;

    const OrderedMoveIndexes ordered_moves = ordered_legal_move_indexes(moves);
    for (std::size_t move = 0; move < ordered_moves.size; ++move) {
        const std::optional<Square> square = Square::from_index(ordered_moves.indexes[move]);
        if (!square.has_value()) {
            continue;
        }

        const Bitboard flips = flips_for_move(board, *square);
        if (flips == 0) {
            continue;
        }

        const Board next = board_after_move(board, *square, flips);
        const ZobristHash next_hash = hash_after_move(hash, board.side_to_move, *square, flips);
        assert(next_hash == zobrist_hash(next));

        const NodeResult child = search_node(next, next_hash, depth - 1, -beta, -alpha, context);
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, *square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
        }

        alpha = std::max(alpha, candidate_score);
        if (alpha >= beta) {
            break;
        }
    }

    const NodeResult result{
        .best_move = best_move,
        .score = best_score.value_or(evaluate_basic(board, board.side_to_move)),
    };
    context.transpositions.store(hash, depth, result.score, original_alpha, beta, result.best_move);
    return result;
}

} // namespace

SearchResult search(const Board& board, const SearchOptions& options) noexcept {
    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{options};
    const NodeResult result = search_node(board, zobrist_hash(board), search_depth,
                                          search_score_min, search_score_max, context);

    return SearchResult{
        .best_move = result.best_move,
        .score = result.score,
        .depth = search_depth,
        .nodes = context.nodes,
    };
}

SearchResult search_fixed_depth(const Board& board, int depth) noexcept {
    return search(board, SearchOptions{.max_depth = depth});
}

} // namespace othello
