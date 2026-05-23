#include "reference_rules.hpp"

#include <array>
#include <bit>

namespace othello::test::reference {
namespace {

struct Direction {
    int file_delta = 0;
    int rank_delta = 0;
};

constexpr std::array<Direction, 8> directions{{
    {.file_delta = 1, .rank_delta = 0},
    {.file_delta = -1, .rank_delta = 0},
    {.file_delta = 0, .rank_delta = 1},
    {.file_delta = 0, .rank_delta = -1},
    {.file_delta = 1, .rank_delta = 1},
    {.file_delta = -1, .rank_delta = 1},
    {.file_delta = 1, .rank_delta = -1},
    {.file_delta = -1, .rank_delta = -1},
}};

[[nodiscard]] constexpr bool is_on_board(int file, int rank) noexcept {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

[[nodiscard]] constexpr int square_index(int file, int rank) noexcept {
    return (rank * 8) + file;
}

[[nodiscard]] Square square_from_index_unchecked(int index) noexcept {
    return *Square::from_index(index);
}

[[nodiscard]] bool has_disc(const Board& board, Side side, Square square) noexcept {
    return (board.discs(side) & square.bit()) != 0;
}

[[nodiscard]] Bitboard flips_in_direction(const Board& board, Square move,
                                          Direction direction) noexcept {
    const Side side = board.side_to_move;
    const Side other = opponent(side);

    int file = move.file() + direction.file_delta;
    int rank = move.rank() + direction.rank_delta;
    Bitboard captured = 0;
    bool saw_opponent = false;

    while (is_on_board(file, rank)) {
        const Square square = square_from_index_unchecked(square_index(file, rank));

        if (has_disc(board, other, square)) {
            saw_opponent = true;
            captured |= square.bit();
        } else if (has_disc(board, side, square)) {
            return saw_opponent ? captured : 0;
        } else {
            return 0;
        }

        file += direction.file_delta;
        rank += direction.rank_delta;
    }

    return 0;
}

[[nodiscard]] Board with_side_to_move(Board board, Side side) noexcept {
    board.side_to_move = side;
    return board;
}

} // namespace

Bitboard flips_for_move(const Board& board, Square square) noexcept {
    if ((board.occupied() & square.bit()) != 0) {
        return 0;
    }

    Bitboard flips = 0;
    for (const Direction direction : directions) {
        flips |= flips_in_direction(board, square, direction);
    }
    return flips;
}

Bitboard legal_moves(const Board& board) noexcept {
    Bitboard moves = 0;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Square square = square_from_index_unchecked(index);
        if (((board.empty() & square.bit()) != 0) &&
            othello::test::reference::flips_for_move(board, square) != 0) {
            moves |= square.bit();
        }
    }

    return moves;
}

std::optional<Board> apply_move(const Board& board, Square square) noexcept {
    const Bitboard flips = othello::test::reference::flips_for_move(board, square);
    if (flips == 0) {
        return std::nullopt;
    }

    Board next = board;
    const Bitboard placed_disc = square.bit();

    if (board.side_to_move == Side::Black) {
        next.black |= placed_disc | flips;
        next.white &= ~flips;
    } else {
        next.white |= placed_disc | flips;
        next.black &= ~flips;
    }

    next.side_to_move = opponent(board.side_to_move);
    return next;
}

std::optional<Board> pass_turn(const Board& board) noexcept {
    if (othello::test::reference::legal_moves(board) != 0) {
        return std::nullopt;
    }

    Board next = with_side_to_move(board, opponent(board.side_to_move));
    if (othello::test::reference::legal_moves(next) == 0) {
        return std::nullopt;
    }

    return next;
}

bool is_game_over(const Board& board) noexcept {
    if (othello::test::reference::legal_moves(board) != 0) {
        return false;
    }

    const Board next = with_side_to_move(board, opponent(board.side_to_move));
    return othello::test::reference::legal_moves(next) == 0;
}

int disc_count(const Board& board, Side side) noexcept {
    return std::popcount(board.discs(side));
}

int score(const Board& board, Side side) noexcept {
    return othello::test::reference::disc_count(board, side) -
           othello::test::reference::disc_count(board, opponent(side));
}

} // namespace othello::test::reference
