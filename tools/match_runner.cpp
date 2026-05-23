#include "match_runner_core.hpp"

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
    othello::match_runner::PlayerSpec black;
    othello::match_runner::PlayerSpec white;
    int games = 1;
    bool swap_sides = false;
    std::uint64_t seed = 1;
    std::string output_path;
    bool quiet = false;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " --black SPEC --white SPEC --games N --swap-sides true|false --seed N"
                 " --output PATH [--format jsonl] [--quiet]\n"
              << '\n'
              << "Player specs:\n"
              << "  first\n"
              << "  random\n"
              << "  eval\n"
              << "  search:depth=N\n"
              << '\n'
              << "Options:\n"
              << "  --black SPEC        initial black / player A spec\n"
              << "  --white SPEC        initial white / player B spec\n"
              << "  --games N           number of games to run\n"
              << "  --swap-sides BOOL   alternate A/B sides by game when true\n"
              << "  --seed N            base random seed; game i uses seed + i\n"
              << "  --output PATH       JSONL output path\n"
              << "  --format jsonl      output format\n"
              << "  --quiet             suppress stdout summary\n"
              << "  --help              show this help text\n";
}

[[nodiscard]] std::optional<bool> parse_bool(std::string_view text) noexcept {
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    return std::nullopt;
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
    std::optional<othello::match_runner::PlayerSpec> black;
    std::optional<othello::match_runner::PlayerSpec> white;
    CliOptions options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view arg{args[index]};

        if (arg == "--help") {
            help_requested = true;
            return options;
        }

        if (arg == "--quiet") {
            options.quiet = true;
        } else if (arg == "--black") {
            const auto value = next_argument(args, index, arg);
            black = value.has_value() ? othello::match_runner::parse_player_spec(*value)
                                      : std::nullopt;
            if (!black.has_value()) {
                std::cerr << "invalid --black player spec\n";
                return std::nullopt;
            }
        } else if (arg == "--white") {
            const auto value = next_argument(args, index, arg);
            white = value.has_value() ? othello::match_runner::parse_player_spec(*value)
                                      : std::nullopt;
            if (!white.has_value()) {
                std::cerr << "invalid --white player spec\n";
                return std::nullopt;
            }
        } else if (arg == "--games") {
            const auto value = next_argument(args, index, arg);
            const auto games =
                value.has_value() ? othello::match_runner::parse_non_negative_int(*value)
                                  : std::nullopt;
            if (!games.has_value() || *games <= 0) {
                std::cerr << "invalid --games value\n";
                return std::nullopt;
            }
            options.games = *games;
        } else if (arg == "--swap-sides") {
            const auto value = next_argument(args, index, arg);
            const auto swap_sides = value.has_value() ? parse_bool(*value) : std::nullopt;
            if (!swap_sides.has_value()) {
                std::cerr << "invalid --swap-sides value\n";
                return std::nullopt;
            }
            options.swap_sides = *swap_sides;
        } else if (arg == "--seed") {
            const auto value = next_argument(args, index, arg);
            const auto seed =
                value.has_value() ? othello::match_runner::parse_u64(*value) : std::nullopt;
            if (!seed.has_value()) {
                std::cerr << "invalid --seed value\n";
                return std::nullopt;
            }
            options.seed = *seed;
        } else if (arg == "--output") {
            const auto value = next_argument(args, index, arg);
            if (!value.has_value() || value->empty()) {
                std::cerr << "invalid --output value\n";
                return std::nullopt;
            }
            options.output_path = std::string{*value};
        } else if (arg == "--format") {
            const auto value = next_argument(args, index, arg);
            if (!value.has_value() || *value != "jsonl") {
                std::cerr << "invalid --format value; only jsonl is supported\n";
                return std::nullopt;
            }
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            return std::nullopt;
        }
    }

    if (!black.has_value()) {
        std::cerr << "missing required option: --black SPEC\n";
        return std::nullopt;
    }
    if (!white.has_value()) {
        std::cerr << "missing required option: --white SPEC\n";
        return std::nullopt;
    }
    if (options.output_path.empty()) {
        std::cerr << "missing required option: --output PATH\n";
        return std::nullopt;
    }

    options.black = *black;
    options.white = *white;
    return options;
}

