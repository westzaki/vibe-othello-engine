#pragma once

#include <othello/types.hpp>

namespace othello {

struct Board {
    Bitboard black = 0;
    Bitboard white = 0;
    Side side_to_move = Side::Black;

    [[nodiscard]] static constexpr Board initial() noexcept {
        return Board{
            .black = (Bitboard{1} << 28) | (Bitboard{1} << 35),
            .white = (Bitboard{1} << 27) | (Bitboard{1} << 36),
            .side_to_move = Side::Black,
        };
    }

    [[nodiscard]] constexpr Bitboard discs(Side side) const noexcept {
        return side == Side::Black ? black : white;
    }

    [[nodiscard]] constexpr Bitboard occupied() const noexcept {
        return black | white;
    }
    [[nodiscard]] constexpr Bitboard empty() const noexcept {
        return ~occupied();
    }
};

} // namespace othello
