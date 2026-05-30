#pragma once

#include "hash_detail.hpp"

#include <bit>
#include <othello/board.hpp>
#include <othello/hash.hpp>
#include <othello/square.hpp>

namespace othello::hash_detail {

[[nodiscard]] inline ZobristHash hash_after_pass(ZobristHash hash,
                                                 Side side_to_move) noexcept {
    hash ^= detail::zobrist_side_hash(side_to_move);
    hash ^= detail::zobrist_side_hash(opponent(side_to_move));
    return hash;
}

[[nodiscard]] inline ZobristHash hash_after_pass(ZobristHash hash,
                                                 const Board& board) noexcept {
    return hash_after_pass(hash, board.side_to_move);
}

[[nodiscard]] inline ZobristHash hash_after_move(ZobristHash hash, Side side_to_move,
                                                 Square square, Bitboard flips) noexcept {
    const Side next_side = opponent(side_to_move);

    hash ^= detail::zobrist_side_hash(side_to_move);
    hash ^= detail::zobrist_side_hash(next_side);
    hash ^= detail::zobrist_piece_hash(side_to_move, square.index());

    while (flips != 0) {
        const int square_index = std::countr_zero(flips);
        hash ^= detail::zobrist_piece_hash(next_side, square_index);
        hash ^= detail::zobrist_piece_hash(side_to_move, square_index);
        flips &= flips - 1;
    }

    return hash;
}

[[nodiscard]] inline ZobristHash hash_after_move(ZobristHash hash, const Board& board,
                                                 Square square, Bitboard flips) noexcept {
    return hash_after_move(hash, board.side_to_move, square, flips);
}

} // namespace othello::hash_detail
