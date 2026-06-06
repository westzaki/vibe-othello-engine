#pragma once

#include "bitboard_ops.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello::search_detail {

struct SearchPosition {
    Bitboard player = 0;
    Bitboard opponent_discs = 0;
    Side side_to_move = Side::Black;

    [[nodiscard]] static constexpr SearchPosition from_board(const Board& board) noexcept {
        return SearchPosition{
            .player = board.discs(board.side_to_move),
            .opponent_discs = board.discs(opponent(board.side_to_move)),
            .side_to_move = board.side_to_move,
        };
    }

    [[nodiscard]] constexpr Board to_board() const noexcept {
        if (side_to_move == Side::Black) {
            return Board{
                .black = player,
                .white = opponent_discs,
                .side_to_move = side_to_move,
            };
        }
        return Board{
            .black = opponent_discs,
            .white = player,
            .side_to_move = side_to_move,
        };
    }

    [[nodiscard]] constexpr Bitboard occupied() const noexcept {
        return player | opponent_discs;
    }

    [[nodiscard]] constexpr Bitboard empty() const noexcept {
        return ~occupied();
    }
};

struct PrincipalVariation {
    std::array<int, 64> indexes{};
    std::size_t size = 0;
};

struct NodeResult {
    std::optional<Square> best_move;
    int score = 0;
    PrincipalVariation principal_variation;
};

[[nodiscard]] inline int empty_count(const Board& board) noexcept {
    return std::popcount(board.empty());
}

[[nodiscard]] inline int empty_count(const SearchPosition& position) noexcept {
    return std::popcount(position.empty());
}

template <Bitboard (*Shift)(Bitboard) noexcept>
[[nodiscard]] inline Bitboard legal_moves_in_direction(Bitboard player, Bitboard opponent_discs,
                                                       Bitboard empty_squares) noexcept {
    Bitboard captured = Shift(player) & opponent_discs;

    for (int step = 0; step < 5; ++step) {
        captured |= Shift(captured) & opponent_discs;
    }

    return Shift(captured) & empty_squares;
}

[[nodiscard]] inline Bitboard legal_moves(const SearchPosition& position) noexcept {
    using bitboard_detail::shift_east;
    using bitboard_detail::shift_north;
    using bitboard_detail::shift_northeast;
    using bitboard_detail::shift_northwest;
    using bitboard_detail::shift_south;
    using bitboard_detail::shift_southeast;
    using bitboard_detail::shift_southwest;
    using bitboard_detail::shift_west;

    const Bitboard empty_squares = position.empty();
    return legal_moves_in_direction<shift_east>(position.player, position.opponent_discs,
                                                empty_squares) |
           legal_moves_in_direction<shift_west>(position.player, position.opponent_discs,
                                                empty_squares) |
           legal_moves_in_direction<shift_north>(position.player, position.opponent_discs,
                                                 empty_squares) |
           legal_moves_in_direction<shift_south>(position.player, position.opponent_discs,
                                                 empty_squares) |
           legal_moves_in_direction<shift_northeast>(position.player, position.opponent_discs,
                                                     empty_squares) |
           legal_moves_in_direction<shift_northwest>(position.player, position.opponent_discs,
                                                     empty_squares) |
           legal_moves_in_direction<shift_southeast>(position.player, position.opponent_discs,
                                                     empty_squares) |
           legal_moves_in_direction<shift_southwest>(position.player, position.opponent_discs,
                                                     empty_squares);
}

template <Bitboard (*Shift)(Bitboard) noexcept>
[[nodiscard]] inline Bitboard flips_in_direction(Bitboard move_bit, Bitboard player,
                                                 Bitboard opponent_discs) noexcept {
    Bitboard flips = 0;
    Bitboard captured = Shift(move_bit) & opponent_discs;

    while (captured != 0) {
        flips |= captured;
        const Bitboard next = Shift(captured);
        if ((next & player) != 0) {
            return flips;
        }

        captured = next & opponent_discs;
    }

    return 0;
}

