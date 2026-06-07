#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace othello {

struct SearchStats {
    std::uint64_t nodes = 0;
    // Depth-limited search observability. game_over_nodes counts terminal
    // no-move/no-pass nodes reached by search, not an eager probe on every node.
    std::uint64_t beta_cutoffs = 0;
    std::uint64_t beta_cutoffs_first_move = 0;
    std::uint64_t searched_moves = 0;
    std::uint64_t legal_move_nodes = 0;
    std::uint64_t eval_calls = 0;
    std::uint64_t pass_nodes = 0;
    std::uint64_t game_over_nodes = 0;

    std::uint64_t tt_lookups = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_exact_hits = 0;
    std::uint64_t tt_lower_hits = 0;
    std::uint64_t tt_upper_hits = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t tt_leaf_stores = 0;
    std::uint64_t tt_leaf_store_skipped = 0;
    std::uint64_t tt_probe_skipped_by_depth = 0;
    std::uint64_t tt_store_skipped_by_depth = 0;
    std::uint64_t tt_overwrites = 0;
    std::uint64_t tt_collisions = 0;
    std::uint64_t tt_rejected_stores = 0;
    std::uint64_t tt_move_ordering_probes = 0;
    std::uint64_t tt_move_ordering_hits = 0;
    std::uint64_t tt_move_ordering_used = 0;

    std::uint64_t pvs_scouts = 0;
    std::uint64_t pvs_researches = 0;
    std::uint64_t pvs_scout_cutoffs = 0;

    std::uint64_t aspiration_searches = 0;
    std::uint64_t aspiration_researches = 0;
    std::uint64_t aspiration_fail_lows = 0;
    std::uint64_t aspiration_fail_highs = 0;
    std::uint64_t aspiration_full_window_fallbacks = 0;
    std::uint64_t aspiration_fail_low_distance_sum = 0;
    std::uint64_t aspiration_fail_high_distance_sum = 0;
    // Cumulative record distances for failed aspiration searches. Aggregating
    // stats keeps the maximum; a delta reports the new cumulative maximum only
    // when the interval raised the previous record.
    std::uint64_t aspiration_fail_low_distance_max = 0;
    std::uint64_t aspiration_fail_high_distance_max = 0;

    std::uint64_t dynamic_ordering_nodes = 0;
    std::uint64_t dynamic_ordering_moves = 0;
};

namespace search_stats_detail {

inline constexpr auto additive_members = std::to_array<std::uint64_t SearchStats::*>({
    &SearchStats::nodes,
    &SearchStats::beta_cutoffs,
    &SearchStats::beta_cutoffs_first_move,
    &SearchStats::searched_moves,
    &SearchStats::legal_move_nodes,
    &SearchStats::eval_calls,
    &SearchStats::pass_nodes,
    &SearchStats::game_over_nodes,
    &SearchStats::tt_lookups,
    &SearchStats::tt_hits,
    &SearchStats::tt_exact_hits,
    &SearchStats::tt_lower_hits,
    &SearchStats::tt_upper_hits,
    &SearchStats::tt_stores,
    &SearchStats::tt_leaf_stores,
    &SearchStats::tt_leaf_store_skipped,
    &SearchStats::tt_probe_skipped_by_depth,
    &SearchStats::tt_store_skipped_by_depth,
    &SearchStats::tt_overwrites,
    &SearchStats::tt_collisions,
    &SearchStats::tt_rejected_stores,
    &SearchStats::tt_move_ordering_probes,
    &SearchStats::tt_move_ordering_hits,
    &SearchStats::tt_move_ordering_used,
    &SearchStats::pvs_scouts,
    &SearchStats::pvs_researches,
    &SearchStats::pvs_scout_cutoffs,
    &SearchStats::aspiration_searches,
    &SearchStats::aspiration_researches,
    &SearchStats::aspiration_fail_lows,
    &SearchStats::aspiration_fail_highs,
    &SearchStats::aspiration_full_window_fallbacks,
    &SearchStats::aspiration_fail_low_distance_sum,
    &SearchStats::aspiration_fail_high_distance_sum,
    &SearchStats::dynamic_ordering_nodes,
    &SearchStats::dynamic_ordering_moves,
});

inline constexpr auto max_members = std::to_array<std::uint64_t SearchStats::*>({
    &SearchStats::aspiration_fail_low_distance_max,
    &SearchStats::aspiration_fail_high_distance_max,
});

inline constexpr std::size_t tracked_member_count = additive_members.size() + max_members.size();

} // namespace search_stats_detail

inline void accumulate_search_stats(SearchStats& total, const SearchStats& stats) noexcept {
    for (const auto member : search_stats_detail::additive_members) {
        total.*member += stats.*member;
    }
    for (const auto member : search_stats_detail::max_members) {
        total.*member = std::max(total.*member, stats.*member);
    }
}

[[nodiscard]] inline SearchStats search_stats_delta(const SearchStats& after,
                                                    const SearchStats& before) noexcept {
    SearchStats delta;
    for (const auto member : search_stats_detail::additive_members) {
        delta.*member = after.*member - before.*member;
    }
    for (const auto member : search_stats_detail::max_members) {
        delta.*member = after.*member > before.*member ? after.*member : 0;
    }
    return delta;
}

} // namespace othello
