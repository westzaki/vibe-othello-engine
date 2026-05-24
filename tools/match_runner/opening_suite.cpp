#include "opening_suite.hpp"

#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <utility>

namespace othello::match_runner {

bool is_ascii_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

std::string_view trim_ascii_space(std::string_view text) noexcept {
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

Opening default_opening() {
    return Opening{.name = "initial", .moves = {}, .start_board = Board::initial()};
}

std::vector<std::string> split_ascii_whitespace(std::string_view text) {
    std::vector<std::string> tokens;

    while (true) {
        text = trim_ascii_space(text);
        if (text.empty()) {
            return tokens;
        }

        std::size_t token_size = 0;
        while (token_size < text.size() && !is_ascii_space(text[token_size])) {
            ++token_size;
        }

        tokens.emplace_back(text.substr(0, token_size));
        text.remove_prefix(token_size);
    }
}

OpeningParseResult parse_opening_line(std::string_view line) {
    line = trim_ascii_space(line);
    if (line.empty() || line.front() == '#') {
        return OpeningParseResult{.ok = true, .has_opening = false};
    }

    const std::size_t separator = line.find(':');
    if (separator == std::string_view::npos) {
        return OpeningParseResult{.error = "missing ':' separator"};
    }

    const std::string_view name = trim_ascii_space(line.substr(0, separator));
    if (name.empty()) {
        return OpeningParseResult{.error = "missing opening name"};
    }

    Opening opening{
        .name = std::string{name},
        .moves = split_ascii_whitespace(line.substr(separator + 1)),
        .start_board = Board::initial(),
    };

    Board board = Board::initial();
    for (const std::string& move_text : opening.moves) {
        if (is_game_over(board)) {
            return OpeningParseResult{.error = "opening has moves after game over"};
        }

        if (!has_legal_move(board)) {
            const std::optional<Board> after_pass = pass_turn(board);
            if (!after_pass.has_value()) {
                return OpeningParseResult{.error = "opening cannot pass from terminal board"};
            }
            board = *after_pass;
        }

        const std::optional<Square> move = square_from_string(move_text);
        if (!move.has_value()) {
            return OpeningParseResult{.error = "invalid move coordinate: " + move_text};
        }

        const std::optional<Board> next = apply_move(board, *move);
        if (!next.has_value()) {
            return OpeningParseResult{.error = "illegal opening move: " + move_text};
        }
        board = *next;
    }

    opening.start_board = board;
    return OpeningParseResult{.ok = true, .has_opening = true, .opening = std::move(opening)};
}

} // namespace othello::match_runner
