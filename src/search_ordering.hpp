#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace othello::search_detail {

struct HistoryKillerOrderingParams {
    int history_cap = 96;
    int history_max_bonus = 12;
    int killer_first_bonus = 8;
    int killer_second_bonus = 4;
};

constexpr HistoryKillerOrderingParams default_history_killer_ordering_params{};

struct HistoryKillerState {
    std::array<int, 64> history{};
    std::array<std::array<int, 2>, 65> killer_moves{};

    HistoryKillerState() noexcept { reset(); }

    void reset() noexcept {
        history.fill(0);
        for (auto& moves : killer_moves) {
            moves.fill(-1);
        }
    }
};

[[nodiscard]] constexpr int depth_bucket(int depth) noexcept {
    if (depth < 0) {
        return 0;
    }
    if (depth > 64) {
        return 64;
    }
    return depth;
}

[[nodiscard]] constexpr int depth_history_increment(int depth) noexcept {
    const int positive_depth = std::max(depth, 1);
    return positive_depth * positive_depth;
}

inline void decay_history(HistoryKillerState& state) noexcept {
    for (int& value : state.history) {
        value /= 2;
    }
}

inline void record_history_cutoff(HistoryKillerState& state, int move_index, int depth,
                                  const HistoryKillerOrderingParams& params =
                                      default_history_killer_ordering_params) noexcept {
    if (move_index < 0 || move_index >= static_cast<int>(state.history.size())) {
        return;
    }

    const int increment = depth_history_increment(depth);
    int& history = state.history[static_cast<std::size_t>(move_index)];
    if (history > params.history_cap - increment) {
        decay_history(state);
    }
    history = std::min(history + increment, params.history_cap);
}

inline void record_killer_cutoff(HistoryKillerState& state, int move_index, int depth) noexcept {
    if (move_index < 0 || move_index >= 64) {
        return;
    }

    auto& killers = state.killer_moves[static_cast<std::size_t>(depth_bucket(depth))];
    if (killers[0] == move_index) {
        return;
    }
    if (killers[1] == move_index) {
        std::swap(killers[0], killers[1]);
        return;
    }

    killers[1] = killers[0];
    killers[0] = move_index;
}

[[nodiscard]] inline int history_killer_bonus(
    const HistoryKillerState& state, int move_index, int depth,
    const HistoryKillerOrderingParams& params =
        default_history_killer_ordering_params) noexcept {
    if (move_index < 0 || move_index >= static_cast<int>(state.history.size())) {
        return 0;
    }

    const int history_bonus = std::min(state.history[static_cast<std::size_t>(move_index)],
                                       params.history_max_bonus);
    const auto& killers = state.killer_moves[static_cast<std::size_t>(depth_bucket(depth))];
    int bonus = history_bonus;
    if (killers[0] == move_index) {
        bonus += params.killer_first_bonus;
    } else if (killers[1] == move_index) {
        bonus += params.killer_second_bonus;
    }
    return bonus;
}

inline void record_history_killer_cutoff(
    HistoryKillerState& state, int move_index, int depth,
    const HistoryKillerOrderingParams& params =
        default_history_killer_ordering_params) noexcept {
    record_history_cutoff(state, move_index, depth, params);
    record_killer_cutoff(state, move_index, depth);
}

} // namespace othello::search_detail
