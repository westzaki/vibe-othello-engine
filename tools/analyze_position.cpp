#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class AnalysisMode {
    Fixed,
    Iterative,
};

struct AnalysisOptions {
    std::optional<std::string> board_file;
    bool read_stdin = false;
    int depth = 10;
    AnalysisMode mode = AnalysisMode::Fixed;
    bool use_transposition_table = true;
    std::size_t transposition_table_entries = othello::SearchOptions{}.transposition_table_entries;
    int exact_endgame_empty_threshold = othello::SearchOptions{}.exact_endgame_empty_threshold;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " (--board-file PATH | --stdin) [--depth N] [--mode fixed|iterative]"
                 " [--tt on|off] [--tt-entries N] [--exact-endgame-threshold N]\n"
              << '\n'
              << "Options:\n"
              << "  --board-file PATH  read a board in board_from_string format\n"
              << "  --stdin            read a board in board_from_string format from stdin\n"
              << "  --depth N          non-negative search depth (default: 10)\n"
              << "  --mode MODE        fixed or iterative (default: fixed)\n"
              << "  --tt on|off        enable or disable transposition table (default: on)\n"
              << "  --tt-entries N     requested transposition table entry count\n"
              << "  --exact-endgame-threshold N\n"
              << "                    solve root positions with at most N empties exactly; N <= 0 "
                 "disables\n"
              << "  --help             show this help text\n";
}

