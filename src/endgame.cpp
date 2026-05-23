#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <othello/endgame.hpp>
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

struct OrderedMoveIndexes {
    struct Move {
        int index = 0;
        int order_score = 0;
        Board next;
    };

    std::array<Move, 64> moves{};
    std::size_t size = 0;
};

struct ExactEndgameContext {
    std::uint64_t nodes = 0;
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
[[nodiscard]] NodeResult solve_node(const Board& board, int alpha, int beta,
                                    ExactEndgameContext& context) noexcept {
    ++context.nodes;

    if (is_game_over(board)) {
        return NodeResult{.score = score(board, board.side_to_move)};
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const std::optional<Board> next = pass_turn(board);
        if (!next.has_value()) {
            return NodeResult{.score = score(board, board.side_to_move)};
        }

        const NodeResult child = solve_node(*next, -beta, -alpha, context);
        return NodeResult{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
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

        const NodeResult child = solve_node(ordered_move.next, -beta, -alpha, context);
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

} // namespace

ExactEndgameResult solve_exact_endgame(const Board& board) noexcept {
    ExactEndgameContext context;
    const NodeResult result = solve_node(board, exact_score_min, exact_score_max, context);

    return ExactEndgameResult{
        .best_move = result.best_move,
        .disc_margin = result.score,
        // NOLINTNEXTLINE(readability-redundant-casting)
        .empties = static_cast<int>(std::popcount(board.empty())),
        .nodes = context.nodes,
        .principal_variation = principal_variation_to_vector(result.principal_variation),
    };
}

} // namespace othello
