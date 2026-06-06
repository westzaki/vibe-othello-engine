#pragma once

#include "bitboard_ops.hpp"

#include <othello/square.hpp>
#include <othello/types.hpp>

namespace othello::rules_detail {

template <Bitboard (*Shift)(Bitboard) noexcept>
[[nodiscard]] inline Bitboard legal_moves_in_direction(Bitboard player, Bitboard opponent,
                                                       Bitboard empty_squares) noexcept {
    Bitboard captured = Shift(player) & opponent;

    for (int step = 0; step < 5; ++step) {
        captured |= Shift(captured) & opponent;
    }

    return Shift(captured) & empty_squares;
}

[[nodiscard]] inline Bitboard legal_moves_for(Bitboard player, Bitboard opponent) noexcept {
    using bitboard_detail::shift_east;
    using bitboard_detail::shift_north;
    using bitboard_detail::shift_northeast;
    using bitboard_detail::shift_northwest;
    using bitboard_detail::shift_south;
    using bitboard_detail::shift_southeast;
    using bitboard_detail::shift_southwest;
    using bitboard_detail::shift_west;

    const Bitboard empty_squares = ~(player | opponent);
    return legal_moves_in_direction<shift_east>(player, opponent, empty_squares) |
           legal_moves_in_direction<shift_west>(player, opponent, empty_squares) |
           legal_moves_in_direction<shift_north>(player, opponent, empty_squares) |
           legal_moves_in_direction<shift_south>(player, opponent, empty_squares) |
           legal_moves_in_direction<shift_northeast>(player, opponent, empty_squares) |
           legal_moves_in_direction<shift_northwest>(player, opponent, empty_squares) |
           legal_moves_in_direction<shift_southeast>(player, opponent, empty_squares) |
           legal_moves_in_direction<shift_southwest>(player, opponent, empty_squares);
}

template <Bitboard (*Shift)(Bitboard) noexcept>
[[nodiscard]] inline Bitboard flips_in_direction(Bitboard move_bit, Bitboard player,
                                                 Bitboard opponent) noexcept {
    Bitboard flips = 0;
    Bitboard captured = Shift(move_bit) & opponent;

    while (captured != 0) {
        flips |= captured;
        const Bitboard next = Shift(captured);
        if ((next & player) != 0) {
            return flips;
        }

        captured = next & opponent;
    }

    return 0;
}

[[nodiscard]] inline Bitboard flips_for_move_for(Bitboard player, Bitboard opponent,
                                                 Square square) noexcept {
    using bitboard_detail::shift_east;
    using bitboard_detail::shift_north;
    using bitboard_detail::shift_northeast;
    using bitboard_detail::shift_northwest;
    using bitboard_detail::shift_south;
    using bitboard_detail::shift_southeast;
    using bitboard_detail::shift_southwest;
    using bitboard_detail::shift_west;

    const Bitboard move_bit = square.bit();
    if (((player | opponent) & move_bit) != 0) {
        return 0;
    }

    return flips_in_direction<shift_east>(move_bit, player, opponent) |
           flips_in_direction<shift_west>(move_bit, player, opponent) |
           flips_in_direction<shift_north>(move_bit, player, opponent) |
           flips_in_direction<shift_south>(move_bit, player, opponent) |
           flips_in_direction<shift_northeast>(move_bit, player, opponent) |
           flips_in_direction<shift_northwest>(move_bit, player, opponent) |
           flips_in_direction<shift_southeast>(move_bit, player, opponent) |
           flips_in_direction<shift_southwest>(move_bit, player, opponent);
}

} // namespace othello::rules_detail
