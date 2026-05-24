#include "protocols/nboard/game_codec.hpp"

#include <cctype>
#include <sstream>

namespace othello::tools::nboard {

namespace {

constexpr std::string_view initial_board_ggf =
    "(;GM[Othello]PC[NBoard]PB[VibeBlack]PW[VibeWhite]RE[?]TI[15:00]TY[8]"
    "BO[8 ---------------------------O*------*O--------------------------- *]";

[[nodiscard]] bool starts_move_property(std::string_view text, std::size_t index) {
    if (index + 1 >= text.size()) {
        return false;
    }
    if (text[index] != 'B' && text[index] != 'W') {
        return false;
    }
    if (text[index + 1] != '[') {
        return false;
    }
    return index == 0 || !std::isalpha(static_cast<unsigned char>(text[index - 1]));
}

} // namespace

MoveListParseResult parse_move_list(std::string_view moves_text) {
    MoveListParseResult result{.ok = true};
    Board board = Board::initial();
    for (const std::string_view token : split_ascii_words(moves_text)) {
        const auto parsed = parse_move_token(token);
        if (!parsed.has_value()) {
            return MoveListParseResult{.ok = false, .error = "invalid move token"};
        }

        if (parsed->pass) {
            const auto next = pass_turn(board);
            if (!next.has_value()) {
                return MoveListParseResult{.ok = false, .error = "illegal pass"};
            }
            board = *next;
            result.moves.push_back("pass");
            continue;
        }

        const auto next = apply_move(board, *parsed->square);
        if (!next.has_value()) {
            return MoveListParseResult{.ok = false, .error = "illegal move"};
        }
        board = *next;
        result.moves.push_back(parsed->text);
    }

    result.board = board;
    return result;
}

MoveListParseResult parse_ggf_game(std::string_view ggf_text) {
    std::vector<std::string> moves;
    for (std::size_t index = 0; index < ggf_text.size(); ++index) {
        if (!starts_move_property(ggf_text, index)) {
            continue;
        }

        const std::size_t value_begin = index + 2;
        const std::size_t value_end = ggf_text.find(']', value_begin);
        if (value_end == std::string_view::npos) {
            return MoveListParseResult{.ok = false, .error = "unterminated GGF move"};
        }
        moves.emplace_back(trim_ascii(ggf_text.substr(value_begin, value_end - value_begin)));
        index = value_end;
    }

    std::ostringstream move_text;
    for (std::size_t index = 0; index < moves.size(); ++index) {
        if (index > 0) {
            move_text << ' ';
        }
        move_text << moves[index];
    }
    return parse_move_list(move_text.str());
}

std::string format_ggf_game(const std::vector<std::string>& moves) {
    std::ostringstream output;
    output << initial_board_ggf;
    for (std::size_t index = 0; index < moves.size(); ++index) {
        output << (index % 2 == 0 ? "B[" : "W[");
        if (moves[index] == "pass") {
            output << "PA";
        } else {
            for (const char value : moves[index]) {
                output << static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
            }
        }
        output << ']';
    }
    output << ";)";
    return output.str();
}

std::string format_set_game_command(const std::vector<std::string>& moves) {
    std::ostringstream output;
    output << "set game " << format_ggf_game(moves);
    return output.str();
}

bool is_legal_response(const Board& board, const NBoardMove& move) noexcept {
    if (move.pass) {
        return pass_turn(board).has_value();
    }
    return move.square.has_value() && apply_move(board, *move.square).has_value();
}

} // namespace othello::tools::nboard
