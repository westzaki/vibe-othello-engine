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
    std::array<int, 64> indexes{};
    std::size_t size = 0;
};

struct ExactEndgameContext {
    std::uint64_t nodes = 0;
};

// Final disc margins are always in [-64, 64]; this leaves a simple generous alpha-beta window.
constexpr int exact_score_min = -1'000;
constexpr int exact_score_max = 1'000;

[[nodiscard]] OrderedMoveIndexes ordered_legal_move_indexes(Bitboard moves) noexcept {
    OrderedMoveIndexes result;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Bitboard move_bit = Bitboard{1} << index;
        if ((moves & move_bit) != 0) {
            result.indexes[result.size] = index;
            ++result.size;
        }
    }

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

    const OrderedMoveIndexes ordered_moves = ordered_legal_move_indexes(moves);
    for (std::size_t move = 0; move < ordered_moves.size; ++move) {
        const std::optional<Square> square = Square::from_index(ordered_moves.indexes[move]);
        if (!square.has_value()) {
            continue;
        }

        const std::optional<Board> next = apply_move(board, *square);
        if (!next.has_value()) {
            continue;
        }

        const NodeResult child = solve_node(*next, -beta, -alpha, context);
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
        .empties = static_cast<int>(std::popcount(board.empty())),
        .nodes = context.nodes,
        .principal_variation = principal_variation_to_vector(result.principal_variation),
    };
}

} // namespace othello
