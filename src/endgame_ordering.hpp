#pragma once

#include "hash_detail.hpp"
#include "search_common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <optional>
#include <othello/hash.hpp>
#include <othello/rules.hpp>

namespace othello::endgame_detail {

using search_detail::board_after_move;
using search_detail::corner_squares;
using search_detail::is_corner;
using search_detail::is_edge;
using search_detail::is_x_square_next_to_empty_corner;

struct OrderedMoveIndexes {
    struct Move {
        int index = 0;
        int order_score = 0;
        ZobristHash hash = 0;
        Board next;
    };

    std::array<Move, 64> moves{};
    std::size_t size = 0;
};

struct EndgameMoveOrderingParams {
    int corner_bonus = 100'000;
    int edge_bonus = 1'000;
    int x_square_empty_corner_penalty = 30'000;
    int opponent_corner_penalty = 80'000;
    int opponent_mobility_penalty = 800;
    int opponent_pass_bonus = 30'000;
};

constexpr EndgameMoveOrderingParams default_move_ordering_params{};

[[nodiscard]] inline ZobristHash hash_after_pass(ZobristHash hash, const Board& board) noexcept {
    hash ^= detail::zobrist_side_hash(board.side_to_move);
    hash ^= detail::zobrist_side_hash(opponent(board.side_to_move));
    return hash;
}

[[nodiscard]] inline ZobristHash hash_after_move(ZobristHash hash, const Board& board,
                                                 Square square, Bitboard flips) noexcept {
    const Side side = board.side_to_move;
    const Side other = opponent(side);
    hash ^= detail::zobrist_side_hash(side);
    hash ^= detail::zobrist_side_hash(other);
    hash ^= detail::zobrist_piece_hash(side, square.index());

    while (flips != 0) {
        const int index = std::countr_zero(flips);
        flips &= flips - 1;
        hash ^= detail::zobrist_piece_hash(other, index);
        hash ^= detail::zobrist_piece_hash(side, index);
    }

    return hash;
}

[[nodiscard]] inline int move_order_score(const Board& board, int index, const Board& next,
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

[[nodiscard]] inline OrderedMoveIndexes
ordered_legal_move_indexes(const Board& board, ZobristHash hash, Bitboard moves,
                           const std::optional<Square>& preferred_move = std::nullopt) noexcept {
    OrderedMoveIndexes result;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Bitboard move_bit = Bitboard{1} << index;
        if ((moves & move_bit) != 0) {
            const std::optional<Square> square = Square::from_index(index);
            if (!square.has_value()) {
                continue;
            }

            const Bitboard flips = flips_for_move(board, *square);
            if (flips == 0) {
                continue;
            }
            const Board next = board_after_move(board, *square, flips);
            const ZobristHash next_hash = hash_after_move(hash, board, *square, flips);
#ifndef NDEBUG
            assert(next_hash == zobrist_hash(next));
#endif

            result.moves[result.size] = OrderedMoveIndexes::Move{
                .index = index,
                .order_score = move_order_score(board, index, next, default_move_ordering_params),
                .hash = next_hash,
                .next = next,
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

    if (preferred_move.has_value()) {
        const Bitboard preferred_bit = Bitboard{1} << preferred_move->index();
        if ((moves & preferred_bit) != 0) {
            auto first = result.moves.begin();
            auto last = result.moves.begin() + result.size;
            auto preferred = std::find_if(first, last,
                                          [preferred_index = preferred_move->index()](
                                              const OrderedMoveIndexes::Move& move) noexcept {
                                              return move.index == preferred_index;
                                          });
            if (preferred != last) {
                std::rotate(first, preferred, preferred + 1);
            }
        }
    }

    return result;
}

} // namespace othello::endgame_detail
