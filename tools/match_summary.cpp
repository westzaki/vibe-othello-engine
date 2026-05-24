#include "match_summary/json_cursor.hpp"
#include "match_summary/parser.hpp"
#include "match_summary/summary.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliOptions {
    std::string input_path;
    bool by_opening = false;
    bool allow_errors = false;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " --input PATH [--by-opening] [--allow-errors] [--format text]\n"
              << '\n'
              << "Options:\n"
              << "  --input PATH     match runner JSONL file\n"
              << "  --by-opening     print per-opening summary rows\n"
              << "  --allow-errors   return success even when records contain illegal_or_error=true\n"
              << "  --format text    output format\n"
              << "  --help           show this help text\n";
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

[[nodiscard]] std::optional<CliOptions> parse_options(std::span<char* const> args,
                                                      bool& help_requested) {
    CliOptions options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view arg{args[index]};
        if (arg == "--help") {
            help_requested = true;
            return options;
        }

        if (arg == "--input") {
            const auto value = next_argument(args, index, arg);
            if (!value.has_value() || value->empty()) {
                std::cerr << "invalid --input value\n";
                return std::nullopt;
            }
            options.input_path = std::string{*value};
        } else if (arg == "--by-opening") {
            options.by_opening = true;
        } else if (arg == "--allow-errors") {
            options.allow_errors = true;
        } else if (arg == "--format") {
            const auto value = next_argument(args, index, arg);
            if (!value.has_value() || *value != "text") {
                std::cerr << "invalid --format value; only text is supported\n";
                return std::nullopt;
            }
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            return std::nullopt;
        }
    }

    if (options.input_path.empty()) {
        std::cerr << "missing required option: --input PATH\n";
        return std::nullopt;
    }

    return options;
}

[[nodiscard]] bool is_blank_line(std::string_view line) noexcept {
    for (const char character : line) {
        if (!othello::match_summary::detail::is_ascii_space(character)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<othello::match_summary::GameRecord>>
load_records(const std::filesystem::path& input_path) {
    std::ifstream input{input_path};
    if (!input) {
        std::cerr << "failed to open input file: " << input_path << '\n';
        return std::nullopt;
    }

    std::vector<othello::match_summary::GameRecord> records;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (is_blank_line(line)) {
            continue;
        }

        const othello::match_summary::ParseResult parsed =
            othello::match_summary::parse_game_record(line);
        if (!parsed.ok) {
            std::cerr << input_path << ':' << line_number << ": " << parsed.error << '\n';
            return std::nullopt;
        }
        records.push_back(parsed.record);
    }

    if (!input.eof()) {
        std::cerr << "failed to read input file: " << input_path << '\n';
        return std::nullopt;
    }
    if (records.empty()) {
        std::cerr << "input file contains no records: " << input_path << '\n';
        return std::nullopt;
    }

    return records;
}

[[nodiscard]] std::string spec_label(std::span<const std::string> specs) {
    if (specs.empty()) {
        return "-";
    }
    if (specs.size() == 1) {
        return specs.front();
    }
    return "mixed (" + std::to_string(specs.size()) + " unique)";
}

void print_summary(const std::filesystem::path& input_path,
                   const othello::match_summary::Summary& summary, bool by_opening) {
    std::cout << "input: " << input_path.string() << '\n';
    std::cout << "games: " << summary.games << '\n';
    std::cout << "valid games: " << summary.valid_games << '\n';
    std::cout << "error games: " << summary.error_games << '\n';
    std::cout << "player A spec: " << spec_label(summary.player_a_specs) << '\n';
    std::cout << "player B spec: " << spec_label(summary.player_b_specs) << '\n';
    std::cout << "A wins: " << summary.player_a_wins << '\n';
    std::cout << "B wins: " << summary.player_b_wins << '\n';
    std::cout << "draws: " << summary.draws << '\n';
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "A win rate: " << (summary.player_a_win_rate * 100.0) << "%\n";
    std::cout << "B win rate: " << (summary.player_b_win_rate * 100.0) << "%\n";
    std::cout << "average disc diff from A perspective: "
              << summary.average_disc_diff_from_player_a << '\n';
    std::cout << "average plies: " << summary.average_plies << '\n';
    std::cout << "average passes: " << summary.average_passes << '\n';
    std::cout << "unique openings: " << summary.unique_openings_count << '\n';

    if (!by_opening) {
        return;
    }

    std::cout << '\n';
    std::cout << "by opening:\n";
    std::cout << std::left << std::setw(8) << "index" << std::setw(24) << "name" << std::right
              << std::setw(8) << "games" << std::setw(8) << "errors" << std::setw(8)
              << "A wins" << std::setw(8) << "B wins" << std::setw(8) << "draws"
              << std::setw(12) << "avg diff" << '\n';
    for (const othello::match_summary::OpeningSummary& opening : summary.openings) {
        std::cout << std::left << std::setw(8) << opening.opening_index << std::setw(24)
                  << opening.opening_name << std::right << std::setw(8) << opening.games
                  << std::setw(8) << opening.error_games << std::setw(8)
                  << opening.player_a_wins << std::setw(8)
                  << opening.player_b_wins << std::setw(8) << opening.draws << std::setw(12)
                  << opening.average_disc_diff_from_player_a << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    bool help_requested = false;
    const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
    const std::optional<CliOptions> options = parse_options(args, help_requested);

    if (help_requested) {
        print_usage(args.empty() ? "othello_match_summary" : args[0]);
        return 0;
    }
    if (!options.has_value()) {
        print_usage(args.empty() ? "othello_match_summary" : args[0]);
        return 2;
    }

    const std::optional<std::vector<othello::match_summary::GameRecord>> records =
        load_records(options->input_path);
    if (!records.has_value()) {
        return 2;
    }

    const othello::match_summary::Summary summary = othello::match_summary::summarize(*records);
    print_summary(options->input_path, summary, options->by_opening);

    if (summary.error_games > 0 && !options->allow_errors) {
        return 1;
    }
    return 0;
}
