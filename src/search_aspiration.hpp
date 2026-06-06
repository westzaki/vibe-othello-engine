#pragma once

#include "search_bounds.hpp"
#include "search_context.hpp"
#include "search_runtime_options.hpp"

#include <algorithm>
#include <optional>
#include <othello/board.hpp>
#include <othello/search.hpp>
#include <othello/square.hpp>

namespace othello::search_detail {

[[nodiscard]] SearchResult search_aspirated_with_context(
    const Board& board, int depth, SearchContext& context, std::optional<Square> previous_best_move,
    PrincipalVariationHint pv_hint, int previous_score, int initial_window,
    const SearchEngineOptions& options) noexcept;

[[nodiscard]] constexpr int positive_aspiration_window(int window) noexcept {
    return window <= 0 ? 1 : window;
}

[[nodiscard]] constexpr int aspiration_initial_window(
    const SearchEngineOptions& options, std::optional<int> previous_score_delta) noexcept {
    const int base_window = positive_aspiration_window(options.aspiration_window);
    if (options.aspiration_profile == AspirationProfile::Fixed ||
        !previous_score_delta.has_value()) {
        return base_window;
    }

    long long delta = *previous_score_delta;
    if (delta < 0) {
        delta = -delta;
    }
    if (delta <= static_cast<long long>(base_window) * 2) {
        return base_window;
    }

    const long long widened = delta + base_window / 2;
    const long long max_dynamic_window = static_cast<long long>(base_window) * 2;
    return clamp_search_score(std::min(widened, max_dynamic_window));
}

} // namespace othello::search_detail
