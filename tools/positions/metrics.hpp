#pragma once

#include <cstdint>
#include <othello/othello.hpp>
#include <string_view>

namespace othello::benchmarks {

struct EndgamePositionMetrics {
    int empties = 0;
    int score_current = 0;
    int legal_moves_current = 0;
    int legal_moves_opponent = 0;

    bool root_pass = false;
    bool game_over = false;

    int empty_corner_count = 0;
    int legal_corner_count = 0;
    int opponent_legal_corner_count = 0;

    int edge_empty_count = 0;
    int legal_edge_count = 0;

    int x_square_legal_risk_count = 0;

    int empty_region_count = 0;
    int odd_region_count = 0;
    int even_region_count = 0;
    int singleton_region_count = 0;
    int largest_region_size = 0;
};

[[nodiscard]] bool same_board(const Board& left, const Board& right) noexcept;
[[nodiscard]] int empty_count(const Board& board) noexcept;
[[nodiscard]] int legal_move_count(const Board& board) noexcept;
[[nodiscard]] int count_bits(Bitboard bits) noexcept;
[[nodiscard]] constexpr Bitboard corner_bits() noexcept {
    return (Bitboard{1} << 0) | (Bitboard{1} << 7) | (Bitboard{1} << 56) |
           (Bitboard{1} << 63);
}
[[nodiscard]] constexpr Bitboard edge_bits() noexcept {
    return 0xFF818181818181FFULL;
}
[[nodiscard]] constexpr Bitboard x_square_bits() noexcept {
    return (Bitboard{1} << 9) | (Bitboard{1} << 14) | (Bitboard{1} << 49) |
           (Bitboard{1} << 54);
}
[[nodiscard]] constexpr Bitboard corner_for_x_square(Bitboard x_square) noexcept {
    switch (x_square) {
    case Bitboard{1} << 9:
        return Bitboard{1} << 0;
    case Bitboard{1} << 14:
        return Bitboard{1} << 7;
    case Bitboard{1} << 49:
        return Bitboard{1} << 56;
    case Bitboard{1} << 54:
        return Bitboard{1} << 63;
    default:
        return 0;
    }
}
[[nodiscard]] constexpr bool is_corner_index(int index) noexcept {
    return index == 0 || index == 7 || index == 56 || index == 63;
}
[[nodiscard]] constexpr bool is_edge_index(int index) noexcept {
    const int file = index % 8;
    const int rank = index / 8;
    return file == 0 || file == 7 || rank == 0 || rank == 7;
}
[[nodiscard]] constexpr bool is_x_square_next_to_empty_corner(int index,
                                                              Bitboard empty) noexcept {
    switch (index) {
    case 9:
        return (empty & (Bitboard{1} << 0)) != 0;
    case 14:
        return (empty & (Bitboard{1} << 7)) != 0;
    case 49:
        return (empty & (Bitboard{1} << 56)) != 0;
    case 54:
        return (empty & (Bitboard{1} << 63)) != 0;
    default:
        return false;
    }
}
[[nodiscard]] bool has_x_square_risk(const Board& board, Bitboard legal_moves) noexcept;
[[nodiscard]] EndgamePositionMetrics compute_endgame_metrics(const Board& board) noexcept;

} // namespace othello::benchmarks
