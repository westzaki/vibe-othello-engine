#include "common/search_preset.hpp"

#include <optional>

namespace othello::tools {

std::string_view search_preset_name(SearchPreset preset) noexcept {
    switch (preset) {
    case SearchPreset::Default:
        return "default";
    case SearchPreset::StrongV1:
        return "strong-v1";
    case SearchPreset::ExperimentalShallowTt:
        return "experimental-shallow-tt";
    }

    return "unknown";
}

std::optional<SearchPreset> parse_search_preset(std::string_view text) noexcept {
    if (text == "default") {
        return SearchPreset::Default;
    }
    if (text == "strong-v1") {
        return SearchPreset::StrongV1;
    }
    if (text == "experimental-shallow-tt") {
        return SearchPreset::ExperimentalShallowTt;
    }
    return std::nullopt;
}

SearchOptions apply_strong_v1_search_options(SearchOptions options) noexcept {
    options.use_transposition_table = true;
    options.tt_min_probe_depth = 1;
    options.tt_min_store_depth = 1;
    options.use_lazy_first_move_ordering = true;
    options.use_pvs = true;
    options.use_aspiration_window = true;
    options.aspiration_profile = AspirationProfile::ScoreDeltaAware;
    options.exact_endgame_empty_threshold = 16;
    options.exact_endgame_root_policy = ExactEndgameRootPolicy::Adaptive16;
    return options;
}

SearchOptions apply_experimental_shallow_tt_search_options(SearchOptions options) noexcept {
    options = apply_strong_v1_search_options(options);
    options.use_shallow_tt_move_ordering_hint = true;
    return options;
}

SearchPresetOptions search_preset_options(SearchPreset preset) noexcept {
    SearchPresetOptions options;
    switch (preset) {
    case SearchPreset::Default:
        return options;
    case SearchPreset::StrongV1:
        options.search_options = apply_strong_v1_search_options(options.search_options);
        options.use_iterative_search = true;
        return options;
    case SearchPreset::ExperimentalShallowTt:
        options.search_options =
            apply_experimental_shallow_tt_search_options(options.search_options);
        options.use_iterative_search = true;
        return options;
    }

    return options;
}

} // namespace othello::tools
