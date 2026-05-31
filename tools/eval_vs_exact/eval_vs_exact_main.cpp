#include "common/cli.hpp"
#include "common/evaluator_selection.hpp"
#include "eval_vs_exact/eval_vs_exact.hpp"

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct Options {
    std::string labels_path;
    std::string output_path;
    othello::tools::EvaluatorSelectionInput evaluator_input;
    std::size_t top = 10;
    int high_confidence_threshold = 250;
    bool phase_breakdown = false;
    bool include_positions = false;
    bool help = false;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " --labels PATH --output PATH (--eval-preset NAME | --eval-config PATH)"
                 " [--top N] [--high-confidence-threshold N]"
                 " [--phase-breakdown] [--include-positions] [--help]\n"
              << '\n'
              << "Options:\n"
              << "  --labels PATH         read exact_label.v1 JSONL labels\n"
              << "  --output PATH         write Markdown analyzer report\n"
              << "  --eval-preset NAME    evaluate with a built-in evaluator preset\n"
              << "  --eval-config PATH    evaluate with a fully expanded .eval config\n"
              << "  --top N               number of disagreement positions to show (default: 10)\n"
              << "  --high-confidence-threshold N\n"
              << "                        minimum abs(eval_score) for high-confidence cases"
                 " (default: 250)\n"
              << "  --phase-breakdown     include evaluator phase bucket summary\n"
              << "  --include-positions   include board text in disagreement sections\n"
              << "  --help                show this help text\n";
}

[[nodiscard]] std::optional<Options> parse_options(std::span<char* const> args) {
    Options options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option = args[index];
        if (option == "--help") {
            options.help = true;
            return options;
        }
        if (option == "--phase-breakdown") {
            options.phase_breakdown = true;
            continue;
        }
        if (option == "--include-positions") {
            options.include_positions = true;
            continue;
        }
        if (option == "--labels") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.labels_path = std::string{*value};
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
        if (option == "--eval-preset") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.evaluator_input.preset_name = std::string{*value};
            continue;
        }
        if (option == "--eval-config") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.evaluator_input.config_path = std::string{*value};
            continue;
        }
        if (option == "--top") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto top = othello::tools::parse_size_t(*value);
            if (!top.has_value()) {
                std::cerr << "--top must be a non-negative integer\n";
                return std::nullopt;
            }
            options.top = *top;
            continue;
        }
        if (option == "--high-confidence-threshold") {
            const auto value = othello::tools::next_argument(args, index, option);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto threshold = othello::tools::parse_non_negative_int(*value);
            if (!threshold.has_value()) {
                std::cerr << "--high-confidence-threshold must be a non-negative integer\n";
                return std::nullopt;
            }
            options.high_confidence_threshold = *threshold;
            continue;
        }

        std::cerr << "unknown option: " << option << '\n';
        return std::nullopt;
    }

    if (options.labels_path.empty()) {
        std::cerr << "--labels is required\n";
        return std::nullopt;
    }
    if (options.output_path.empty()) {
        std::cerr << "--output is required\n";
        return std::nullopt;
    }
    if (!options.evaluator_input.preset_name.has_value() &&
        !options.evaluator_input.config_path.has_value()) {
        std::cerr << "exactly one of --eval-preset or --eval-config is required\n";
        return std::nullopt;
    }

    return options;
}

[[nodiscard]] std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        std::cerr << "failed to open labels file: " << path << '\n';
        return std::nullopt;
    }

    std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        std::cerr << "failed to read labels file: " << path << '\n';
        return std::nullopt;
    }
    return text;
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

[[nodiscard]] std::string current_timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream out;
    out << std::put_time(std::gmtime(&time), "%FT%TZ");
    return out.str();
}

[[nodiscard]] std::string command_line(std::span<char* const> args) {
    std::string command;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index != 0) {
            command += ' ';
        }
        command += args[index];
    }
    return command;
}

} // namespace

static int run(int argc, char** argv) {
    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    const std::optional<Options> parsed = parse_options(args);
    if (!parsed.has_value()) {
        return 1;
    }
    if (parsed->help) {
        print_usage(args.empty() ? "othello_eval_vs_exact" : args.front());
        return 0;
    }

    std::string error;
    const std::optional<othello::tools::EvaluatorSelection> evaluator =
        othello::tools::parse_evaluator_selection(parsed->evaluator_input, error);
    if (!evaluator.has_value()) {
        std::cerr << error << '\n';
        return 1;
    }

    const std::filesystem::path labels_path{parsed->labels_path};
    const std::optional<std::string> labels_text = read_text_file(labels_path);
    if (!labels_text.has_value()) {
        return 1;
    }

    const othello::tools::eval_vs_exact::AnalyzerOptions analyzer_options{
        .labels_path = labels_path,
        .evaluator = *evaluator,
        .top = parsed->top,
        .high_confidence_threshold = parsed->high_confidence_threshold,
        .phase_breakdown = parsed->phase_breakdown,
        .include_positions = parsed->include_positions,
        .timestamp = current_timestamp_utc(),
#ifdef OTHELLO_SOURCE_SHA
        .source_sha = OTHELLO_SOURCE_SHA,
#else
        .source_sha = "unknown",
#endif
        .command = command_line(args),
    };

    const std::optional<othello::tools::eval_vs_exact::AnalyzerReport> report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(*labels_text, analyzer_options,
                                                                 error);
    if (!report.has_value()) {
        std::cerr << error << '\n';
        return 1;
    }

    const std::filesystem::path output_path{parsed->output_path};
    if (!ensure_parent_directory(output_path)) {
        return 1;
    }
    std::ofstream output{output_path};
    if (!output) {
        std::cerr << "failed to open output file: " << output_path << '\n';
        return 1;
    }
    output << report->markdown;
    if (!output) {
        std::cerr << "failed to write output file: " << output_path << '\n';
        return 1;
    }

    std::cout << "eval vs exact: records_read=" << report->summary.records_read
              << " analyzed=" << report->summary.analyzed << " skipped=" << report->summary.skipped
              << " sign_agreements=" << report->summary.sign_agreements
              << " wrong_direction=" << report->summary.wrong_direction << " output=" << output_path
              << '\n';
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
