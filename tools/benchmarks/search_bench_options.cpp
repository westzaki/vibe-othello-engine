#include "benchmarks/search_bench_options.hpp"

#include "common/cli.hpp"
#include "common/evaluator_cli.hpp"
#include "common/search_preset.hpp"

#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace othello::benchmarks::search_bench {

using othello::tools::OutputFormat;
using othello::tools::parse_output_format;

namespace {

[[nodiscard]] std::optional<int> parse_positive_depth(std::string_view text) noexcept {
    return othello::tools::parse_positive_int(text);
}

[[nodiscard]] std::optional<int> parse_int(std::string_view text) noexcept {
    return othello::tools::parse_int(text);
}

} // namespace

[[nodiscard]] ExactRootProfile fixed_exact_root_profile(int threshold) {
    return ExactRootProfile{
        .label = std::to_string(threshold),
        .threshold = threshold,
    };
}

[[nodiscard]] ExactRootProfile adaptive16_exact_root_profile() {
    return ExactRootProfile{
        .label = "adaptive16",
        .threshold = 16,
        .policy = othello::ExactEndgameRootPolicy::Adaptive16,
    };
}

[[nodiscard]] ExactRootProfile experimental_exact_root_profile(std::string label,
                                                               ExactRootProfileKind kind) {
    return ExactRootProfile{
        .label = std::move(label),
        .threshold = 16,
        .kind = kind,
    };
}

BenchmarkOptions::BenchmarkOptions()
    : exact_root_profiles{
          fixed_exact_root_profile(othello::SearchOptions{}.exact_endgame_empty_threshold)} {}

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " [--mode fixed|iterative|both] [--depths 1,2,3,4,5]"
                 " [--preset default|strong-v1|experimental-shallow-tt]"
                 " [--repetitions N] [--positions smoke|suite|evaluation|threshold]"
                 " [--describe-positions] [--by-position] [--emit-iterative-depth-rows]"
                 " [--tt on|off] [--tt-entries N] [--exact-tt-entries N]"
                 " [--tt-store-leaf on|off] [--tt-min-probe-depth N]"
                 " [--tt-min-store-depth N] [--lazy-first-move-ordering on|off]"
                 " [--shallow-tt-move-ordering-hint on|off]"
                 " [--pvs on|off] [--aspiration on|off]"
                 " [--aspiration-window N]"
                 " [--aspiration-max-researches N]"
                 " [--aspiration-profile fixed|score-delta-aware]"
                 " [--exact-endgame-threshold N]"
                 " [--exact-endgame-thresholds LIST]"
                 " "
              << othello::tools::evaluator_cli_usage() << " [--format text|jsonl]\n"
              << '\n'
              << "Options:\n"
              << "  --depths LIST       comma-separated positive search depths\n"
              << "  --repetitions N     positive repetition count per depth\n"
              << "  --mode MODE         fixed, iterative, or both (default: fixed)\n"
              << "  --preset PRESET     search preset: default, strong-v1, or "
                 "experimental-shallow-tt\n"
              << "                      experimental-shallow-tt is a behavior-changing "
                 "candidate with shallow TT hints\n"
              << "  --positions SET     smoke, suite, evaluation, or threshold (default: smoke)\n"
              << "  --describe-positions\n"
              << "                      print selected position metadata and metrics only\n"
              << "  --by-position       print per-position benchmark rows and summaries\n"
              << "  --emit-iterative-depth-rows\n"
              << "                      with jsonl output, emit diagnostic rows for each "
                 "completed iterative depth\n"
              << "  --tt on|off         override the SearchOptions TT default\n"
              << "  --tt-entries N      requested transposition table entry count\n"
              << "  --exact-tt-entries N\n"
              << "                      requested private exact-endgame TT entries; 0 disables "
                 "only exact TT\n"
              << "  --tt-store-leaf on|off\n"
              << "                      store depth-0 midgame heuristic leaves in TT\n"
              << "  --tt-min-probe-depth N\n"
              << "                      skip midgame TT probes below remaining depth N\n"
              << "  --tt-min-store-depth N\n"
              << "                      skip midgame TT stores below remaining depth N\n"
              << "  --lazy-first-move-ordering on|off\n"
              << "                      try a legal PV/root/TT preferred move before full "
                 "ordering\n"
              << "  --shallow-tt-move-ordering-hint on|off\n"
              << "                      allow shallower matching TT best moves as ordering-only "
                 "hints\n"
              << "  --pvs on|off        override the SearchOptions PVS default\n"
              << "  --aspiration on|off enable iterative-search aspiration windows\n"
              << "  --aspiration-window N\n"
              << "                      positive initial aspiration half-window\n"
              << "  --aspiration-max-researches N\n"
              << "                      non-negative aspiration widening retries before "
                 "full-window fallback\n"
              << "  --aspiration-profile PROFILE\n"
              << "                      fixed or score-delta-aware (default: fixed)\n"
              << "  --exact-endgame-threshold N|adaptive16\n"
              << "                      solve root positions by fixed threshold N, or use the "
                 "experimental adaptive16 root profile; N <= 0 disables\n"
              << "  --exact-endgame-thresholds LIST\n"
              << "                      comma-separated exact root thresholds/profiles for matrix "
                 "runs; tokens may include adaptive16, adaptive16_current, "
                 "adaptive16_cap8, adaptive16_cap6, adaptive16_opp10, "
                 "adaptive16_shape, or adaptive16_split\n"
              << othello::tools::evaluator_cli_help()
              << "  --format FORMAT    output format: text or jsonl (default: text)\n"
              << "  --help              show this help text\n";
}

