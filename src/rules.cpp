#include "bitboard_rules.hpp"

#include <bit>
#include <othello/rules.hpp>

namespace othello {
Bitboard legal_moves(const Board& board) noexcept {
    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));
    return rules_detail::legal_moves_for(own_discs, opponent_discs);
}

bool has_legal_move(const Board& board) noexcept {
    return legal_moves(board) != 0;
}

Bitboard flips_for_move(const Board& board, Square square) noexcept {
    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));
    return rules_detail::flips_for_move_for(own_discs, opponent_discs, square);
}

std::optional<Board> apply_move(const Board& board, Square square) noexcept {
    const Bitboard move_bit = square.bit();
    if ((board.occupied() & move_bit) != 0) {
        return std::nullopt;
    }

    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));
    const Bitboard flips =
        rules_detail::flips_for_known_empty_move(own_discs, opponent_discs, move_bit);
    if (flips == 0) {
        return std::nullopt;
    }

    Board next = board;

    if (board.side_to_move == Side::Black) {
        next.black |= move_bit | flips;
        next.white &= ~flips;
    } else {
        next.white |= move_bit | flips;
        next.black &= ~flips;
    }

    next.side_to_move = opponent(board.side_to_move);
    return next;
}

std::optional<Board> pass_turn(const Board& board) noexcept {
    if (has_legal_move(board)) {
        return std::nullopt;
    }

    Board next = board;
    next.side_to_move = opponent(board.side_to_move);
    if (!has_legal_move(next)) {
        return std::nullopt;
    }

    return next;
}

bool is_game_over(const Board& board) noexcept {
    if (has_legal_move(board)) {
        return false;
    }

    Board opponent_board = board;
    opponent_board.side_to_move = opponent(board.side_to_move);
    return !has_legal_move(opponent_board);
}

int disc_count(const Board& board, Side side) noexcept {
    return std::popcount(board.discs(side));
}

int score(const Board& board, Side side) noexcept {
    return disc_count(board, side) - disc_count(board, opponent(side));
}

} // namespace othello
