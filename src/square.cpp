#include <othello/square.hpp>

namespace othello {
namespace {

[[nodiscard]] constexpr int square_index(int file, int rank) noexcept {
    return (rank * 8) + file;
}

} // namespace

std::optional<Square> square_from_string(std::string_view coordinate) noexcept {
    if (coordinate.size() != 2) {
        return std::nullopt;
    }

    const char file_char = coordinate[0];
    const char rank_char = coordinate[1];

    if (file_char < 'a' || file_char > 'h' || rank_char < '1' || rank_char > '8') {
        return std::nullopt;
    }

    const int file = file_char - 'a';
    const int rank = rank_char - '1';
    return Square::from_index(square_index(file, rank));
}

std::string to_string(Square square) {
    std::string coordinate;
    coordinate += static_cast<char>('a' + square.file());
    coordinate += static_cast<char>('1' + square.rank());
    return coordinate;
}

} // namespace othello
