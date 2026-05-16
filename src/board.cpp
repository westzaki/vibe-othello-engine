#include <othello/board.hpp>

#include <array>

namespace othello {
namespace {

struct Direction {
    int file_delta;
    int rank_delta;
};

constexpr std::array<Direction, 8> directions{{
    {-1, -1},
    {0, -1},
    {1, -1},
    {-1, 0},
    {1, 0},
    {-1, 1},
    {0, 1},
    {1, 1},
}};

[[nodiscard]] constexpr bool on_board(int file, int rank) noexcept
{
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

[[nodiscard]] constexpr int square_index(int file, int rank) noexcept
{
    return rank * 8 + file;
}

[[nodiscard]] constexpr Bitboard bit_at(int file, int rank) noexcept
{
    return Bitboard{1} << square_index(file, rank);
}

[[nodiscard]] bool is_legal_move_in_direction(
    Square square,
    Direction direction,
    Bitboard own_discs,
    Bitboard opponent_discs) noexcept
{
    int file = square.file() + direction.file_delta;
    int rank = square.rank() + direction.rank_delta;
    bool saw_opponent_disc = false;

    while (on_board(file, rank)) {
        const Bitboard bit = bit_at(file, rank);

        if ((opponent_discs & bit) != 0) {
            saw_opponent_disc = true;
            file += direction.file_delta;
            rank += direction.rank_delta;
            continue;
        }

        return saw_opponent_disc && (own_discs & bit) != 0;
    }

    return false;
}

} // namespace

std::optional<Square> square_from_string(std::string_view coordinate) noexcept
{
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

std::string to_string(Square square)
{
    std::string coordinate;
    coordinate += static_cast<char>('a' + square.file());
    coordinate += static_cast<char>('1' + square.rank());
    return coordinate;
}

Bitboard legal_moves(const Board& board) noexcept
{
    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));
    const Bitboard empty_squares = ~(own_discs | opponent_discs);
    Bitboard moves = 0;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const auto square = Square::from_index(index);
        const Bitboard bit = square->bit();

        if ((empty_squares & bit) == 0) {
            continue;
        }

        for (const Direction direction : directions) {
            if (is_legal_move_in_direction(*square, direction, own_discs, opponent_discs)) {
                moves |= bit;
                break;
            }
        }
    }

    return moves;
}

} // namespace othello
