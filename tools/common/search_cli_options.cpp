#include "common/search_cli_options.hpp"

#include "common/cli.hpp"
#include "common/evaluator_selection.hpp"

#include <optional>

namespace othello::tools {

std::string_view aspiration_profile_name(AspirationProfile profile) noexcept {
    switch (profile) {
    case AspirationProfile::Fixed:
        return "fixed";
    case AspirationProfile::ScoreDeltaAware:
        return "score-delta-aware";
    }

    return "unknown";
}

std::optional<AspirationProfile> parse_aspiration_profile(std::string_view text) noexcept {
    if (text == "fixed") {
        return AspirationProfile::Fixed;
    }
    if (text == "score-delta-aware") {
        return AspirationProfile::ScoreDeltaAware;
    }
    return std::nullopt;
}

SearchCliParseResult parse_search_cli_option(std::span<char* const> args, std::size_t& index,
                                             SearchCliOptions& options, std::string& error,
                                             SearchCliParseOptions parse_options) {
    const std::string_view option = args[index];

    const auto parse_on_off_option = [&](std::optional<bool>& output) {
        ++index;
        if (index >= args.size()) {
            error = std::string{option} + " requires on or off";
            return false;
        }

        const auto setting = parse_on_off(args[index]);
        if (!setting.has_value()) {
            error = std::string{option} + " must be on or off";
            return false;
        }
        output = setting;
        return true;
    };

    if (option == "--tt") {
        return parse_on_off_option(options.use_transposition_table) ? SearchCliParseResult::Parsed
                                                                    : SearchCliParseResult::Error;
    }

    if (parse_options.parse_tt_store_leaf && option == "--tt-store-leaf") {
        return parse_on_off_option(options.store_leaf_tt_entries) ? SearchCliParseResult::Parsed
                                                                  : SearchCliParseResult::Error;
    }

    if (option == "--pvs") {
        return parse_on_off_option(options.use_pvs) ? SearchCliParseResult::Parsed
                                                    : SearchCliParseResult::Error;
    }

    if (option == "--aspiration") {
        return parse_on_off_option(options.use_aspiration_window) ? SearchCliParseResult::Parsed
                                                                  : SearchCliParseResult::Error;
    }

    if (option == "--aspiration-window") {
        ++index;
        if (index >= args.size()) {
            error = "--aspiration-window requires a positive integer";
            return SearchCliParseResult::Error;
        }

        const auto window = parse_positive_int(args[index]);
        if (!window.has_value()) {
            error = "--aspiration-window must be a positive integer";
            return SearchCliParseResult::Error;
        }
        options.aspiration_window = *window;
        return SearchCliParseResult::Parsed;
    }

    if (option == "--aspiration-max-researches") {
        ++index;
        if (index >= args.size()) {
            error = "--aspiration-max-researches requires a non-negative integer";
            return SearchCliParseResult::Error;
        }

        const auto researches = parse_non_negative_int(args[index]);
        if (!researches.has_value()) {
            error = "--aspiration-max-researches must be a non-negative integer";
            return SearchCliParseResult::Error;
        }
        options.aspiration_max_researches = *researches;
        return SearchCliParseResult::Parsed;
    }

    if (option == "--aspiration-profile") {
        ++index;
        if (index >= args.size()) {
            error = "--aspiration-profile requires fixed or score-delta-aware";
            return SearchCliParseResult::Error;
        }

        const auto profile = parse_aspiration_profile(args[index]);
        if (!profile.has_value()) {
            error = "--aspiration-profile must be fixed or score-delta-aware";
            return SearchCliParseResult::Error;
        }
        options.aspiration_profile = *profile;
        return SearchCliParseResult::Parsed;
    }

    if (option == "--tt-entries") {
        ++index;
        if (index >= args.size()) {
            error = "--tt-entries requires a non-negative integer";
            return SearchCliParseResult::Error;
        }

        const auto entry_count = parse_entry_count(args[index]);
        if (!entry_count.has_value()) {
            error = "--tt-entries must be a non-negative integer";
            return SearchCliParseResult::Error;
        }
        options.transposition_table_entries = *entry_count;
        return SearchCliParseResult::Parsed;
    }

    return SearchCliParseResult::NotSearchOption;
}

SearchCliParseResult parse_nboard_search_cli_option(std::span<char* const> args, std::size_t& index,
                                                    NBoardSearchCliOptions& options,
                                                    std::string& error) {
    const std::string_view option = args[index];

    if (option == "--preset") {
        const auto value = next_argument(args, index, option);
        const auto preset = value.has_value() ? parse_search_preset(*value) : std::nullopt;
        if (!preset.has_value()) {
            error = "invalid --preset value";
            return SearchCliParseResult::Error;
        }
        options.preset = *preset;
        return SearchCliParseResult::Parsed;
    }

    if (option == "--exact-endgame-threshold") {
        const auto value = next_argument(args, index, option);
        const auto threshold = value.has_value() ? parse_int(*value) : std::nullopt;
        if (!threshold.has_value()) {
            error = "invalid --exact-endgame-threshold value";
            return SearchCliParseResult::Error;
        }
        options.exact_endgame_empty_threshold = *threshold;
        options.exact_endgame_threshold_overridden = true;
        return SearchCliParseResult::Parsed;
    }

    return SearchCliParseResult::NotSearchOption;
}

SearchOptions apply_search_cli_options(SearchOptions options,
                                       const SearchCliOptions& cli_options) noexcept {
    if (cli_options.use_transposition_table.has_value()) {
        options.use_transposition_table = *cli_options.use_transposition_table;
    }
    if (cli_options.store_leaf_tt_entries.has_value()) {
        options.store_leaf_tt_entries = *cli_options.store_leaf_tt_entries;
    }
    if (cli_options.use_pvs.has_value()) {
        options.use_pvs = *cli_options.use_pvs;
    }
    if (cli_options.use_aspiration_window.has_value()) {
        options.use_aspiration_window = *cli_options.use_aspiration_window;
    }
    options.transposition_table_entries = cli_options.transposition_table_entries;
    options.aspiration_window = cli_options.aspiration_window;
    options.aspiration_max_researches = cli_options.aspiration_max_researches;
    options.aspiration_profile = cli_options.aspiration_profile;
    return options;
}

SearchOptions make_search_options_from_preset(SearchPreset preset, int max_depth,
                                              std::optional<int> exact_endgame_empty_threshold,
                                              const EvaluatorSelection& evaluator) noexcept {
    const SearchPresetOptions preset_options = search_preset_options(preset);
    SearchOptions options = preset_options.search_options;
    options.max_depth = max_depth;
    if (preset == SearchPreset::Default) {
        options.use_transposition_table = true;
        options.use_pvs = true;
        options.exact_endgame_empty_threshold = exact_endgame_empty_threshold.value_or(12);
    } else if (exact_endgame_empty_threshold.has_value()) {
        options.exact_endgame_empty_threshold = *exact_endgame_empty_threshold;
        options.exact_endgame_root_policy = ExactEndgameRootPolicy::FixedThreshold;
    }
    return apply_evaluator_selection(options, evaluator);
}

} // namespace othello::tools
