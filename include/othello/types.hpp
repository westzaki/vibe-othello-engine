#pragma once

#include <cstdint>

namespace othello {

using Bitboard = std::uint64_t;

enum class Side {
    Black,
    White,
};

[[nodiscard]] constexpr Side opponent(Side side) noexcept {
    return side == Side::Black ? Side::White : Side::Black;
}

} // namespace othello
