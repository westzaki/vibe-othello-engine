#include <algorithm>
#include <array>
#include <cstddef>
#include <othello/evaluation.hpp>
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

// Fixed-depth negamax is easiest to audit as direct recursion at this stage.
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] NodeResult search_node(const Board& board, int depth, int alpha, int beta,
                                     std::uint64_t& nodes) noexcept {
    ++nodes;

    if (depth <= 0 || is_game_over(board)) {
        return NodeResult{.score = evaluate_basic(board, board.side_to_move)};
    }

    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        const std::optional<Board> next = pass_turn(board);
        if (!next.has_value()) {
            return NodeResult{.score = evaluate_basic(board, board.side_to_move)};
        }

        const NodeResult child = search_node(*next, depth - 1, -beta, -alpha, nodes);
        return NodeResult{.score = -child.score};
    }

    std::optional<int> best_score;
    std::optional<Square> best_move;

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

        const NodeResult child = search_node(*next, depth - 1, -beta, -alpha, nodes);
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

    return NodeResult{
        .best_move = best_move,
        .score = best_score.value_or(evaluate_basic(board, board.side_to_move)),
    };
}

} // namespace

SearchResult search_fixed_depth(const Board& board, int depth) noexcept {
    const int search_depth = depth < 0 ? 0 : depth;
    std::uint64_t nodes = 0;
    const NodeResult result =
        search_node(board, search_depth, search_score_min, search_score_max, nodes);

    return SearchResult{
        .best_move = result.best_move,
        .score = result.score,
        .depth = search_depth,
        .nodes = nodes,
    };
}

} // namespace othello
