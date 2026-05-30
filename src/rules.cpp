#include "bitboard_ops.hpp"

#include <bit>
#include <othello/rules.hpp>

namespace othello {
namespace {

using bitboard_detail::shift_east;
using bitboard_detail::shift_north;
using bitboard_detail::shift_northeast;
using bitboard_detail::shift_northwest;
using bitboard_detail::shift_south;
using bitboard_detail::shift_southeast;
using bitboard_detail::shift_southwest;
using bitboard_detail::shift_west;

template <Bitboard (*Shift)(Bitboard) noexcept>
[[nodiscard]] Bitboard legal_moves_in_direction(Bitboard own_discs, Bitboard opponent_discs,
                                                Bitboard empty_squares) noexcept {
    Bitboard captured = Shift(own_discs) & opponent_discs;

    for (int step = 0; step < 5; ++step) {
        captured |= Shift(captured) & opponent_discs;
    }

    return Shift(captured) & empty_squares;
}

template <Bitboard (*Shift)(Bitboard) noexcept>
[[nodiscard]] Bitboard flips_in_direction(Bitboard move_bit, Bitboard own_discs,
                                          Bitboard opponent_discs) noexcept {
    Bitboard flips = 0;
    Bitboard captured = Shift(move_bit) & opponent_discs;

    while (captured != 0) {
        flips |= captured;
        const Bitboard next = Shift(captured);
        if ((next & own_discs) != 0) {
            return flips;
        }

        captured = next & opponent_discs;
    }

    return 0;
}

} // namespace

Bitboard legal_moves(const Board& board) noexcept {
    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));
    const Bitboard empty_squares = ~board.occupied();

    return legal_moves_in_direction<shift_east>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_west>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_north>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_south>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_northeast>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_northwest>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_southeast>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_southwest>(own_discs, opponent_discs, empty_squares);
}

bool has_legal_move(const Board& board) noexcept {
    return legal_moves(board) != 0;
}

Bitboard flips_for_move(const Board& board, Square square) noexcept {
    const Bitboard move_bit = square.bit();
    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));

    if ((board.occupied() & move_bit) != 0) {
        return 0;
    }

    return flips_in_direction<shift_east>(move_bit, own_discs, opponent_discs) |
           flips_in_direction<shift_west>(move_bit, own_discs, opponent_discs) |
           flips_in_direction<shift_north>(move_bit, own_discs, opponent_discs) |
           flips_in_direction<shift_south>(move_bit, own_discs, opponent_discs) |
           flips_in_direction<shift_northeast>(move_bit, own_discs, opponent_discs) |
           flips_in_direction<shift_northwest>(move_bit, own_discs, opponent_discs) |
           flips_in_direction<shift_southeast>(move_bit, own_discs, opponent_discs) |
           flips_in_direction<shift_southwest>(move_bit, own_discs, opponent_discs);
}

std::optional<Board> apply_move(const Board& board, Square square) noexcept {
    const Bitboard flips = flips_for_move(board, square);
    if (flips == 0) {
        return std::nullopt;
    }

    const Bitboard move_bit = square.bit();
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
