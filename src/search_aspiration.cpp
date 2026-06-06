#include "search_aspiration.hpp"

#include "search_core.hpp"

#include <algorithm>
#include <cstdint>

namespace othello::search_detail {
namespace {

[[nodiscard]] constexpr int non_negative_research_limit(int researches) noexcept {
    return researches < 0 ? 0 : researches;
}

[[nodiscard]] constexpr int aspiration_alpha(int previous_score, int window) noexcept {
    return clamp_search_score(static_cast<long long>(previous_score) - window);
}

[[nodiscard]] constexpr int aspiration_beta(int previous_score, int window) noexcept {
    return clamp_search_score(static_cast<long long>(previous_score) + window);
}

[[nodiscard]] constexpr int widened_aspiration_window(int window) noexcept {
    if (window >= search_score_max / 2) {
        return search_score_max;
    }
    return window * 2;
}

[[nodiscard]] bool failed_low(const SearchResult& result, int alpha) noexcept {
    return result.score <= alpha;
}

[[nodiscard]] bool failed_high(const SearchResult& result, int beta) noexcept {
    return result.score >= beta;
}

void record_aspiration_fail_low_distance(SearchContext& context, int score, int alpha) noexcept {
    const std::uint64_t distance =
        static_cast<std::uint64_t>(alpha > score ? alpha - score : 0);
    context.stats.aspiration_fail_low_distance_sum += distance;
    context.stats.aspiration_fail_low_distance_max =
        std::max(context.stats.aspiration_fail_low_distance_max, distance);
}

void record_aspiration_fail_high_distance(SearchContext& context, int score, int beta) noexcept {
    const std::uint64_t distance =
        static_cast<std::uint64_t>(score > beta ? score - beta : 0);
    context.stats.aspiration_fail_high_distance_sum += distance;
    context.stats.aspiration_fail_high_distance_max =
        std::max(context.stats.aspiration_fail_high_distance_max, distance);
}

} // namespace

SearchResult search_aspirated_with_context(
    const Board& board, int depth, SearchContext& context, std::optional<Square> previous_best_move,
    PrincipalVariationHint pv_hint, int previous_score, int initial_window,
    const SearchEngineOptions& options) noexcept {
    ++context.stats.aspiration_searches;

    int window = positive_aspiration_window(initial_window);
    const int max_researches = non_negative_research_limit(options.aspiration_max_researches);
    int researches = 0;

    while (true) {
        const int alpha = aspiration_alpha(previous_score, window);
        const int beta = aspiration_beta(previous_score, window);
        const SearchResult result =
            search_with_context(board, depth, context, alpha, beta, previous_best_move, pv_hint);

        if (!failed_low(result, alpha) && !failed_high(result, beta)) {
            return result;
        }

        if (failed_low(result, alpha)) {
            ++context.stats.aspiration_fail_lows;
            record_aspiration_fail_low_distance(context, result.score, alpha);
        } else {
            ++context.stats.aspiration_fail_highs;
            record_aspiration_fail_high_distance(context, result.score, beta);
        }

        ++context.stats.aspiration_researches;
        if (researches >= max_researches) {
            ++context.stats.aspiration_full_window_fallbacks;
            return search_with_context(board, depth, context, search_score_min, search_score_max,
                                       previous_best_move, pv_hint);
        }

        ++researches;
        // Keep the policy easy to audit: re-center on the previous iteration's score
        // and double the symmetric half-window on each failed attempt.
        window = widened_aspiration_window(window);
    }
}

} // namespace othello::search_detail
