#include "common/cli.hpp"
#include "replay/replay.hpp"

#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

namespace {

struct Options {
    std::filesystem::path match_jsonl;
    othello::replay::OutputFormat format = othello::replay::OutputFormat::Markdown;
    bool help = false;
};

void print_usage(std::ostream& output) {
    output << R"(Usage: othello_replay_game --match-jsonl PATH [--format markdown|jsonl]

Replay match-runner JSONL with the C++ rule core and emit first base/head
divergence boards. This replaces the legacy Python rules-based divergence
extractor for benchmark diagnostics.

Options:
  --match-jsonl PATH   Match-runner JSONL file from a swap-side base/head run.
  --input PATH         Alias for --match-jsonl.
  --format FORMAT      markdown or jsonl. Defaults to markdown.
  --help               Show this help text.
)";
}

bool parse_args(std::span<char* const> args, Options& options) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view argument{args[index]};
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            return true;
        }
        if (argument == "--match-jsonl" || argument == "--input") {
            const auto value = othello::tools::next_argument(args, index, argument);
            if (!value.has_value()) {
                return false;
            }
            options.match_jsonl = std::filesystem::path{*value};
        } else if (argument == "--format") {
            const auto value = othello::tools::next_argument(args, index, argument);
            if (!value.has_value()) {
                return false;
            }
            if (*value == "markdown") {
                options.format = othello::replay::OutputFormat::Markdown;
            } else if (*value == "jsonl") {
                options.format = othello::replay::OutputFormat::Jsonl;
            } else {
                std::cerr << "--format must be markdown or jsonl\n";
                return false;
            }
        } else {
            std::cerr << "unknown option: " << argument << '\n';
            return false;
        }
    }

    if (options.match_jsonl.empty()) {
        std::cerr << "--match-jsonl is required\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_args(std::span<char* const>{argv, static_cast<std::size_t>(argc)}, options)) {
        print_usage(std::cerr);
        return 2;
    }
    if (options.help) {
        print_usage(std::cout);
        return 0;
    }

    auto records = othello::replay::read_match_jsonl_records(options.match_jsonl);
    if (!records.ok) {
        std::cerr << "error: " << records.error << '\n';
        return 1;
    }

    auto divergences = othello::replay::extract_divergences(records.records);
    if (!divergences.ok) {
        std::cerr << "error: " << divergences.error << '\n';
        return 1;
    }

    std::cout << othello::replay::render_divergences(divergences.divergences, options.format);
    return 0;
}
