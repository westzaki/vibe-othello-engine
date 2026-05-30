#pragma once

#include <othello/types.hpp>

namespace othello::bitboard_detail {

inline constexpr Bitboard a_file = 0x0101010101010101ULL;
inline constexpr Bitboard h_file = 0x8080808080808080ULL;
inline constexpr Bitboard not_a_file = ~a_file;
inline constexpr Bitboard not_h_file = ~h_file;

inline constexpr Bitboard corner_squares =
    (Bitboard{1} << 0) | (Bitboard{1} << 7) | (Bitboard{1} << 56) | (Bitboard{1} << 63);

[[nodiscard]] constexpr Bitboard shift_east(Bitboard bits) noexcept {
    return (bits & not_h_file) << 1;
}

[[nodiscard]] constexpr Bitboard shift_west(Bitboard bits) noexcept {
    return (bits & not_a_file) >> 1;
}

[[nodiscard]] constexpr Bitboard shift_north(Bitboard bits) noexcept {
    return bits << 8;
}

[[nodiscard]] constexpr Bitboard shift_south(Bitboard bits) noexcept {
    return bits >> 8;
}

[[nodiscard]] constexpr Bitboard shift_northeast(Bitboard bits) noexcept {
    return (bits & not_h_file) << 9;
}

[[nodiscard]] constexpr Bitboard shift_northwest(Bitboard bits) noexcept {
    return (bits & not_a_file) << 7;
}

[[nodiscard]] constexpr Bitboard shift_southeast(Bitboard bits) noexcept {
    return (bits & not_h_file) >> 7;
}

[[nodiscard]] constexpr Bitboard shift_southwest(Bitboard bits) noexcept {
    return (bits & not_a_file) >> 9;
}

[[nodiscard]] constexpr Bitboard adjacent_squares(Bitboard bits) noexcept {
    return shift_east(bits) | shift_west(bits) | shift_north(bits) | shift_south(bits) |
           shift_northeast(bits) | shift_northwest(bits) | shift_southeast(bits) |
           shift_southwest(bits);
}

[[nodiscard]] constexpr bool is_corner(int index) noexcept {
    return index == 0 || index == 7 || index == 56 || index == 63;
}

[[nodiscard]] constexpr bool is_x_square(int index) noexcept {
    return index == 9 || index == 14 || index == 49 || index == 54;
}

[[nodiscard]] constexpr bool is_edge(int index) noexcept {
    const int file = index % 8;
    const int rank = index / 8;
    return file == 0 || file == 7 || rank == 0 || rank == 7;
}

[[nodiscard]] constexpr bool is_x_square_next_to_empty_corner(int index,
                                                              Bitboard occupied) noexcept {
    switch (index) {
    case 9:
        return (occupied & (Bitboard{1} << 0)) == 0;
    case 14:
        return (occupied & (Bitboard{1} << 7)) == 0;
    case 49:
        return (occupied & (Bitboard{1} << 56)) == 0;
    case 54:
        return (occupied & (Bitboard{1} << 63)) == 0;
    default:
        return false;
    }
}

[[nodiscard]] constexpr Bitboard orthogonal_neighbors(int index) noexcept {
    const Bitboard square = Bitboard{1} << index;
    return shift_east(square) | shift_west(square) | shift_north(square) | shift_south(square);
}

} // namespace othello::bitboard_detail
