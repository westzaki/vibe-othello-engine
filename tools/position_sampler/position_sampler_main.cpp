#include "common/cli.hpp"
#include "position_sampler/position_sampler.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct Options {
    std::string output_path;
    othello::tools::position_sampler::SamplerOptions sampler;
    bool seed_seen = false;
    bool help = false;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " --output PATH --count N --target-empties LIST --seed N"
                 " [--max-plies N] [--unique true|false]"
                 " [--allow-terminal true|false] [--max-attempts N] [--help]\n"
              << '\n'
              << "Options:\n"
              << "  --output PATH          write sampled positions in 9-line board format\n"
              << "  --count N              positive number of positions to emit\n"
              << "  --target-empties LIST  comma-separated exact empty counts, e.g. 8,10,12\n"
              << "  --seed N               deterministic random seed\n"
              << "  --max-plies N          maximum random plies per attempt (default: 128)\n"
              << "  --unique true|false    deduplicate board+side positions (default: true)\n"
              << "  --allow-terminal true|false\n"
              << "                         allow terminal sampled positions (default: false)\n"
              << "  --max-attempts N       optional positive attempt budget override\n"
              << "  --help                 show this help text\n";
}

[[nodiscard]] std::optional<std::size_t> parse_positive_size(std::string_view text) noexcept {
    const std::optional<std::size_t> value = othello::tools::parse_size_t(text);
    if (!value.has_value() || *value == 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<Options> parse_options(std::span<char* const> args) {
    Options options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option = args[index];
        if (option == "--help") {
            options.help = true;
            return options;
        }
        if (option == "--output") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.output_path = std::string{*value};
            continue;
        }
        if (option == "--count") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto count = parse_positive_size(*value);
            if (!count.has_value()) {
                std::cerr << "--count must be a positive integer\n";
                return std::nullopt;
            }
            options.sampler.count = *count;
            continue;
        }
        if (option == "--target-empties") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            std::string error;
            const auto target_empties =
                othello::tools::position_sampler::parse_target_empties(*value, error);
            if (!target_empties.has_value()) {
                std::cerr << error << '\n';
                return std::nullopt;
            }
            options.sampler.target_empties = *target_empties;
            continue;
        }
        if (option == "--seed") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto seed = othello::tools::parse_u64(*value);
            if (!seed.has_value()) {
                std::cerr << "--seed must be a non-negative integer\n";
                return std::nullopt;
            }
            options.sampler.seed = *seed;
            options.seed_seen = true;
            continue;
        }
        if (option == "--max-plies") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto max_plies = othello::tools::parse_non_negative_int(*value);
            if (!max_plies.has_value()) {
                std::cerr << "--max-plies must be a non-negative integer\n";
                return std::nullopt;
            }
            options.sampler.max_plies = *max_plies;
            continue;
        }
        if (option == "--unique") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto unique = othello::tools::parse_bool_true_false(*value);
            if (!unique.has_value()) {
                std::cerr << "--unique must be true or false\n";
                return std::nullopt;
            }
            options.sampler.unique = *unique;
            continue;
        }
        if (option == "--allow-terminal") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto allow_terminal = othello::tools::parse_bool_true_false(*value);
            if (!allow_terminal.has_value()) {
                std::cerr << "--allow-terminal must be true or false\n";
                return std::nullopt;
            }
            options.sampler.allow_terminal = *allow_terminal;
            continue;
        }
        if (option == "--max-attempts") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto max_attempts = parse_positive_size(*value);
            if (!max_attempts.has_value()) {
                std::cerr << "--max-attempts must be a positive integer\n";
                return std::nullopt;
            }
            options.sampler.max_attempts = *max_attempts;
            continue;
        }

        std::cerr << "unknown option: " << option << '\n';
        return std::nullopt;
    }

    if (options.output_path.empty()) {
        std::cerr << "--output is required\n";
        return std::nullopt;
    }
    if (options.sampler.count == 0) {
        std::cerr << "--count is required\n";
        return std::nullopt;
    }
    if (options.sampler.target_empties.empty()) {
        std::cerr << "--target-empties is required\n";
        return std::nullopt;
    }
    if (!options.seed_seen) {
        std::cerr << "--seed is required\n";
        return std::nullopt;
    }

    return options;
}

[[nodiscard]] bool ensure_parent_directory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) {
        std::cerr << "failed to create output directory: " << parent << ": " << error.message()
                  << '\n';
        return false;
    }
    return true;
}

} // namespace

static int run(int argc, char** argv) {
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const auto options = parse_options(args);
    if (!options.has_value()) {
        return 1;
    }
    if (options->help) {
        print_usage(args.empty() ? "othello_position_sampler" : args.front());
        return 0;
    }

    othello::tools::position_sampler::SampleSummary summary;
    std::string error;
    const auto samples =
        othello::tools::position_sampler::sample_positions(options->sampler, summary, error);
    if (!samples.has_value()) {
        std::cerr << error << " after attempts=" << summary.attempts
                  << " sampled=" << summary.sampled << " duplicates=" << summary.duplicates
                  << " discarded_terminal=" << summary.discarded_terminal
                  << " discarded_max_plies=" << summary.discarded_max_plies << '\n';
        return 1;
    }

    const std::filesystem::path output_path{options->output_path};
    if (!ensure_parent_directory(output_path)) {
        return 1;
    }
    std::ofstream output{output_path};
    if (!output) {
        std::cerr << "failed to open output file: " << output_path << '\n';
        return 1;
    }

    othello::tools::position_sampler::write_positions(output, *samples);
    if (!output) {
        std::cerr << "failed to write output file: " << output_path << '\n';
        return 1;
    }

    std::cout << "position sampler: sampled=" << summary.sampled
              << " attempts=" << summary.attempts << " duplicates=" << summary.duplicates
              << " discarded_terminal=" << summary.discarded_terminal
              << " discarded_max_plies=" << summary.discarded_max_plies
              << " output=" << output_path << '\n';
    return 0;
}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "fatal error: " << exception.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "fatal error: unknown exception\n";
        return 1;
    }
}
