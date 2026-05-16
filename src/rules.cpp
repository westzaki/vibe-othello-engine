#include <array>
#include <bit>
#include <othello/rules.hpp>

namespace othello {
namespace {

struct Direction {
    int file_delta;
    int rank_delta;
};

constexpr Bitboard a_file = 0x0101010101010101ULL;
constexpr Bitboard h_file = 0x8080808080808080ULL;
constexpr Bitboard not_a_file = ~a_file;
constexpr Bitboard not_h_file = ~h_file;

constexpr std::array<Direction, 8> directions{{
    {.file_delta = -1, .rank_delta = -1},
    {.file_delta = 0, .rank_delta = -1},
    {.file_delta = 1, .rank_delta = -1},
    {.file_delta = -1, .rank_delta = 0},
    {.file_delta = 1, .rank_delta = 0},
    {.file_delta = -1, .rank_delta = 1},
    {.file_delta = 0, .rank_delta = 1},
    {.file_delta = 1, .rank_delta = 1},
}};

[[nodiscard]] constexpr bool on_board(int file, int rank) noexcept {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

[[nodiscard]] constexpr int square_index(int file, int rank) noexcept {
    return (rank * 8) + file;
}

[[nodiscard]] constexpr Bitboard bit_at(int file, int rank) noexcept {
    return Bitboard{1} << square_index(file, rank);
}

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

template <Bitboard (*Shift)(Bitboard) noexcept>
[[nodiscard]] Bitboard legal_moves_in_direction(Bitboard own_discs, Bitboard opponent_discs,
                                                Bitboard empty_squares) noexcept {
    Bitboard captured = Shift(own_discs) & opponent_discs;

    for (int step = 0; step < 5; ++step) {
        captured |= Shift(captured) & opponent_discs;
    }

    return Shift(captured) & empty_squares;
}

[[nodiscard]] Bitboard flips_in_direction(Square square, Direction direction, Bitboard own_discs,
                                          Bitboard opponent_discs) noexcept {
    int file = square.file() + direction.file_delta;
    int rank = square.rank() + direction.rank_delta;
    Bitboard flips = 0;

    while (on_board(file, rank)) {
        const Bitboard bit = bit_at(file, rank);

        if ((opponent_discs & bit) != 0) {
            flips |= bit;
            file += direction.file_delta;
            rank += direction.rank_delta;
            continue;
        }

        if ((own_discs & bit) != 0) {
            return flips;
        }

        return 0;
    }

    return 0;
}

} // namespace

Bitboard legal_moves(const Board& board) noexcept {
    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));
    const Bitboard empty_squares = ~board.occupied();

    return legal_moves_in_direction<shift_east>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_west>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_north>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_south>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_northeast>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_northwest>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_southeast>(own_discs, opponent_discs, empty_squares) |
           legal_moves_in_direction<shift_southwest>(own_discs, opponent_discs, empty_squares);
}

bool has_legal_move(const Board& board) noexcept {
    return legal_moves(board) != 0;
}

Bitboard flips_for_move(const Board& board, Square square) noexcept {
    const Bitboard move_bit = square.bit();
    const Bitboard own_discs = board.discs(board.side_to_move);
    const Bitboard opponent_discs = board.discs(opponent(board.side_to_move));

    if ((board.occupied() & move_bit) != 0) {
        return 0;
    }

    Bitboard flips = 0;
    for (const Direction direction : directions) {
        flips |= flips_in_direction(square, direction, own_discs, opponent_discs);
    }

    return flips;
}

std::optional<Board> apply_move(const Board& board, Square square) noexcept {
    const Bitboard flips = flips_for_move(board, square);
    if (flips == 0) {
        return std::nullopt;
    }

    const Bitboard move_bit = square.bit();
    Board next = board;

    if (board.side_to_move == Side::Black) {
        next.black |= move_bit | flips;
        next.white &= ~flips;
    } else {
        next.white |= move_bit | flips;
        next.black &= ~flips;
    }

    next.side_to_move = opponent(board.side_to_move);
    return next;
}

std::optional<Board> pass_turn(const Board& board) noexcept {
    if (has_legal_move(board)) {
        return std::nullopt;
    }

    Board next = board;
    next.side_to_move = opponent(board.side_to_move);
    if (!has_legal_move(next)) {
        return std::nullopt;
    }

    return next;
}

bool is_game_over(const Board& board) noexcept {
    if (has_legal_move(board)) {
        return false;
    }

    Board opponent_board = board;
    opponent_board.side_to_move = opponent(board.side_to_move);
    return !has_legal_move(opponent_board);
}

int disc_count(const Board& board, Side side) noexcept {
    return std::popcount(board.discs(side));
}

int score(const Board& board, Side side) noexcept {
    return disc_count(board, side) - disc_count(board, opponent(side));
}

} // namespace othello