[[nodiscard]] std::string json_escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 2);

    for (const char character : text) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}

void write_json_string(std::ostream& output, std::string_view text) {
    output << '"' << json_escape(text) << '"';
}

void write_jsonl_record(std::ostream& output, const othello::match_runner::GameRecord& record) {
    output << "{";
    output << "\"game_index\":" << record.game_index << ',';
    output << "\"seed\":" << record.seed << ',';
    output << "\"black_spec\":";
    write_json_string(output, record.black_spec);
    output << ',';
    output << "\"white_spec\":";
    write_json_string(output, record.white_spec);
    output << ',';
    output << "\"winner\":";
    write_json_string(output, record.winner);
    output << ',';
    output << "\"black_score\":" << record.black_score << ',';
    output << "\"white_score\":" << record.white_score << ',';
    output << "\"score_diff_from_black\":" << record.score_diff_from_black << ',';
    output << "\"plies\":" << record.plies << ',';
    output << "\"passes\":" << record.passes << ',';
    output << "\"moves\":[";
    for (std::size_t index = 0; index < record.moves.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_json_string(output, record.moves[index]);
    }
    output << "],";
    output << "\"illegal_or_error\":" << (record.illegal_or_error ? "true" : "false");
    output << "}\n";
}

[[nodiscard]] bool write_jsonl_file(const std::filesystem::path& output_path,
                                    std::span<const othello::match_runner::GameRecord> records) {
    if (output_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            std::cerr << "failed to create output directory: " << output_path.parent_path()
                      << '\n';
            return false;
        }
    }

    std::ofstream output{output_path};
    if (!output) {
        std::cerr << "failed to open output file: " << output_path << '\n';
        return false;
    }

    for (const othello::match_runner::GameRecord& record : records) {
        write_jsonl_record(output, record);
    }

    if (!output) {
        std::cerr << "failed to write output file: " << output_path << '\n';
        return false;
    }

    return true;
}

void print_summary(const CliOptions& options,
                   std::span<const othello::match_runner::GameRecord> records) {
    const othello::match_runner::MatchSummary summary = othello::match_runner::summarize(records);

    std::cout << "games: " << summary.games << '\n';
    std::cout << "black spec (A initial): " << options.black.text << '\n';
    std::cout << "white spec (B initial): " << options.white.text << '\n';
    std::cout << "A wins: " << summary.player_a_wins << '\n';
    std::cout << "B wins: " << summary.player_b_wins << '\n';
    std::cout << "draws: " << summary.draws << '\n';
    std::cout << "average disc diff from A perspective: " << std::fixed << std::setprecision(2)
              << summary.average_disc_diff_from_player_a << '\n';
    std::cout << "output: " << options.output_path << '\n';
}

} // namespace

int main(int argc, char** argv) {
    bool help_requested = false;
    const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
    const std::optional<CliOptions> options = parse_options(args, help_requested);

    if (help_requested) {
        print_usage(args.empty() ? "othello_match_runner" : args[0]);
        return 0;
    }
    if (!options.has_value()) {
        print_usage(args.empty() ? "othello_match_runner" : args[0]);
        return 2;
    }

    const othello::match_runner::MatchConfig config{
        .player_a = options->black,
        .player_b = options->white,
        .games = options->games,
        .swap_sides = options->swap_sides,
        .seed = options->seed,
    };
    const std::vector<othello::match_runner::GameRecord> records =
        othello::match_runner::run_match(config);

    if (!write_jsonl_file(options->output_path, records)) {
        return 1;
    }

    if (!options->quiet) {
        print_summary(*options, records);
    }

    return 0;
}
