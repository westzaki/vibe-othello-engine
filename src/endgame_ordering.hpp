#pragma once

#include "hash_detail.hpp"
#include "search_common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <optional>
#include <othello/endgame.hpp>
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
    int singleton_region_bonus = 12'000;
    int odd_region_bonus = 4'000;
    int even_region_penalty = 1'000;
    int opponent_corner_penalty = 80'000;
    int opponent_mobility_penalty = 800;
    int opponent_pass_bonus = 30'000;
};

constexpr EndgameMoveOrderingParams default_move_ordering_params{};

struct EndgameMoveOrderingPolicy {
    EndgameMoveOrderingParams params = default_move_ordering_params;
    bool use_empty_region_parity = false;
};

struct EmptyRegionSizes {
    std::array<int, 64> by_square{};
};

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

[[nodiscard]] inline bool has_legal_move_after_forced_pass(const Board& board,
                                                           Bitboard moves) noexcept {
    if (moves != 0) {
        return false;
    }

    Board after_pass = board;
    after_pass.side_to_move = opponent(board.side_to_move);
    return legal_moves(after_pass) != 0;
}

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

[[nodiscard]] inline int empty_region_size_containing(Bitboard empty, int index) noexcept {
    const Bitboard seed = Bitboard{1} << index;
    if ((empty & seed) == 0) {
        return 0;
    }

    Bitboard region = seed;
    std::array<int, 64> stack{};
    std::size_t stack_size = 1;
    stack[0] = index;

    while (stack_size > 0) {
        const int current = stack[--stack_size];
        Bitboard neighbors = orthogonal_neighbors(current) & empty & ~region;
        while (neighbors != 0) {
            const int next = std::countr_zero(neighbors);
            neighbors &= neighbors - 1;
            region |= Bitboard{1} << next;
            stack[stack_size] = next;
            ++stack_size;
        }
    }

    return static_cast<int>(std::popcount(region));
}

[[nodiscard]] inline EmptyRegionSizes empty_region_sizes(Bitboard empty) noexcept {
    EmptyRegionSizes result;
    Bitboard unvisited = empty;
    std::array<int, 64> stack{};

    while (unvisited != 0) {
        const int seed = std::countr_zero(unvisited);
        Bitboard region = Bitboard{1} << seed;
        unvisited &= ~region;

        std::size_t stack_size = 1;
        stack[0] = seed;
        while (stack_size > 0) {
            const int current = stack[--stack_size];
            Bitboard neighbors = orthogonal_neighbors(current) & unvisited;
            while (neighbors != 0) {
                const int next = std::countr_zero(neighbors);
                const Bitboard next_bit = Bitboard{1} << next;
                neighbors &= neighbors - 1;
                unvisited &= ~next_bit;
                region |= next_bit;
                stack[stack_size] = next;
                ++stack_size;
            }
        }

        const int region_size = static_cast<int>(std::popcount(region));
        Bitboard remaining_region = region;
        while (remaining_region != 0) {
            const int index = std::countr_zero(remaining_region);
            remaining_region &= remaining_region - 1;
            result.by_square[static_cast<std::size_t>(index)] = region_size;
        }
    }

    return result;
}

[[nodiscard]] inline int
empty_region_parity_order_score(int region_size,
                                const EndgameMoveOrderingParams& params) noexcept {
    if (region_size <= 0) {
        return 0;
    }

    if (region_size == 1) {
        return params.singleton_region_bonus;
    }

    int score = 0;
    if (region_size % 2 != 0) {
        score += params.odd_region_bonus;
    } else {
        score -= params.even_region_penalty;
    }
    return score;
}

[[nodiscard]] inline int move_order_score(const Board& board, int index, const Board& next,
                                          const EndgameMoveOrderingParams& params,
                                          const EmptyRegionSizes* empty_regions) noexcept {
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
    if (empty_regions != nullptr) {
        score += empty_region_parity_order_score(
            empty_regions->by_square[static_cast<std::size_t>(index)], params);
    }

    const Bitboard opponent_moves = legal_moves(next);
    if ((opponent_moves & corner_squares) != 0) {
        score -= params.opponent_corner_penalty;
    }
    // Keep the count as an int to match the rest of the search code's scoring arithmetic.
    // NOLINTNEXTLINE(readability-redundant-casting)
    const int opponent_move_count = static_cast<int>(std::popcount(opponent_moves));
    score -= opponent_move_count * params.opponent_mobility_penalty;

    if (has_legal_move_after_forced_pass(next, opponent_moves)) {
        score += params.opponent_pass_bonus;
    }

    return score;
}

inline void sort_ordered_moves(OrderedMoveIndexes& candidates) noexcept {
    std::sort(candidates.moves.begin(), candidates.moves.begin() + candidates.size,
              [](const OrderedMoveIndexes::Move& lhs, const OrderedMoveIndexes::Move& rhs) {
                  if (lhs.order_score != rhs.order_score) {
                      return lhs.order_score > rhs.order_score;
                  }
                  return lhs.index < rhs.index;
              });
}

[[nodiscard]] inline OrderedMoveIndexes
ordered_legal_move_indexes(const Board& board, ZobristHash hash, Bitboard moves,
                           const EndgameMoveOrderingPolicy& policy) noexcept {
    OrderedMoveIndexes result;
    const EmptyRegionSizes empty_regions = policy.use_empty_region_parity
                                               ? empty_region_sizes(board.empty())
                                               : EmptyRegionSizes{};
    const EmptyRegionSizes* empty_regions_ptr =
        policy.use_empty_region_parity ? &empty_regions : nullptr;

    while (moves != 0) {
        const int index = std::countr_zero(moves);
        moves &= moves - 1;

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
            .order_score = move_order_score(board, index, next, policy.params,
                                            empty_regions_ptr),
            .hash = next_hash,
            .next = next,
        };
        ++result.size;
    }

    sort_ordered_moves(result);

    return result;
}

[[nodiscard]] inline bool promote_preferred_move(OrderedMoveIndexes& candidates,
                                                 Square preferred_move) noexcept {
    const int preferred_index = preferred_move.index();
    for (std::size_t index = 0; index < candidates.size; ++index) {
        if (candidates.moves[index].index != preferred_index) {
            continue;
        }

        const OrderedMoveIndexes::Move preferred = candidates.moves[index];
        for (std::size_t shift = index; shift > 0; --shift) {
            candidates.moves[shift] = candidates.moves[shift - 1];
        }
        candidates.moves[0] = preferred;
        return true;
    }

    return false;
}

[[nodiscard]] inline OrderedMoveIndexes
ordered_legal_move_indexes(const Board& board, ZobristHash hash, Bitboard moves,
                           std::optional<Square> tt_preferred_move,
                           ExactEndgameStats& stats,
                           const EndgameMoveOrderingPolicy& policy) noexcept {
    OrderedMoveIndexes ordered_moves = ordered_legal_move_indexes(board, hash, moves, policy);

    if (tt_preferred_move.has_value() && (moves & tt_preferred_move->bit()) != 0 &&
        promote_preferred_move(ordered_moves, *tt_preferred_move)) {
        ++stats.tt_move_ordering_used;
    }

    return ordered_moves;
}

} // namespace othello::endgame_detail
