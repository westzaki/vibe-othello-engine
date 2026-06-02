#include "analyze/analysis.hpp"
#include "common/board_io.hpp"
#include "common/cli.hpp"
#include "common/evaluator_cli.hpp"
#include "common/evaluator_selection.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>

namespace {

using Clock = std::chrono::steady_clock;
using AnalysisMode = othello::tools::analyze::AnalysisMode;
using AnalysisOptions = othello::tools::analyze::AnalysisOptions;

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " (--board-file PATH | --stdin) [--depth N] [--mode fixed|iterative]"
                 " [--tt on|off] [--tt-entries N] [--tt-store-leaf on|off] [--pvs on|off]"
                 " [--aspiration on|off] [--aspiration-window N]"
                 " [--aspiration-max-researches N] [--exact-endgame-threshold N]"
                 " "
              << othello::tools::evaluator_cli_usage() << " [--root-candidates]\n"
              << '\n'
              << "Options:\n"
              << "  --board-file PATH  read a board in board_from_string format\n"
              << "  --stdin            read a board in board_from_string format from stdin\n"
              << "  --depth N          non-negative search depth (default: 10)\n"
              << "  --mode MODE        fixed or iterative (default: fixed)\n"
              << "  --tt on|off        enable or disable transposition table (default: on)\n"
              << "  --tt-entries N     requested transposition table entry count\n"
              << "  --tt-store-leaf on|off\n"
              << "                    store depth-0 midgame heuristic leaves in TT (default: on)\n"
              << "  --pvs on|off       enable or disable PVS (default: off)\n"
              << "  --aspiration on|off\n"
              << "                    enable iterative-search aspiration windows (default: off)\n"
              << "  --aspiration-window N\n"
              << "                    positive initial aspiration half-window\n"
              << "  --aspiration-max-researches N\n"
              << "                    non-negative aspiration widening retries before full-window "
                 "fallback\n"
              << "  --exact-endgame-threshold N\n"
              << "                    solve root positions with at most N empties exactly; N <= 0 "
                 "disables\n"
              << othello::tools::evaluator_cli_help()
              << "  --root-candidates  analyze each legal root move separately\n"
              << "  --help             show this help text\n";
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

[[nodiscard]] std::optional<AnalysisOptions> parse_options(std::span<char* const> args,
                                                           bool& help_requested) {
    AnalysisOptions options;
    othello::tools::EvaluatorCliParseState evaluator_cli;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view arg{args[index]};

        if (arg == "--help") {
            help_requested = true;
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
        } else if (arg == "--depth") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto depth =
                value.has_value() ? othello::tools::parse_non_negative_int(*value) : std::nullopt;
            if (!depth.has_value()) {
                std::cerr << "invalid --depth value\n";
                return std::nullopt;
            }
            options.depth = *depth;
        } else if (arg == "--mode") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto mode = value.has_value() ? parse_mode(*value) : std::nullopt;
            if (!mode.has_value()) {
                std::cerr << "invalid --mode value\n";
                return std::nullopt;
            }
            options.mode = *mode;
        } else if (arg == "--tt") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto tt = value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!tt.has_value()) {
                std::cerr << "invalid --tt value\n";
                return std::nullopt;
            }
            options.use_transposition_table = *tt;
        } else if (arg == "--tt-entries") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto entries =
                value.has_value() ? othello::tools::parse_entry_count(*value) : std::nullopt;
            if (!entries.has_value()) {
                std::cerr << "invalid --tt-entries value\n";
                return std::nullopt;
            }
            options.transposition_table_entries = *entries;
        } else if (arg == "--tt-store-leaf") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto store_leaf =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!store_leaf.has_value()) {
                std::cerr << "invalid --tt-store-leaf value\n";
                return std::nullopt;
            }
            options.store_leaf_tt_entries = *store_leaf;
        } else if (arg == "--pvs") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto pvs =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!pvs.has_value()) {
                std::cerr << "invalid --pvs value\n";
                return std::nullopt;
            }
            options.use_pvs = *pvs;
        } else if (arg == "--aspiration") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto aspiration =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!aspiration.has_value()) {
                std::cerr << "invalid --aspiration value\n";
                return std::nullopt;
            }
            options.use_aspiration_window = *aspiration;
        } else if (arg == "--aspiration-window") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto window =
                value.has_value() ? othello::tools::parse_positive_int(*value) : std::nullopt;
            if (!window.has_value()) {
                std::cerr << "invalid --aspiration-window value\n";
                return std::nullopt;
            }
            options.aspiration_window = *window;
        } else if (arg == "--aspiration-max-researches") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto researches =
                value.has_value() ? othello::tools::parse_non_negative_int(*value) : std::nullopt;
            if (!researches.has_value()) {
                std::cerr << "invalid --aspiration-max-researches value\n";
                return std::nullopt;
            }
            options.aspiration_max_researches = *researches;
        } else if (arg == "--exact-endgame-threshold") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto threshold =
                value.has_value() ? othello::tools::parse_int(*value) : std::nullopt;
            if (!threshold.has_value()) {
                std::cerr << "invalid --exact-endgame-threshold value\n";
                return std::nullopt;
            }
            options.exact_endgame_empty_threshold = *threshold;
        } else if (arg == "--eval-config") {
            std::string evaluator_cli_error;
            if (othello::tools::parse_evaluator_cli_option(args, index, evaluator_cli,
                                                           evaluator_cli_error) ==
                othello::tools::EvaluatorCliParseResult::Error) {
                std::cerr << evaluator_cli_error << '\n';
                return std::nullopt;
            }
        } else if (arg == "--root-candidates" || arg == "--root-breakdown") {
            options.root_candidates = true;
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

    std::string evaluator_error;
    const std::optional<othello::tools::EvaluatorSelection> evaluator =
        othello::tools::parse_evaluator_selection(evaluator_cli.input, evaluator_error);
    if (!evaluator.has_value()) {
        std::cerr << evaluator_error << '\n';
        return std::nullopt;
    }
    options.evaluator = *evaluator;

    return options;
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

    const std::optional<std::string> board_text =
        options->read_stdin ? othello::tools::read_stdin_text()
                            : othello::tools::read_text_file(*options->board_file);
    if (!board_text.has_value()) {
        return 1;
    }

    const std::optional<othello::Board> board = othello::board_from_string(*board_text);
    if (!board.has_value()) {
        std::cerr << "invalid board input: expected 8 board rows followed by side=B or side=W\n";
        return 1;
    }

    const auto start = Clock::now();
    const othello::SearchResult result = othello::tools::analyze::run_search(*board, *options);
    const auto end = Clock::now();

    othello::tools::analyze::print_report(*board, *options, result, end - start);
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
