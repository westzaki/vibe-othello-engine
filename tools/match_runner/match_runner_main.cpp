#include "common/cli.hpp"
#include "match_runner/core.hpp"
#include "match_runner/engine_config.hpp"
#include "match_runner/jsonl_writer.hpp"

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
    std::string openings_path;
    std::string engines_path;
    int external_timeout_ms = 10000;
    std::string output_path;
    bool quiet = false;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " --black SPEC --white SPEC --games N --swap-sides true|false --seed N"
                 " [--openings PATH] [--engines PATH] --output PATH [--format jsonl] [--quiet]\n"
              << '\n'
              << "Player specs:\n"
              << "  first\n"
              << "  random\n"
              << "  eval\n"
              << "  search:depth=N[,tt=on|off][,pvs=on|off]"
                 "[,exact=off|N|adaptive16][,tt_entries=N][,tt_store_leaf=on|off]"
                 "[,eval=NAME|eval_config=PATH]\n"
              << "  external:NAME\n"
              << '\n'
              << "Options:\n"
              << "  --black SPEC        initial black / player A spec\n"
              << "  --white SPEC        initial white / player B spec\n"
              << "  --games N           number of games to run\n"
              << "  --swap-sides BOOL   alternate A/B sides by game when true\n"
              << "  --seed N            base random seed; game i uses seed + i\n"
              << "  --openings PATH     opening suite text file\n"
              << "  --engines PATH      external NBoard engine config file\n"
              << "  --external-timeout-ms N\n"
              << "                      timeout per external NBoard command sequence\n"
              << "  --output PATH       JSONL output path\n"
              << "  --format jsonl      output format\n"
              << "  --quiet             suppress stdout summary\n"
              << "  --help              show this help text\n";
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
            const auto value = othello::tools::next_argument(args, index, arg);
            black =
                value.has_value() ? othello::match_runner::parse_player_spec(*value) : std::nullopt;
            if (!black.has_value()) {
                std::cerr << "invalid --black player spec\n";
                return std::nullopt;
            }
        } else if (arg == "--white") {
            const auto value = othello::tools::next_argument(args, index, arg);
            white =
                value.has_value() ? othello::match_runner::parse_player_spec(*value) : std::nullopt;
            if (!white.has_value()) {
                std::cerr << "invalid --white player spec\n";
                return std::nullopt;
            }
        } else if (arg == "--games") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto games = value.has_value()
                                   ? othello::match_runner::parse_non_negative_int(*value)
                                   : std::nullopt;
            if (!games.has_value() || *games <= 0) {
                std::cerr << "invalid --games value\n";
                return std::nullopt;
            }
            options.games = *games;
        } else if (arg == "--swap-sides") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto swap_sides =
                value.has_value() ? othello::tools::parse_bool_true_false(*value) : std::nullopt;
            if (!swap_sides.has_value()) {
                std::cerr << "invalid --swap-sides value\n";
                return std::nullopt;
            }
            options.swap_sides = *swap_sides;
        } else if (arg == "--seed") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto seed =
                value.has_value() ? othello::match_runner::parse_u64(*value) : std::nullopt;
            if (!seed.has_value()) {
                std::cerr << "invalid --seed value\n";
                return std::nullopt;
            }
            options.seed = *seed;
        } else if (arg == "--openings") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value() || value->empty()) {
                std::cerr << "invalid --openings value\n";
                return std::nullopt;
            }
            options.openings_path = std::string{*value};
        } else if (arg == "--engines") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value() || value->empty()) {
                std::cerr << "invalid --engines value\n";
                return std::nullopt;
            }
            options.engines_path = std::string{*value};
        } else if (arg == "--external-timeout-ms") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto timeout =
                value.has_value() ? othello::tools::parse_positive_int(*value) : std::nullopt;
            if (!timeout.has_value()) {
                std::cerr << "invalid --external-timeout-ms value\n";
                return std::nullopt;
            }
            options.external_timeout_ms = *timeout;
        } else if (arg == "--output") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value() || value->empty()) {
                std::cerr << "invalid --output value\n";
                return std::nullopt;
            }
            options.output_path = std::string{*value};
        } else if (arg == "--format") {
            const auto value = othello::tools::next_argument(args, index, arg);
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

[[nodiscard]] std::optional<std::vector<othello::match_runner::Opening>>
load_openings_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        std::cerr << "failed to open openings file: " << path << '\n';
        return std::nullopt;
    }

    std::vector<othello::match_runner::Opening> openings;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const othello::match_runner::OpeningParseResult result =
            othello::match_runner::parse_opening_line(line);
        if (!result.ok) {
            std::cerr << "invalid opening at " << path << ':' << line_number << ": " << result.error
                      << '\n';
            return std::nullopt;
        }
        if (result.has_opening) {
            openings.push_back(result.opening);
        }
    }

    if (!input.eof()) {
        std::cerr << "failed to read openings file: " << path << '\n';
        return std::nullopt;
    }
    if (openings.empty()) {
        std::cerr << "openings file contains no openings: " << path << '\n';
        return std::nullopt;
    }

    return openings;
}

