#pragma once

#include <cstddef>
#include <optional>
#include <othello/search.hpp>
#include <vector>

namespace othello::search_detail {

struct SearchEngineOptions {
    bool use_transposition_table = false;
    std::size_t transposition_table_entries = 1 << 18;
    bool store_leaf_tt_entries = true;
    int tt_min_probe_depth = 0;
    int tt_min_store_depth = 0;
    int exact_endgame_empty_threshold = 12;
    ExactEndgameRootPolicy exact_endgame_root_policy = ExactEndgameRootPolicy::FixedThreshold;
    std::optional<std::size_t> exact_endgame_tt_entries = std::nullopt;
    bool use_pvs = false;
    bool use_aspiration_window = false;
    int aspiration_window = 50;
    int aspiration_max_researches = 4;
    AspirationProfile aspiration_profile = AspirationProfile::Fixed;
};

struct SearchDiagnosticsOptions {
    IterativeSearchDepthObserver iterative_depth_observer = nullptr;
    void* iterative_depth_observer_user_data = nullptr;
    std::vector<RootMoveOrderingEntry>* root_move_ordering_snapshot = nullptr;
};

[[nodiscard]] constexpr SearchEngineOptions
engine_options_from(const SearchOptions& options) noexcept {
    return SearchEngineOptions{
        .use_transposition_table = options.use_transposition_table,
        .transposition_table_entries = options.transposition_table_entries,
        .store_leaf_tt_entries = options.store_leaf_tt_entries,
        .tt_min_probe_depth = options.tt_min_probe_depth,
        .tt_min_store_depth = options.tt_min_store_depth,
        .exact_endgame_empty_threshold = options.exact_endgame_empty_threshold,
        .exact_endgame_root_policy = options.exact_endgame_root_policy,
        .exact_endgame_tt_entries = options.exact_endgame_tt_entries,
        .use_pvs = options.use_pvs,
        .use_aspiration_window = options.use_aspiration_window,
        .aspiration_window = options.aspiration_window,
        .aspiration_max_researches = options.aspiration_max_researches,
        .aspiration_profile = options.aspiration_profile,
    };
}

[[nodiscard]] constexpr SearchDiagnosticsOptions
diagnostics_options_from(const SearchOptions& options) noexcept {
    const SearchInstrumentation& instrumentation = options.instrumentation;
    return SearchDiagnosticsOptions{
        .iterative_depth_observer = instrumentation.iterative_depth_observer,
        .iterative_depth_observer_user_data = instrumentation.iterative_depth_observer_user_data,
        .root_move_ordering_snapshot = instrumentation.root_move_ordering_snapshot,
    };
}

} // namespace othello::search_detail
