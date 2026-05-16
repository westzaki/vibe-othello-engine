#include <algorithm>
#include <othello/evaluation.hpp>
#include <othello/rules.hpp>
#include <othello/search.hpp>

namespace othello {
namespace {

struct NodeResult {
    std::optional<Square> best_move;
    int score = 0;
};

constexpr int search_score_min = -1'000'000'000;
constexpr int search_score_max = 1'000'000'000;

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

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const std::optional<Square> square = Square::from_index(index);
        if (!square.has_value() || (moves & square->bit()) == 0) {
            continue;
        }

        const std::optional<Board> next = apply_move(board, *square);
        if (!next.has_value()) {
            continue;
        }

        const NodeResult child = search_node(*next, depth - 1, -beta, -alpha, nodes);
        const int candidate_score = -child.score;
        if (!best_score.has_value() || candidate_score > *best_score) {
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