[[nodiscard]] std::optional<std::vector<othello::match_runner::ExternalEngineConfig>>
load_engines_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        std::cerr << "failed to open engines file: " << path << '\n';
        return std::nullopt;
    }

    std::vector<othello::match_runner::ExternalEngineConfig> engines;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const othello::match_runner::EngineConfigParseResult result =
            othello::match_runner::parse_engine_config_line(line);
        if (!result.ok) {
            std::cerr << "invalid engine config at " << path << ':' << line_number << ": "
                      << result.error << '\n';
            return std::nullopt;
        }
        if (result.has_config) {
            engines.push_back(result.config);
        }
    }

    if (!input.eof()) {
        std::cerr << "failed to read engines file: " << path << '\n';
        return std::nullopt;
    }
    if (engines.empty()) {
        std::cerr << "engines file contains no engines: " << path << '\n';
        return std::nullopt;
    }

    return engines;
}

[[nodiscard]] bool uses_external_player(const othello::match_runner::PlayerSpec& spec) noexcept {
    return spec.kind == othello::match_runner::PlayerKind::ExternalNBoard;
}

void print_summary(const CliOptions& options,
                   std::span<const othello::match_runner::GameRecord> records,
                   std::size_t openings_count) {
    const othello::match_runner::MatchSummary summary = othello::match_runner::summarize(records);

    std::cout << "games: " << summary.games << '\n';
    std::cout << "valid games: " << summary.valid_games << '\n';
    std::cout << "error games: " << summary.error_games << '\n';
    std::cout << "black spec (A initial): " << options.black.text << '\n';
    std::cout << "white spec (B initial): " << options.white.text << '\n';
    std::cout << "A wins: " << summary.player_a_wins << '\n';
    std::cout << "B wins: " << summary.player_b_wins << '\n';
    std::cout << "draws: " << summary.draws << '\n';
    std::cout << "average disc diff from A perspective: " << std::fixed << std::setprecision(2)
              << summary.average_disc_diff_from_player_a << '\n';
    std::cout << "openings: " << openings_count << '\n';
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

    std::vector<othello::match_runner::Opening> openings;
    if (!options->openings_path.empty()) {
        const std::optional<std::vector<othello::match_runner::Opening>> loaded_openings =
            load_openings_file(options->openings_path);
        if (!loaded_openings.has_value()) {
            return 2;
        }
        openings = *loaded_openings;
    }

    std::vector<othello::match_runner::ExternalEngineConfig> engines;
    if (!options->engines_path.empty()) {
        const std::optional<std::vector<othello::match_runner::ExternalEngineConfig>>
            loaded_engines = load_engines_file(options->engines_path);
        if (!loaded_engines.has_value()) {
            return 2;
        }
        engines = *loaded_engines;
    } else if (uses_external_player(options->black) || uses_external_player(options->white)) {
        std::cerr << "--engines PATH is required when using external players\n";
        return 2;
    }

    const othello::match_runner::MatchConfig config{
        .player_a = options->black,
        .player_b = options->white,
        .games = options->games,
        .swap_sides = options->swap_sides,
        .seed = options->seed,
        .openings = openings,
        .external_engines = engines,
        .external_timeout_ms = options->external_timeout_ms,
    };
    const std::vector<othello::match_runner::GameRecord> records =
        othello::match_runner::run_match(config);

    if (!othello::match_runner::write_jsonl_file(options->output_path, records)) {
        return 1;
    }

    if (!options->quiet) {
        print_summary(*options, records, openings.empty() ? 1 : openings.size());
    }

    return 0;
}
