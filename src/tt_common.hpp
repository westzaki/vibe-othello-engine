#pragma once

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

} // namespace othello::tt_detail
