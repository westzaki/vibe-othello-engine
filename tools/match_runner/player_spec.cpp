#include "player_spec.hpp"

#include "common/cli.hpp"
#include "common/evaluator_selection.hpp"

#include <limits>
#include <string>

namespace othello::match_runner {

std::optional<int> parse_non_negative_int(std::string_view text) noexcept {
    return tools::parse_non_negative_int(text);
}

std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept {
    return tools::parse_u64(text);
}

std::optional<bool> parse_on_off(std::string_view text) noexcept {
    return tools::parse_on_off(text);
}

std::optional<PlayerSpec> parse_player_spec(std::string_view text) {
    if (text == "first") {
        return PlayerSpec{.kind = PlayerKind::First, .depth = 0, .text = std::string{text}};
    }
    if (text == "random") {
        return PlayerSpec{.kind = PlayerKind::Random, .depth = 0, .text = std::string{text}};
    }
    if (text == "eval") {
        return PlayerSpec{.kind = PlayerKind::Eval, .depth = 0, .text = std::string{text}};
    }

    constexpr std::string_view external_prefix = "external:";
    if (text.starts_with(external_prefix)) {
        const std::string_view name = text.substr(external_prefix.size());
        if (name.empty()) {
            return std::nullopt;
        }
        return PlayerSpec{.kind = PlayerKind::ExternalNBoard,
                          .depth = 0,
                          .external_engine_name = std::string{name},
                          .text = std::string{text}};
    }

    constexpr std::string_view search_prefix = "search:depth=";
    if (text.starts_with(search_prefix)) {
        std::string_view rest = text.substr(search_prefix.size());
        const std::size_t depth_end = rest.find(',');
        const std::string_view depth_text = rest.substr(0, depth_end);
        const std::optional<int> depth = parse_non_negative_int(depth_text);
        if (!depth.has_value() || *depth <= 0) {
            return std::nullopt;
        }

        SearchPlayerOptions search_options;
        search_options.max_depth = *depth;

        bool seen_tt = false;
        bool seen_tt_store_leaf = false;
        bool seen_pvs = false;
        bool seen_exact = false;
        bool seen_tt_entries = false;
        bool seen_eval_config = false;
        tools::EvaluatorSelectionInput evaluator_input;

        if (depth_end != std::string_view::npos) {
            rest.remove_prefix(depth_end + 1);
            while (true) {
                if (rest.empty()) {
                    return std::nullopt;
                }

                const std::size_t option_end = rest.find(',');
                const std::string_view option = rest.substr(0, option_end);
                if (option.empty()) {
                    return std::nullopt;
                }

                const std::size_t equals = option.find('=');
                if (equals == std::string_view::npos) {
                    return std::nullopt;
                }

                const std::string_view key = option.substr(0, equals);
                const std::string_view value = option.substr(equals + 1);

                if (key == "tt") {
                    if (seen_tt) {
                        return std::nullopt;
                    }
                    const std::optional<bool> parsed = parse_on_off(value);
                    if (!parsed.has_value()) {
                        return std::nullopt;
                    }
                    search_options.use_transposition_table = *parsed;
                    seen_tt = true;
                } else if (key == "tt_store_leaf") {
                    if (seen_tt_store_leaf) {
                        return std::nullopt;
                    }
                    const std::optional<bool> parsed = parse_on_off(value);
                    if (!parsed.has_value()) {
                        return std::nullopt;
                    }
                    search_options.store_leaf_tt_entries = *parsed;
                    seen_tt_store_leaf = true;
                } else if (key == "pvs") {
                    if (seen_pvs) {
                        return std::nullopt;
                    }
                    const std::optional<bool> parsed = parse_on_off(value);
                    if (!parsed.has_value()) {
                        return std::nullopt;
                    }
                    search_options.use_pvs = *parsed;
                    seen_pvs = true;
                } else if (key == "exact") {
                    if (seen_exact) {
                        return std::nullopt;
                    }
                    if (value == "off") {
                        search_options.exact_endgame_empty_threshold = 0;
                        search_options.exact_endgame_root_policy =
                            ExactEndgameRootPolicy::FixedThreshold;
                    } else if (value == "adaptive16") {
                        search_options.exact_endgame_empty_threshold = 16;
                        search_options.exact_endgame_root_policy =
                            ExactEndgameRootPolicy::Adaptive16;
                    } else {
                        const std::optional<int> parsed = parse_non_negative_int(value);
                        if (!parsed.has_value()) {
                            return std::nullopt;
                        }
                        search_options.exact_endgame_empty_threshold = *parsed;
                        search_options.exact_endgame_root_policy =
                            ExactEndgameRootPolicy::FixedThreshold;
                    }
                    seen_exact = true;
                } else if (key == "tt_entries") {
                    if (seen_tt_entries) {
                        return std::nullopt;
                    }
                    const std::optional<std::uint64_t> parsed = parse_u64(value);
                    if (!parsed.has_value() ||
                        *parsed >
                            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
                        return std::nullopt;
                    }
                    search_options.transposition_table_entries = static_cast<std::size_t>(*parsed);
                    seen_tt_entries = true;
                } else if (key == "eval_config") {
                    if (seen_eval_config || value.empty()) {
                        return std::nullopt;
                    }
                    evaluator_input.config_path = std::string{value};
                    seen_eval_config = true;
                } else {
                    return std::nullopt;
                }

                if (option_end == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(option_end + 1);
            }
        }

        std::string evaluator_error;
        const std::optional<tools::EvaluatorSelection> evaluator =
            tools::parse_evaluator_selection(evaluator_input, evaluator_error);
        if (!evaluator.has_value()) {
            return std::nullopt;
        }
        search_options.evaluator = *evaluator;

        return PlayerSpec{.kind = PlayerKind::Search,
                          .depth = *depth,
                          .search_options = search_options,
                          .text = std::string{text}};
    }

    return std::nullopt;
}

SearchOptions make_search_options(const PlayerSpec& spec) noexcept {
    SearchOptions options;
    options.max_depth = spec.search_options.max_depth;
    options.use_transposition_table = spec.search_options.use_transposition_table;
    options.transposition_table_entries = spec.search_options.transposition_table_entries;
    options.store_leaf_tt_entries = spec.search_options.store_leaf_tt_entries;
    options.exact_endgame_empty_threshold = spec.search_options.exact_endgame_empty_threshold;
    options.exact_endgame_root_policy = spec.search_options.exact_endgame_root_policy;
    options.use_pvs = spec.search_options.use_pvs;
    return tools::apply_evaluator_selection(options, spec.search_options.evaluator);
}

} // namespace othello::match_runner
