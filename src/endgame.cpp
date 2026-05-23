#include <algorithm>
#include <array>
#include <bit>
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

struct PrincipalVariation {
    std::array<int, 64> indexes{};
    std::size_t size = 0;
};

struct NodeResult {
    std::optional<Square> best_move;
    int score = 0;
    PrincipalVariation principal_variation;
};

enum class ExactTranspositionBound {
    Exact,
    Lower,
    Upper,
};

struct ExactTranspositionEntry {
    ZobristHash hash = 0;
    int empties = -1;
    int score = 0;
    int best_move_index = -1;
    ExactTranspositionBound bound = ExactTranspositionBound::Exact;
    bool occupied = false;
};

struct OrderedMoveIndexes {
    struct Move {
        int index = 0;
        int order_score = 0;
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
            .empties = empties,
            .score = score,
            .best_move_index = best_move.has_value() ? best_move->index() : -1,
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

    [[nodiscard]] static std::optional<Square>
    square_from_entry(const ExactTranspositionEntry& entry) noexcept {
        if (entry.best_move_index < Square::min_index ||
            entry.best_move_index > Square::max_index) {
            return std::nullopt;
        }
        return Square::from_index(entry.best_move_index);
    }

    [[nodiscard]] static NodeResult
    node_result_from_entry(const ExactTranspositionEntry& entry) noexcept {
        const std::optional<Square> best_move = square_from_entry(entry);
        NodeResult result{
            .best_move = best_move,
            .score = entry.score,
        };
        if (best_move.has_value()) {
            result.principal_variation.indexes[0] = best_move->index();
            result.principal_variation.size = 1;
        }
        return result;
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
constexpr EndgameMoveOrderingParams default_move_ordering_params{};
constexpr Bitboard corner_squares =
    (Bitboard{1} << 0) | (Bitboard{1} << 7) | (Bitboard{1} << 56) | (Bitboard{1} << 63);

[[nodiscard]] int empty_count(const Board& board) noexcept {
    // NOLINTNEXTLINE(readability-redundant-casting)
    return static_cast<int>(std::popcount(board.empty()));
}

[[nodiscard]] constexpr bool is_corner(int index) noexcept {
    return index == 0 || index == 7 || index == 56 || index == 63;
}

[[nodiscard]] constexpr bool is_edge(int index) noexcept {
    const int file = index % 8;
    const int rank = index / 8;
    return file == 0 || file == 7 || rank == 0 || rank == 7;
}

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

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(const Board& board,
                                                            Bitboard moves) noexcept {
    OrderedMoveIndexes result;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Bitboard move_bit = Bitboard{1} << index;
        if ((moves & move_bit) != 0) {
            const std::optional<Square> square = Square::from_index(index);
            if (!square.has_value()) {
                continue;
            }

            const std::optional<Board> next = apply_move(board, *square);
            if (!next.has_value()) {
                continue;
            }

            result.moves[result.size] = OrderedMoveIndexes::Move{
                .index = index,
                .order_score = move_order_score(board, index, *next, default_move_ordering_params),
                .next = *next,
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

// Exact negamax searches to game-over leaves only.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult solve_node(const Board& board, ZobristHash hash, int alpha, int beta,
                                    ExactEndgameContext& context, bool is_root) noexcept {
    ++context.stats.nodes;
    const int original_alpha = alpha;
    const int empties = empty_count(board);

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

        const NodeResult child =
            solve_node(*next, zobrist_hash(*next), -beta, -alpha, context, false);
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

    const OrderedMoveIndexes ordered_moves = ordered_legal_move_indexes(board, moves);
    for (std::size_t move = 0; move < ordered_moves.size; ++move) {
        const OrderedMoveIndexes::Move& ordered_move = ordered_moves.moves[move];
        const std::optional<Square> square = Square::from_index(ordered_move.index);
        if (!square.has_value()) {
            continue;
        }

        // Root results are public API, so compare exact root candidate scores instead of
        // alpha-beta bounds. Interior nodes still use normal alpha-beta windows.
        const NodeResult child =
            is_root ? solve_node(ordered_move.next, zobrist_hash(ordered_move.next),
                                 exact_score_min, exact_score_max, context, false)
                    : solve_node(ordered_move.next, zobrist_hash(ordered_move.next), -beta, -alpha,
                                 context, false);
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
        .score = best_score.value_or(score(board, board.side_to_move)),
        .principal_variation = best_principal_variation,
    };
    context.transpositions.store(hash, empties, result.score, original_alpha, beta,
                                 result.best_move, context.stats);
    return result;
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