[[nodiscard]] std::string_view mode_name(SearchRunMode mode) noexcept {
    switch (mode) {
    case SearchRunMode::Fixed:
        return "fixed";
    case SearchRunMode::Iterative:
        return "iterative";
    }

    return "unknown";
}

[[nodiscard]] std::uint64_t mode_checksum(SearchRunMode mode) noexcept {
    switch (mode) {
    case SearchRunMode::Fixed:
        return 1;
    case SearchRunMode::Iterative:
        return 2;
    }

    return 0;
}

[[nodiscard]] std::string_view position_set_name(PositionSet position_set) noexcept {
    switch (position_set) {
    case PositionSet::Smoke:
        return "smoke";
    case PositionSet::Suite:
        return "suite";
    case PositionSet::Evaluation:
        return "evaluation";
    case PositionSet::Threshold:
        return "threshold";
    }

    return "unknown";
}

[[nodiscard]] std::optional<std::vector<int>> parse_depths(std::string_view text) {
    std::vector<int> depths;

    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto token = text.substr(begin, end - begin);
        const auto depth = parse_positive_depth(token);
        if (!depth.has_value()) {
            return std::nullopt;
        }

        depths.push_back(*depth);
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }

    if (depths.empty()) {
        return std::nullopt;
    }

    return depths;
}

[[nodiscard]] std::optional<ExactRootProfile> parse_exact_root_profile(std::string_view text) {
    if (text == "adaptive16") {
        return adaptive16_exact_root_profile();
    }
    if (text == "adaptive16_current") {
        return experimental_exact_root_profile("adaptive16_current",
                                               ExactRootProfileKind::Adaptive16Current);
    }
    if (text == "adaptive16_cap8") {
        return experimental_exact_root_profile("adaptive16_cap8",
                                               ExactRootProfileKind::Adaptive16Cap8);
    }
    if (text == "adaptive16_cap6") {
        return experimental_exact_root_profile("adaptive16_cap6",
                                               ExactRootProfileKind::Adaptive16Cap6);
    }
    if (text == "adaptive16_opp10") {
        return experimental_exact_root_profile("adaptive16_opp10",
                                               ExactRootProfileKind::Adaptive16Opponent10);
    }
    if (text == "adaptive16_shape") {
        return experimental_exact_root_profile("adaptive16_shape",
                                               ExactRootProfileKind::Adaptive16Shape);
    }
    if (text == "adaptive16_split") {
        return experimental_exact_root_profile("adaptive16_split",
                                               ExactRootProfileKind::Adaptive16Split);
    }

    const auto threshold = parse_int(text);
    if (!threshold.has_value()) {
        return std::nullopt;
    }
    return fixed_exact_root_profile(*threshold);
}