[[nodiscard]] inline Bitboard flips_for_move(const SearchPosition& position,
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
    if ((position.occupied() & move_bit) != 0) {
        return 0;
    }

    return flips_in_direction<shift_east>(move_bit, position.player,
                                          position.opponent_discs) |
           flips_in_direction<shift_west>(move_bit, position.player,
                                          position.opponent_discs) |
           flips_in_direction<shift_north>(move_bit, position.player,
                                           position.opponent_discs) |
           flips_in_direction<shift_south>(move_bit, position.player,
                                           position.opponent_discs) |
           flips_in_direction<shift_northeast>(move_bit, position.player,
                                               position.opponent_discs) |
           flips_in_direction<shift_northwest>(move_bit, position.player,
                                               position.opponent_discs) |
           flips_in_direction<shift_southeast>(move_bit, position.player,
                                               position.opponent_discs) |
           flips_in_direction<shift_southwest>(move_bit, position.player,
                                               position.opponent_discs);
}

[[nodiscard]] inline SearchPosition position_after_move(const SearchPosition& position,
                                                        Square square,
                                                        Bitboard flips) noexcept {
    const Bitboard move_bit = square.bit();
    const Bitboard next_opponent = position.player | move_bit | flips;
    const Bitboard next_player = position.opponent_discs & ~flips;
    return SearchPosition{
        .player = next_player,
        .opponent_discs = next_opponent,
        .side_to_move = opponent(position.side_to_move),
    };
}

[[nodiscard]] inline SearchPosition
position_after_pass(const SearchPosition& position) noexcept {
    return SearchPosition{
        .player = position.opponent_discs,
        .opponent_discs = position.player,
        .side_to_move = opponent(position.side_to_move),
    };
}

[[nodiscard]] inline int score_for_player(const SearchPosition& position) noexcept {
    return std::popcount(position.player) - std::popcount(position.opponent_discs);
}

[[nodiscard]] inline bool is_better_best_move(int candidate_score, Square candidate,
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

[[nodiscard]] inline PrincipalVariation
principal_variation_with_move(Square move, const PrincipalVariation& child_variation) noexcept {
    PrincipalVariation principal_variation;
    principal_variation.indexes[0] = move.index();
    principal_variation.size = 1;

    const std::size_t child_size =
        std::min(child_variation.size, principal_variation.indexes.size() - 1);
    for (std::size_t index = 0; index < child_size; ++index) {
        principal_variation.indexes[index + 1] = child_variation.indexes[index];
    }
    principal_variation.size += child_size;

    return principal_variation;
}

[[nodiscard]] inline std::vector<Square>
principal_variation_to_vector(const PrincipalVariation& principal_variation) noexcept {
    std::vector<Square> squares;
    try {
        squares.reserve(principal_variation.size);
        for (std::size_t index = 0; index < principal_variation.size; ++index) {
            const std::optional<Square> square =
                Square::from_index(principal_variation.indexes[index]);
            if (square.has_value()) {
                squares.push_back(*square);
            }
        }
    } catch (...) {
        return {};
    }
    return squares;
}

[[nodiscard]] inline PrincipalVariation
principal_variation_from_vector(const std::vector<Square>& principal_variation) noexcept {
    PrincipalVariation result;
    const std::size_t size = std::min(principal_variation.size(), result.indexes.size());
    for (std::size_t index = 0; index < size; ++index) {
        result.indexes[index] = principal_variation[index].index();
    }
    result.size = size;
    return result;
}

[[nodiscard]] inline std::optional<Square> square_from_transposition_index(int index) noexcept {
    if (index < Square::min_index || index > Square::max_index) {
        return std::nullopt;
    }
    return Square::from_index(index);
}

[[nodiscard]] inline NodeResult node_result_from_transposition_entry(int score,
                                                                     int best_move_index) noexcept {
    const std::optional<Square> best_move = square_from_transposition_index(best_move_index);
    NodeResult result{
        .best_move = best_move,
        .score = score,
    };

    // TT entries intentionally store only a score and best move, not a full line.
    // A cached result therefore reports the conservative PV fragment we know.
    if (best_move.has_value()) {
        result.principal_variation.indexes[0] = best_move->index();
        result.principal_variation.size = 1;
    }
    return result;
}

} // namespace othello::search_detail
