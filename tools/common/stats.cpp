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

double beta_cut_first_move_percentage(const SearchStats& stats) noexcept {
    return percentage(stats.beta_cutoffs_first_move, stats.beta_cutoffs);
}

void add_search_stats(SearchStats& total, const SearchStats& stats) noexcept {
    othello::accumulate_search_stats(total, stats);
}

void add_exact_endgame_stats(ExactEndgameStats& total, const ExactEndgameStats& stats) noexcept {
    othello::accumulate_exact_endgame_stats(total, stats);
}

} // namespace othello::tools