[[nodiscard]] std::optional<int> parse_non_negative_int(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<int> parse_int(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::size_t> parse_entry_count(std::string_view text) noexcept {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end ||
        value > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::optional<AnalysisMode> parse_mode(std::string_view text) noexcept {
    if (text == "fixed") {
        return AnalysisMode::Fixed;
    }
    if (text == "iterative") {
        return AnalysisMode::Iterative;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<bool> parse_tt(std::string_view text) noexcept {
    if (text == "on") {
        return true;
    }
    if (text == "off") {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view mode_name(AnalysisMode mode) noexcept {
    switch (mode) {
    case AnalysisMode::Fixed:
        return "fixed";
    case AnalysisMode::Iterative:
        return "iterative";
    }

    return "unknown";
}

[[nodiscard]] std::string_view side_name(othello::Side side) noexcept {
    return side == othello::Side::Black ? "black" : "white";
}

[[nodiscard]] std::optional<std::string_view>
next_argument(std::span<char* const> args, std::size_t& index, std::string_view option) {
    if (index + 1 >= args.size()) {
        std::cerr << "missing value for " << option << '\n';
        return std::nullopt;
    }

    ++index;
    return std::string_view{args[index]};
}

[[nodiscard]] std::optional<AnalysisOptions> parse_options(std::span<char* const> args,
                                                           bool& help_requested) {
    AnalysisOptions options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view arg{args[index]};

        if (arg == "--help") {
            help_requested = true;
            return options;
        }

        if (arg == "--board-file") {
            const auto value = next_argument(args, index, arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.board_file = std::string{*value};
        } else if (arg == "--stdin") {
            options.read_stdin = true;
        } else if (arg == "--depth") {
            const auto value = next_argument(args, index, arg);
            const auto depth = value.has_value() ? parse_non_negative_int(*value) : std::nullopt;
            if (!depth.has_value()) {
                std::cerr << "invalid --depth value\n";
                return std::nullopt;
            }
            options.depth = *depth;
        } else if (arg == "--mode") {
            const auto value = next_argument(args, index, arg);
            const auto mode = value.has_value() ? parse_mode(*value) : std::nullopt;
            if (!mode.has_value()) {
                std::cerr << "invalid --mode value\n";
                return std::nullopt;
            }
            options.mode = *mode;
        } else if (arg == "--tt") {
            const auto value = next_argument(args, index, arg);
            const auto tt = value.has_value() ? parse_tt(*value) : std::nullopt;
            if (!tt.has_value()) {
                std::cerr << "invalid --tt value\n";
                return std::nullopt;
            }
            options.use_transposition_table = *tt;
        } else if (arg == "--tt-entries") {
            const auto value = next_argument(args, index, arg);
            const auto entries = value.has_value() ? parse_entry_count(*value) : std::nullopt;
            if (!entries.has_value()) {
                std::cerr << "invalid --tt-entries value\n";
                return std::nullopt;
            }
            options.transposition_table_entries = *entries;
        } else if (arg == "--exact-endgame-threshold") {
            const auto value = next_argument(args, index, arg);
            const auto threshold = value.has_value() ? parse_int(*value) : std::nullopt;
            if (!threshold.has_value()) {
                std::cerr << "invalid --exact-endgame-threshold value\n";
                return std::nullopt;
            }
            options.exact_endgame_empty_threshold = *threshold;
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

    return options;
}

[[nodiscard]] std::optional<std::string> read_board_text(const AnalysisOptions& options) {
    if (options.read_stdin) {
        return std::string{std::istreambuf_iterator<char>{std::cin},
                           std::istreambuf_iterator<char>{}};
    }

    std::ifstream input{*options.board_file};
    if (!input) {
        std::cerr << "failed to open board file: " << *options.board_file << '\n';
        return std::nullopt;
    }

    std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input.good() && !input.eof()) {
        std::cerr << "failed to read board file: " << *options.board_file << '\n';
        return std::nullopt;
    }

    return text;
}

[[nodiscard]] std::string format_square(std::optional<othello::Square> square) {
    if (!square.has_value()) {
        return "-";
    }
    return othello::to_string(*square);
}

[[nodiscard]] std::string format_moves(othello::Bitboard moves) {
    std::string text;
    for (int index = othello::Square::min_index; index <= othello::Square::max_index; ++index) {
        const auto square = othello::Square::from_index(index);
        if (!square.has_value() || (moves & square->bit()) == 0) {
            continue;
        }

        if (!text.empty()) {
            text += ' ';
        }
        text += othello::to_string(*square);
    }

    return text.empty() ? "-" : text;
}

[[nodiscard]] std::string
format_principal_variation(const std::vector<othello::Square>& principal_variation) {
    std::string text;
    for (const othello::Square square : principal_variation) {
        if (!text.empty()) {
            text += "->";
        }
        text += othello::to_string(square);
    }

    return text.empty() ? "-" : text;
}

[[nodiscard]] othello::SearchResult run_search(const othello::Board& board,
                                               const AnalysisOptions& options) noexcept {
    const othello::SearchOptions search_options{
        .max_depth = options.depth,
        .use_transposition_table = options.use_transposition_table,
        .transposition_table_entries = options.transposition_table_entries,
        .exact_endgame_empty_threshold = options.exact_endgame_empty_threshold,
    };

    if (options.mode == AnalysisMode::Fixed) {
        return othello::search(board, search_options);
    }
    return othello::search_iterative(board, search_options);
}

void print_report(const othello::Board& board, const AnalysisOptions& options,
                  const othello::SearchResult& result, std::chrono::nanoseconds elapsed) {
    const othello::Bitboard moves = othello::legal_moves(board);
    const bool no_legal_moves = moves == 0;
    const bool pass_available = othello::pass_turn(board).has_value();
    const bool game_over = othello::is_game_over(board);
    const double elapsed_ms = std::chrono::duration<double, std::milli>{elapsed}.count();

    std::cout << "Othello position analysis\n"
              << '\n'
              << "input_board:\n"
              << othello::to_string(board) << '\n'
              << '\n'
              << "side_to_move: " << side_name(board.side_to_move) << '\n'
              << "legal_moves: " << format_moves(moves) << '\n'
              << "mode: " << mode_name(options.mode) << '\n'
              << "depth: " << options.depth << '\n'
              << "tt: " << (options.use_transposition_table ? "on" : "off") << '\n'
              << "tt_entries: " << options.transposition_table_entries << '\n'
              << "exact_endgame_threshold: " << options.exact_endgame_empty_threshold << '\n'
              << "elapsed_ms: " << std::fixed << std::setprecision(3) << elapsed_ms << '\n'
              << "best_move: " << format_square(result.best_move) << '\n'
              << "score: " << result.score << '\n'
              << "nodes: " << result.nodes << '\n'
              << "principal_variation: " << format_principal_variation(result.principal_variation)
              << '\n'
              << "game_over: " << (game_over ? "yes" : "no") << '\n'
              << "no_legal_moves: " << (no_legal_moves ? "yes" : "no") << '\n'
              << "pass_available: " << (pass_available ? "yes" : "no") << '\n';
}

int run_analysis(std::span<char* const> args) {
    bool help_requested = false;
    const std::optional<AnalysisOptions> options = parse_options(args, help_requested);

    if (help_requested) {
        print_usage(args.empty() ? "othello_analyze_position" : args.front());
        return 0;
    }

    if (!options.has_value()) {
        print_usage(args.empty() ? "othello_analyze_position" : args.front());
        return 1;
    }

    const std::optional<std::string> board_text = read_board_text(*options);
    if (!board_text.has_value()) {
        return 1;
    }

    const std::optional<othello::Board> board = othello::board_from_string(*board_text);
    if (!board.has_value()) {
        std::cerr << "invalid board input: expected 8 board rows followed by side=B or side=W\n";
        return 1;
    }

    const auto start = Clock::now();
    const othello::SearchResult result = run_search(*board, *options);
    const auto end = Clock::now();

    print_report(*board, *options, result, end - start);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run_analysis(std::span<char* const>{argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& exception) {
        std::cerr << "analysis failed: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "analysis failed with an unknown exception\n";
    }

    return 1;
}
