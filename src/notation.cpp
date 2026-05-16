#include <othello/notation.hpp>

namespace othello {
namespace {

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

} // namespace

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
