#include "positions/metrics.hpp"

#include <bit>

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

} // namespace othello::benchmarks