[[nodiscard]] std::optional<std::vector<ExactRootProfile>>
parse_exact_root_profiles(std::string_view text) {
    std::vector<ExactRootProfile> profiles;

    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto token = text.substr(begin, end - begin);
        const auto profile = parse_exact_root_profile(token);
        if (!profile.has_value()) {
            return std::nullopt;
        }

        profiles.push_back(*profile);
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }

    if (profiles.empty()) {
        return std::nullopt;
    }

    return profiles;
}

[[nodiscard]] std::string exact_root_profile_list_text(std::span<const ExactRootProfile> profiles) {
    std::string text;
    for (const auto& profile : profiles) {
        if (!text.empty()) {
            text += ',';
        }
        text += profile.label;
    }
    return text;
}

[[nodiscard]] std::optional<PositionSet> parse_position_set(std::string_view text) {
    if (text == "smoke") {
        return PositionSet::Smoke;
    }
    if (text == "suite") {
        return PositionSet::Suite;
    }
    if (text == "evaluation") {
        return PositionSet::Evaluation;
    }
    if (text == "threshold") {
        return PositionSet::Threshold;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<SearchBenchmarkMode> parse_benchmark_mode(std::string_view text) {
    if (text == "fixed") {
        return SearchBenchmarkMode::Fixed;
    }
    if (text == "iterative") {
        return SearchBenchmarkMode::Iterative;
    }
    if (text == "both") {
        return SearchBenchmarkMode::Both;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<BenchmarkOptions> parse_options(std::span<char* const> args) {
    BenchmarkOptions options;
    othello::tools::EvaluatorCliParseState evaluator_cli;
    bool mode_explicit = false;
    bool exact_root_explicit = false;
    constexpr othello::tools::EvaluatorCliParseOptions evaluator_cli_options{
        .missing_eval_config_message = "--eval-config requires a .eval config path",
        .reject_empty_eval_config = false,
    };

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option = args[index];

        if (option == "--help") {
            return std::nullopt;
        }

        if (option == "--describe-positions") {
            options.describe_positions = true;
            continue;
        }

        if (option == "--by-position") {
            options.by_position = true;
            continue;
        }

        if (option == "--emit-iterative-depth-rows") {
            options.emit_iterative_depth_rows = true;
            continue;
        }

        if (option == "--depths") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--depths requires a comma-separated positive integer list\n";
                return std::nullopt;
            }

            const auto depths = parse_depths(args[index]);
            if (!depths.has_value()) {
                std::cerr << "--depths must be a comma-separated positive integer list\n";
                return std::nullopt;
            }
            options.depths = *depths;
            continue;
        }

        if (option == "--mode") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--mode requires fixed, iterative, or both\n";
                return std::nullopt;
            }

            const auto mode = parse_benchmark_mode(args[index]);
            if (!mode.has_value()) {
                std::cerr << "--mode must be fixed, iterative, or both\n";
                return std::nullopt;
            }
            options.mode = *mode;
            mode_explicit = true;
            continue;
        }

        if (option == "--preset") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--preset requires default, strong-v1, or "
                             "experimental-shallow-tt\n";
                return std::nullopt;
            }

            const auto preset = othello::tools::parse_search_preset(args[index]);
            if (!preset.has_value()) {
                std::cerr << "--preset must be default, strong-v1, or "
                             "experimental-shallow-tt\n";
                return std::nullopt;
            }
            const othello::tools::SearchPresetOptions preset_options =
                othello::tools::search_preset_options(*preset);
            options.preset = *preset;
            options.search_cli.use_transposition_table =
                preset_options.search_options.use_transposition_table;
            options.search_cli.store_leaf_tt_entries =
                preset_options.search_options.store_leaf_tt_entries;
            options.search_cli.use_pvs = preset_options.search_options.use_pvs;
            options.search_cli.use_aspiration_window =
                preset_options.search_options.use_aspiration_window;
            options.search_cli.transposition_table_entries =
                preset_options.search_options.transposition_table_entries;
            options.search_cli.tt_min_probe_depth =
                preset_options.search_options.tt_min_probe_depth;
            options.search_cli.tt_min_store_depth =
                preset_options.search_options.tt_min_store_depth;
            options.search_cli.use_lazy_first_move_ordering =
                preset_options.search_options.use_lazy_first_move_ordering;
            options.search_cli.use_shallow_tt_move_ordering_hint =
                preset_options.search_options.use_shallow_tt_move_ordering_hint;
            options.search_cli.exact_endgame_tt_entries =
                preset_options.search_options.exact_endgame_tt_entries;
            options.search_cli.aspiration_window = preset_options.search_options.aspiration_window;
            options.search_cli.aspiration_max_researches =
                preset_options.search_options.aspiration_max_researches;
            options.search_cli.aspiration_profile =
                preset_options.search_options.aspiration_profile;
            if (preset_options.use_iterative_search && !mode_explicit) {
                options.mode = SearchBenchmarkMode::Iterative;
            }
            if (!exact_root_explicit) {
                if (preset_options.search_options.exact_endgame_root_policy ==
                    othello::ExactEndgameRootPolicy::Adaptive16) {
                    options.exact_root_profiles = {adaptive16_exact_root_profile()};
                } else {
                    options.exact_root_profiles = {
                        fixed_exact_root_profile(
                            preset_options.search_options.exact_endgame_empty_threshold)};
                }
            }
            continue;
        }

        if (option == "--positions") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--positions requires smoke, suite, evaluation, or threshold\n";
                return std::nullopt;
            }

            const auto position_set = parse_position_set(args[index]);
            if (!position_set.has_value()) {
                std::cerr << "--positions must be smoke, suite, evaluation, or threshold\n";
                return std::nullopt;
            }
            options.position_set = *position_set;
            continue;
        }

        std::string search_cli_error;
        const othello::tools::SearchCliParseResult search_cli_result =
            othello::tools::parse_search_cli_option(args, index, options.search_cli,
                                                    search_cli_error);
        if (search_cli_result == othello::tools::SearchCliParseResult::Error) {
            std::cerr << search_cli_error << '\n';
            return std::nullopt;
        }
        if (search_cli_result == othello::tools::SearchCliParseResult::Parsed) {
            continue;
        }

        if (option == "--exact-endgame-threshold") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--exact-endgame-threshold requires an integer or adaptive16\n";
                return std::nullopt;
            }

            const auto profile = parse_exact_root_profile(args[index]);
            if (!profile.has_value()) {
                std::cerr << "--exact-endgame-threshold must be an integer or adaptive16\n";
                return std::nullopt;
            }
            options.exact_root_profiles = {*profile};
            exact_root_explicit = true;
            continue;
        }

        if (option == "--exact-endgame-thresholds") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--exact-endgame-thresholds requires a comma-separated profile list\n";
                return std::nullopt;
            }

            const auto profiles = parse_exact_root_profiles(args[index]);
            if (!profiles.has_value()) {
                std::cerr << "--exact-endgame-thresholds must be a comma-separated list of "
                             "integers or adaptive16\n";
                return std::nullopt;
            }
            options.exact_root_profiles = *profiles;
            exact_root_explicit = true;
            continue;
        }

        std::string evaluator_cli_error;
        const othello::tools::EvaluatorCliParseResult evaluator_cli_result =
            othello::tools::parse_evaluator_cli_option(args, index, evaluator_cli,
                                                       evaluator_cli_error, evaluator_cli_options);
        if (evaluator_cli_result == othello::tools::EvaluatorCliParseResult::Error) {
            std::cerr << evaluator_cli_error << '\n';
            return std::nullopt;
        }
        if (evaluator_cli_result == othello::tools::EvaluatorCliParseResult::Parsed) {
            continue;
        }

        if (option == "--repetitions") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--repetitions requires a positive integer\n";
                return std::nullopt;
            }

            const auto repetitions = othello::tools::parse_positive_count(args[index]);
            if (!repetitions.has_value()) {
                std::cerr << "--repetitions must be a positive integer\n";
                return std::nullopt;
            }
            options.repetitions = *repetitions;
            continue;
        }

        if (option == "--format") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--format requires text or jsonl\n";
                return std::nullopt;
            }

            const auto output_format = parse_output_format(args[index]);
            if (!output_format.has_value()) {
                std::cerr << "invalid --format value; expected text or jsonl\n";
                return std::nullopt;
            }
            options.output_format = *output_format;
            continue;
        }

        std::cerr << "unknown option: " << option << '\n';
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

} // namespace othello::benchmarks::search_bench
