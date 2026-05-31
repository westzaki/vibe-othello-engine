#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace othello::tt_detail {

enum class BoundKind : std::uint8_t {
    Exact,
    Lower,
    Upper,
};

[[nodiscard]] constexpr BoundKind bound_for_score(int score, int original_alpha,
                                                  int beta) noexcept {
    if (score <= original_alpha) {
        return BoundKind::Upper;
    }
    if (score >= beta) {
        return BoundKind::Lower;
    }
    return BoundKind::Exact;
}

[[nodiscard]] constexpr bool proves_cutoff(BoundKind bound, int score, int alpha,
                                           int beta) noexcept {
    switch (bound) {
    case BoundKind::Exact:
        return true;
    case BoundKind::Lower:
        return score >= beta;
    case BoundKind::Upper:
        return score <= alpha;
    }
    return false;
}

[[nodiscard]] constexpr std::size_t
bucket_count_for_entry_count(std::size_t entry_count, std::size_t bucket_width) noexcept {
    if (entry_count == 0 || bucket_width == 0) {
        return 0;
    }
    const std::size_t requested_buckets = ((entry_count - 1) / bucket_width) + 1;
    return std::bit_ceil(requested_buckets);
}

static_assert(bucket_count_for_entry_count(0, 4) == 0);
static_assert(bucket_count_for_entry_count(1, 4) == 1);
static_assert(bucket_count_for_entry_count(4, 4) == 1);
static_assert(bucket_count_for_entry_count(5, 4) == 2);
static_assert(bucket_count_for_entry_count(17, 4) == 8);

} // namespace othello::tt_detail
