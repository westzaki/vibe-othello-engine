#include "common/stats.hpp"

namespace othello::tools {

double rate(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    if (denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

double percentage(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    return rate(numerator, denominator) * 100.0;
}

double tt_hit_percentage(const SearchStats& stats) noexcept {
    return percentage(stats.tt_hits, stats.tt_lookups);
}

double tt_hit_percentage(const ExactEndgameStats& stats) noexcept {
    return percentage(stats.tt_hits, stats.tt_lookups);
}

void add_search_stats(SearchStats& total, const SearchStats& stats) noexcept {
    total.nodes += stats.nodes;
    total.tt_lookups += stats.tt_lookups;
    total.tt_hits += stats.tt_hits;
    total.tt_exact_hits += stats.tt_exact_hits;
    total.tt_lower_hits += stats.tt_lower_hits;
    total.tt_upper_hits += stats.tt_upper_hits;
    total.tt_stores += stats.tt_stores;
    total.tt_overwrites += stats.tt_overwrites;
    total.tt_collisions += stats.tt_collisions;
    total.tt_rejected_stores += stats.tt_rejected_stores;
    total.tt_move_ordering_probes += stats.tt_move_ordering_probes;
    total.tt_move_ordering_hits += stats.tt_move_ordering_hits;
    total.tt_move_ordering_used += stats.tt_move_ordering_used;
    total.pvs_scouts += stats.pvs_scouts;
    total.pvs_researches += stats.pvs_researches;
    total.pvs_scout_cutoffs += stats.pvs_scout_cutoffs;
    total.dynamic_ordering_nodes += stats.dynamic_ordering_nodes;
    total.dynamic_ordering_moves += stats.dynamic_ordering_moves;
}

void add_exact_endgame_stats(ExactEndgameStats& total,
                             const ExactEndgameStats& stats) noexcept {
    total.nodes += stats.nodes;
    total.tt_lookups += stats.tt_lookups;
    total.tt_hits += stats.tt_hits;
    total.tt_exact_hits += stats.tt_exact_hits;
    total.tt_lower_hits += stats.tt_lower_hits;
    total.tt_upper_hits += stats.tt_upper_hits;
    total.tt_stores += stats.tt_stores;
    total.tt_overwrites += stats.tt_overwrites;
    total.tt_collisions += stats.tt_collisions;
    total.tt_rejected_stores += stats.tt_rejected_stores;
}

} // namespace othello::tools
