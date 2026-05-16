#pragma once

#include <optional>
#include <othello/types.hpp>
#include <string>
#include <string_view>

namespace othello {

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

} // namespace othello
