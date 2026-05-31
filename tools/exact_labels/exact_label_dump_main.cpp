#include "common/board_io.hpp"
#include "common/cli.hpp"
#include "exact_labels/exact_label_dump.hpp"

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
    std::string input_path;
    std::string output_path;
    othello::tools::exact_labels::DumpOptions dump;
    bool help = false;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " --input PATH --output PATH [--limit N] [--max-empties N]"
                 " [--include-move-scores] [--help]\n"
              << '\n'
              << "Options:\n"
              << "  --input PATH          read board positions in existing 9-line board format\n"
              << "  --output PATH         write exact-solver labels as JSONL\n"
              << "  --limit N             optional positive maximum number of labels to write\n"
              << "  --max-empties N       skip positions with more than N empties (default: 14)\n"
              << "  --include-move-scores solve each legal root move and include move_scores\n"
              << "  --help                show this help text\n";
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

        if (option == "--include-move-scores") {
            options.dump.include_move_scores = true;
            continue;
        }

        if (option == "--input") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.input_path = std::string{*value};
            continue;
        }

        if (option == "--output") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.output_path = std::string{*value};
            continue;
        }

        if (option == "--limit") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto limit = parse_positive_size(*value);
            if (!limit.has_value()) {
                std::cerr << "--limit must be a positive integer\n";
                return std::nullopt;
            }
            options.dump.limit = *limit;
            continue;
        }

        if (option == "--max-empties") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto max_empties = othello::tools::parse_non_negative_int(*value);
            if (!max_empties.has_value() || *max_empties > 64) {
                std::cerr << "--max-empties must be an integer in [0, 64]\n";
                return std::nullopt;
            }
            options.dump.max_empties = *max_empties;
            continue;
        }

        std::cerr << "unknown option: " << option << '\n';
        return std::nullopt;
    }

    if (options.input_path.empty()) {
        std::cerr << "--input is required\n";
        return std::nullopt;
    }
    if (options.output_path.empty()) {
        std::cerr << "--output is required\n";
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

int main(int argc, char** argv) {
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const auto options = parse_options(args);
    if (!options.has_value()) {
        return 1;
    }
    if (options->help) {
        print_usage(args.empty() ? "othello_exact_label_dump" : args.front());
        return 0;
    }

    const std::optional<std::string> input_text =
        othello::tools::read_text_file(options->input_path);
    if (!input_text.has_value()) {
        return 1;
    }

    std::string error;
    const auto positions =
        othello::tools::exact_labels::parse_position_text(*input_text, error);
    if (!positions.has_value()) {
        std::cerr << "invalid input: " << error << '\n';
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

    othello::tools::exact_labels::DumpSummary summary;
    if (!othello::tools::exact_labels::write_exact_label_jsonl(*positions, options->dump, output,
                                                               summary, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::cout << "exact label dump: input_positions=" << summary.input_positions
              << " labeled=" << summary.labeled
              << " skipped_too_many_empties=" << summary.skipped_too_many_empties
              << " output=" << output_path << '\n';
    return 0;
}
