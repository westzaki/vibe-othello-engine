#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace othello {

using Bitboard = std::uint64_t;

enum class Side {
    Black,
    White,
};

[[nodiscard]] constexpr Side opponent(Side side) noexcept {
    return side == Side::Black ? Side::White : Side::Black;
}

class Square {
  public:
    static constexpr int min_index = 0;
    static constexpr int max_index = 63;

    [[nodiscard]] static constexpr std::optional<Square> from_index(int index) noexcept {
        if (index < min_index || index > max_index) {
            return std::nullopt;
        }

        return Square(index);
    }

    [[nodiscard]] constexpr int index() const noexcept {
        return index_;
    }
    [[nodiscard]] constexpr int file() const noexcept {
        return index_ % 8;
    }
    [[nodiscard]] constexpr int rank() const noexcept {
        return index_ / 8;
    }
    [[nodiscard]] constexpr Bitboard bit() const noexcept {
        return Bitboard{1} << index_;
    }

    [[nodiscard]] friend constexpr bool operator==(Square lhs, Square rhs) noexcept = default;

  private:
    explicit constexpr Square(int index) noexcept : index_(index) {}

    int index_;
};

[[nodiscard]] std::optional<Square> square_from_string(std::string_view coordinate) noexcept;
[[nodiscard]] std::string to_string(Square square);

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

[[nodiscard]] Bitboard legal_moves(const Board& board) noexcept;

} // namespace othello
