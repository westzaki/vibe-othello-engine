#pragma once

namespace othello::search_detail {

constexpr int search_score_min = -1'000'000'000;
constexpr int search_score_max = 1'000'000'000;

[[nodiscard]] constexpr int clamp_search_score(long long score) noexcept {
    if (score < search_score_min) {
        return search_score_min;
    }
    if (score > search_score_max) {
        return search_score_max;
    }
    return static_cast<int>(score);
}

} // namespace othello::search_detail
