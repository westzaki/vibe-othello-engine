#include "positions/metrics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>

namespace othello::benchmarks {

bool same_board(const Board& left, const Board& right) noexcept {
    return left.black == right.black && left.white == right.white &&
           left.side_to_move == right.side_to_move;
}

int empty_count(const Board& board) noexcept {
    return 64 - std::popcount(board.black | board.white);
}

int legal_move_count(const Board& board) noexcept {
    return std::popcount(legal_moves(board));
}

int count_bits(Bitboard bits) noexcept {
    return std::popcount(bits);
}

bool has_x_square_risk(const Board& board, Bitboard legal_moves) noexcept {
    auto risky_x_squares = legal_moves & x_square_bits();
    while (risky_x_squares != 0) {
        const auto x_square = risky_x_squares & (~risky_x_squares + 1);
        const auto adjacent_corner = corner_for_x_square(x_square);
        if (adjacent_corner != 0 && (board.occupied() & adjacent_corner) == 0) {
            return true;
        }
        risky_x_squares &= ~x_square;
    }
    return false;
}

namespace {

[[nodiscard]] constexpr Bitboard orthogonal_neighbors(int index) noexcept {
    Bitboard neighbors = 0;
    const int file = index % 8;
    const int rank = index / 8;
    if (file > 0) {
        neighbors |= Bitboard{1} << (index - 1);
    }
    if (file < 7) {
        neighbors |= Bitboard{1} << (index + 1);
    }
    if (rank > 0) {
        neighbors |= Bitboard{1} << (index - 8);
    }
    if (rank < 7) {
        neighbors |= Bitboard{1} << (index + 8);
    }
    return neighbors;
}

void add_empty_region_metrics(Bitboard empty, EndgamePositionMetrics& metrics) noexcept {
    Bitboard remaining = empty;
    while (remaining != 0) {
        const int seed_index = std::countr_zero(remaining);
        Bitboard region = Bitboard{1} << seed_index;
        std::array<int, 64> stack{};
        std::size_t stack_size = 1;
        stack[0] = seed_index;

        while (stack_size > 0) {
            const int index = stack[--stack_size];
            Bitboard neighbors = orthogonal_neighbors(index) & remaining & ~region;
            while (neighbors != 0) {
                const int next_index = std::countr_zero(neighbors);
                neighbors &= neighbors - 1;
                region |= Bitboard{1} << next_index;
                stack[stack_size] = next_index;
                ++stack_size;
            }
        }

        remaining &= ~region;
        const int region_size = static_cast<int>(std::popcount(region));
        ++metrics.empty_region_count;
        if (region_size % 2 == 0) {
            ++metrics.even_region_count;
        } else {
            ++metrics.odd_region_count;
        }
        if (region_size == 1) {
            ++metrics.singleton_region_count;
        }
        metrics.largest_region_size = std::max(metrics.largest_region_size, region_size);
    }
}

} // namespace

EndgamePositionMetrics compute_endgame_metrics(const Board& board) noexcept {
    EndgamePositionMetrics metrics;
    const Bitboard empty = board.empty();
    const Bitboard current_moves = legal_moves(board);
    auto opponent_board = board;
    opponent_board.side_to_move = opponent(board.side_to_move);
    const Bitboard opponent_moves = legal_moves(opponent_board);

    metrics.empties = empty_count(board);
    metrics.score_current = score(board, board.side_to_move);
    metrics.legal_moves_current = static_cast<int>(std::popcount(current_moves));
    metrics.legal_moves_opponent = static_cast<int>(std::popcount(opponent_moves));
    metrics.root_pass = pass_turn(board).has_value();
    metrics.game_over = is_game_over(board);

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Bitboard bit = Bitboard{1} << index;
        if ((empty & bit) != 0 && is_corner_index(index)) {
            ++metrics.empty_corner_count;
        }
        if ((empty & bit) != 0 && is_edge_index(index)) {
            ++metrics.edge_empty_count;
        }
        if ((current_moves & bit) != 0 && is_corner_index(index)) {
            ++metrics.legal_corner_count;
        }
        if ((opponent_moves & bit) != 0 && is_corner_index(index)) {
            ++metrics.opponent_legal_corner_count;
        }
        if ((current_moves & bit) != 0 && is_edge_index(index)) {
            ++metrics.legal_edge_count;
        }
        if ((current_moves & bit) != 0 && is_x_square_next_to_empty_corner(index, empty)) {
            ++metrics.x_square_legal_risk_count;
        }
    }

    add_empty_region_metrics(empty, metrics);
    return metrics;
}

} // namespace othello::benchmarks
