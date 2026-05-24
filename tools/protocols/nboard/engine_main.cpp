#include "common/cli.hpp"
#include "protocols/nboard/game_codec.hpp"

#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>

namespace {

struct Options {
    int depth = 4;
    bool verbose = false;
    bool help = false;
};

void print_usage(std::string_view program) {
    std::cout << "usage: " << program << " [--depth N] [--verbose] [--help]\n";
}

[[nodiscard]] std::optional<Options> parse_options(std::span<char* const> args) {
    Options options;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view arg{args[index]};
        if (arg == "--help") {
            options.help = true;
            return options;
        }
        if (arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "--depth") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto depth =
                value.has_value() ? othello::tools::parse_positive_int(*value) : std::nullopt;
            if (!depth.has_value()) {
                std::cerr << "invalid --depth value\n";
                return std::nullopt;
            }
            options.depth = *depth;
            continue;
        }
        std::cerr << "unknown option: " << arg << '\n';
        return std::nullopt;
    }
    return options;
}

[[nodiscard]] std::optional<std::string_view> command_tail(std::string_view line,
                                                           std::string_view command) {
    if (!line.starts_with(command)) {
        return std::nullopt;
    }
    if (line.size() == command.size()) {
        return std::string_view{};
    }
    if (line[command.size()] != ' ') {
        return std::nullopt;
    }
    line.remove_prefix(command.size() + 1);
    return line;
}

[[nodiscard]] std::optional<othello::Square> choose_move(const othello::Board& board, int depth) {
    const othello::SearchOptions options{
        .max_depth = depth,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 12,
        .use_pvs = true,
    };
    const othello::SearchResult result = othello::search(board, options);
    if (result.best_move.has_value()) {
        return result.best_move;
    }

    const othello::Bitboard moves = othello::legal_moves(board);
    for (int index = othello::Square::min_index; index <= othello::Square::max_index; ++index) {
        const auto square = othello::Square::from_index(index);
        if (square.has_value() && (moves & square->bit()) != 0) {
            return square;
        }
    }
    return std::nullopt;
}

int run_engine(Options options) {
    othello::Board board = othello::Board::initial();
    std::string line;

    while (std::getline(std::cin, line)) {
        line = othello::tools::nboard::trim_ascii(line);
        if (options.verbose) {
            std::cerr << "recv: " << line << '\n';
        }

        if (line == "quit" || line == "exit") {
            break;
        }
        if (line.starts_with("nboard")) {
            std::cout << "feature ping=1 setboard=0 done=1\n" << std::flush;
            continue;
        }
        if (const auto tail = command_tail(line, "ping"); tail.has_value()) {
            std::cout << "pong " << *tail << '\n' << std::flush;
            continue;
        }
        if (const auto tail = command_tail(line, "set depth"); tail.has_value()) {
            const auto depth = othello::tools::parse_positive_int(*tail);
            if (depth.has_value()) {
                options.depth = *depth;
            }
            continue;
        }
        if (const auto tail = command_tail(line, "set game"); tail.has_value()) {
            const std::string tail_text = othello::tools::nboard::trim_ascii(*tail);
            const auto parsed =
                tail_text.starts_with("(;") ? othello::tools::nboard::parse_ggf_game(tail_text)
                                            : othello::tools::nboard::parse_move_list(tail_text);
            if (parsed.ok) {
                board = parsed.board;
            }
            continue;
        }
        const auto usermove_tail = command_tail(line, "usermove");
        const auto move_tail = command_tail(line, "move");
        if (usermove_tail.has_value() || move_tail.has_value()) {
            const std::string_view tail = usermove_tail.has_value() ? *usermove_tail : *move_tail;
            const auto parsed = othello::tools::nboard::parse_move_token(tail);
            if (parsed.has_value()) {
                const auto next = parsed->pass ? othello::pass_turn(board)
                                               : othello::apply_move(board, *parsed->square);
                if (next.has_value()) {
                    board = *next;
                }
            }
            continue;
        }
        if (line == "go") {
            const auto move = choose_move(board, options.depth);
            if (!move.has_value()) {
                if (othello::pass_turn(board).has_value()) {
                    std::cout << "=== pass\n" << std::flush;
                } else {
                    std::cout << "=== pass\n" << std::flush;
                }
                continue;
            }
            std::cout << "=== " << othello::to_string(*move) << '\n' << std::flush;
            continue;
        }
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
    const auto options = parse_options(args);
    if (!options.has_value()) {
        print_usage(args.empty() ? "othello_nboard_engine" : args.front());
        return 2;
    }
    if (options->help) {
        print_usage(args.empty() ? "othello_nboard_engine" : args.front());
        return 0;
    }
    return run_engine(*options);
}
