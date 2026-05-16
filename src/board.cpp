#include <array>
#include <bit>
#include <othello/board.hpp>

namespace othello {
namespace {

struct Direction {
    int file_delta;
    int rank_delta;
};

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

[[nodiscard]] std::optional<std::string_view> take_line(std::string_view& text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    const std::size_t newline = text.find('\n');
    if (newline == std::string_view::npos) {
        const std::string_view line = text;
        text = {};
        return line;
    }

    std::string_view line = text;
    line.remove_suffix(text.size() - newline);
    text.remove_prefix(newline + 1);
    return line;
}

[[nodiscard]] bool parse_board_row(std::string_view row, int rank, Board& board) noexcept {
    if (row.size() != 8) {
        return false;
    }

    for (int file = 0; file < 8; ++file) {
        const Bitboard bit = bit_at(file, rank);

        switch (row[static_cast<std::size_t>(file)]) {
        case 'B':
            board.black |= bit;
            break;
        case 'W':
            board.white |= bit;
            break;
        case '.':
            break;
        default:
            return false;
        }
    }

    return true;
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

Bitboard legal_moves(const Board& board) noexcept {
    Bitboard moves = 0;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const auto square = Square::from_index(index);
        const Bitboard bit = square->bit();

        if (flips_for_move(board, *square) != 0) {
            moves |= bit;
        }
    }

    return moves;
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

std::optional<Board> board_from_string(std::string_view text) noexcept {
    Board board{};

    for (int row_index = 0; row_index < 8; ++row_index) {
        const std::optional<std::string_view> row = take_line(text);
        if (!row.has_value() || !parse_board_row(*row, 7 - row_index, board)) {
            return std::nullopt;
        }
    }

    const std::optional<std::string_view> side_line = take_line(text);
    if (!side_line.has_value()) {
        return std::nullopt;
    }

    if (*side_line == "side=B") {
        board.side_to_move = Side::Black;
    } else if (*side_line == "side=W") {
        board.side_to_move = Side::White;
    } else {
        return std::nullopt;
    }

    if (!text.empty()) {
        return std::nullopt;
    }

    return board;
}

std::string to_string(const Board& board) {
    std::string text;
    text.reserve(78);

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            const Bitboard bit = bit_at(file, rank);

            if ((board.black & bit) != 0) {
                text += 'B';
            } else if ((board.white & bit) != 0) {
                text += 'W';
            } else {
                text += '.';
            }
        }

        text += '\n';
    }

    text += board.side_to_move == Side::Black ? "side=B" : "side=W";
    return text;
}

} // namespace othello
