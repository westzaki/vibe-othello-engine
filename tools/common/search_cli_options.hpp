#pragma once

#include "common/evaluator_selection.hpp"
#include "common/search_preset.hpp"

#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>

namespace othello::tools {

struct SearchCliOptions {
    std::optional<bool> use_transposition_table;
    std::optional<bool> store_leaf_tt_entries;
    std::optional<bool> use_pvs;
    std::optional<bool> use_aspiration_window;
    std::size_t transposition_table_entries = SearchOptions{}.transposition_table_entries;
    int tt_min_probe_depth = SearchOptions{}.tt_min_probe_depth;
    int tt_min_store_depth = SearchOptions{}.tt_min_store_depth;
    std::optional<bool> use_lazy_first_move_ordering;
    std::optional<bool> use_shallow_tt_move_ordering_hint;
    std::optional<std::size_t> exact_endgame_tt_entries = SearchOptions{}.exact_endgame_tt_entries;
    int aspiration_window = SearchOptions{}.aspiration_window;
    int aspiration_max_researches = SearchOptions{}.aspiration_max_researches;
    AspirationProfile aspiration_profile = SearchOptions{}.aspiration_profile;
};

enum class SearchCliParseResult {
    Parsed,
    NotSearchOption,
    Error,
};

struct SearchCliParseOptions {
    bool parse_tt_store_leaf = true;
};

struct NBoardSearchCliOptions {
    int exact_endgame_empty_threshold = 12;
    bool exact_endgame_threshold_overridden = false;
    SearchPreset preset = SearchPreset::Default;
};

[[nodiscard]] std::string_view aspiration_profile_name(AspirationProfile profile) noexcept;
[[nodiscard]] std::optional<AspirationProfile>
parse_aspiration_profile(std::string_view text) noexcept;

[[nodiscard]] SearchCliParseResult
parse_search_cli_option(std::span<char* const> args, std::size_t& index, SearchCliOptions& options,
                        std::string& error, SearchCliParseOptions parse_options = {});

[[nodiscard]] SearchCliParseResult parse_nboard_search_cli_option(std::span<char* const> args,
                                                                  std::size_t& index,
                                                                  NBoardSearchCliOptions& options,
                                                                  std::string& error);

[[nodiscard]] SearchOptions apply_search_cli_options(SearchOptions options,
                                                     const SearchCliOptions& cli_options) noexcept;

[[nodiscard]] SearchOptions
make_search_options_from_preset(SearchPreset preset, int max_depth,
                                std::optional<int> exact_endgame_empty_threshold,
                                const EvaluatorSelection& evaluator) noexcept;

} // namespace othello::tools
