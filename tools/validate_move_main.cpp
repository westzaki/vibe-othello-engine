#include "common/board_io.hpp"
#include "common/cli.hpp"

#include <bit>
#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::optional<std::string> board_file;
    std::string move;
    bool read_stdin = false;
    bool help = false;
};

void print_usage(std::string_view program) {
    std::cout << "usage: " << program << " (--board-file PATH | --stdin) --move MOVE\n"
              << '\n'
              << "Options:\n"
              << "  --board-file PATH  read a board in board_from_string format\n"
              << "  --stdin            read a board in board_from_string format from stdin\n"
              << "  --move MOVE        validate MOVE as a1-h8 or pass\n"
              << "  --help             show this help text\n";
}

[[nodiscard]] std::optional<Options> parse_options(std::span<char* const> args) {
    Options options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view arg{args[index]};
        if (arg == "--help") {
            options.help = true;
            return options;
        }
        if (arg == "--board-file") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.board_file = std::string{*value};
        } else if (arg == "--stdin") {
            options.read_stdin = true;
        } else if (arg == "--move") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value() || value->empty()) {
                return std::nullopt;
            }
            options.move = std::string{*value};
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            return std::nullopt;
        }
    }

    const int input_count = (options.board_file.has_value() ? 1 : 0) + (options.read_stdin ? 1 : 0);
    if (input_count != 1) {
        std::cerr << "choose exactly one input source: --board-file PATH or --stdin\n";
        return std::nullopt;
    }
    if (options.move.empty()) {
        std::cerr << "--move is required\n";
        return std::nullopt;
    }
    return options;
}

[[nodiscard]] std::vector<std::string> legal_move_labels(const othello::Board& board) {
    std::vector<std::string> labels;
    othello::Bitboard moves = othello::legal_moves(board);
    while (moves != 0) {
        const int index = std::countr_zero(moves);
        moves &= moves - 1;
        const auto square = othello::Square::from_index(index);
        if (square.has_value()) {
            labels.push_back(othello::to_string(*square));
        }
    }
    if (labels.empty() && othello::pass_turn(board).has_value()) {
        labels.push_back("pass");
    }
    return labels;
}

[[nodiscard]] bool is_legal_move(const othello::Board& board, std::string_view move) {
    if (move == "pass") {
        return othello::pass_turn(board).has_value();
    }
    const auto square = othello::square_from_string(move);
    return square.has_value() && othello::apply_move(board, *square).has_value();
}

void print_result(bool legal, const std::vector<std::string>& legal_moves, std::string_view error) {
    std::cout << "legal_move_valid=" << (legal ? "true" : "false") << '\n';
    std::cout << "legal_validation_source=othello_validate_move\n";
    std::cout << "legal_moves=";
    for (std::size_t index = 0; index < legal_moves.size(); ++index) {
        if (index > 0) {
            std::cout << ' ';
        }
        std::cout << legal_moves[index];
    }
    std::cout << '\n';
    std::cout << "error=" << (error.empty() ? "-" : error) << '\n';
}

int run(std::span<char* const> args) {
    const auto options = parse_options(args);
    if (!options.has_value()) {
        print_usage(args.empty() ? "othello_validate_move" : args.front());
        return 2;
    }
    if (options->help) {
        print_usage(args.empty() ? "othello_validate_move" : args.front());
        return 0;
    }

    const auto board_text = options->read_stdin
                                ? othello::tools::read_stdin_text()
                                : othello::tools::read_text_file(*options->board_file);
    if (!board_text.has_value()) {
        return 2;
    }

    const auto board = othello::board_from_string(*board_text);
    if (!board.has_value()) {
        std::cerr << "invalid board input: expected 8 board rows followed by side=B or side=W\n";
        return 2;
    }

    const std::vector<std::string> legal_moves = legal_move_labels(*board);
    const bool legal = is_legal_move(*board, options->move);
    print_result(legal, legal_moves, legal ? "" : "illegal move");
    return legal ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    return run(std::span<char* const>{argv, static_cast<std::size_t>(argc)});
}
