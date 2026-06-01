#include "evaluation_internal.hpp"

#include <bit>
#include <othello/evaluation.hpp>
#include <othello/rules.hpp>

namespace othello {
namespace {

constexpr int terminal_score_weight = 1000;

[[nodiscard]] EvaluationPhase phase_for_occupied_count(
    int occupied_count, const EvaluationConfig& config) noexcept {
    if (occupied_count <= config.opening_max_occupied) {
        return EvaluationPhase::Opening;
    }
    if (occupied_count <= config.midgame_max_occupied) {
        return EvaluationPhase::Midgame;
    }
    return EvaluationPhase::Late;
}

[[nodiscard]] constexpr const EvaluationFeatureWeights&
weights_for_phase(const EvaluationConfig& config, EvaluationPhase phase) noexcept {
    switch (phase) {
    case EvaluationPhase::Opening:
        return config.opening;
    case EvaluationPhase::Midgame:
        return config.midgame;
    case EvaluationPhase::Late:
        return config.late;
    }

    return config.opening;
}

[[nodiscard]] const PatternTableBundle*
pattern_tables_for_phase(const EvaluationConfig& config, EvaluationPhase phase) noexcept {
    switch (phase) {
    case EvaluationPhase::Opening:
        if (config.opening_pattern_tables != nullptr) {
            return config.opening_pattern_tables.get();
        }
        break;
    case EvaluationPhase::Midgame:
        if (config.midgame_pattern_tables != nullptr) {
            return config.midgame_pattern_tables.get();
        }
        break;
    case EvaluationPhase::Late:
        if (config.late_pattern_tables != nullptr) {
            return config.late_pattern_tables.get();
        }
        break;
    }
    return config.pattern_tables.get();
}

[[nodiscard]] int evaluate_score_only(const Board& board, Side side,
                                      const EvaluationConfig& config) noexcept {
    const int occupied_count = std::popcount(board.occupied());
    const EvaluationPhase phase = phase_for_occupied_count(occupied_count, config);
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);

    if (is_game_over(board)) {
        return score(board, side) * terminal_score_weight;
    }

    int total = 0;
    if (weights.disc_difference != 0) {
        total += evaluate_disc_difference(board, side) * weights.disc_difference;
    }
    if (weights.mobility != 0) {
        total += evaluate_mobility(board, side) * weights.mobility;
    }
    if (weights.corner_occupancy != 0) {
        total += evaluation_detail::corner_occupancy_score(board, side) *
                 weights.corner_occupancy;
    }
    if (weights.potential_mobility != 0) {
        total += evaluation_detail::potential_mobility_score(board, side) *
                 weights.potential_mobility;
    }
    if (weights.corner_access != 0) {
        total += evaluation_detail::corner_access_score(board, side) * weights.corner_access;
    }
    if (weights.x_square_danger != 0) {
        total += evaluation_detail::x_square_danger_score(board, side) *
                 weights.x_square_danger;
    }
    if (weights.frontier != 0) {
        total += evaluation_detail::frontier_score(board, side) * weights.frontier;
    }
    if (weights.corner_local_2x3 != 0) {
        total += evaluation_detail::corner_local_2x3_score(board, side) *
                 weights.corner_local_2x3;
    }
    if (weights.corner_2x3_pattern != 0) {
        total += corner_2x3_pattern_score(board, side) * weights.corner_2x3_pattern;
    }
    if (weights.edge_stability_lite != 0) {
        total += evaluation_detail::edge_stability_lite_score(board, side) *
                 weights.edge_stability_lite;
    }
    if (weights.edge_8_pattern != 0) {
        total += edge_8_pattern_score(board, side) * weights.edge_8_pattern;
    }
    if (weights.pattern_table != 0) {
        const PatternTableBundle* pattern_tables = pattern_tables_for_phase(config, phase);
        if (pattern_tables != nullptr) {
            total += evaluation_pattern_table_score(board, side, *pattern_tables) *
                     weights.pattern_table;
        }
    }
    return total;
}

} // namespace

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                             const EvaluationConfig& config) noexcept {
    const int occupied_count = std::popcount(board.occupied());
    const EvaluationPhase phase = phase_for_occupied_count(occupied_count, config);
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);

    EvaluationBreakdown breakdown{
        .phase = phase,
        .occupied_count = occupied_count,
        .empty_count = 64 - occupied_count,
        .disc_difference_weight = weights.disc_difference,
        .mobility_weight = weights.mobility,
        .corner_occupancy_weight = weights.corner_occupancy,
        .potential_mobility_weight = weights.potential_mobility,
        .corner_access_weight = weights.corner_access,
        .x_square_danger_weight = weights.x_square_danger,
        .frontier_weight = weights.frontier,
        .corner_local_2x3_weight = weights.corner_local_2x3,
        .corner_2x3_pattern_weight = weights.corner_2x3_pattern,
        .edge_stability_lite_weight = weights.edge_stability_lite,
        .edge_8_pattern_weight = weights.edge_8_pattern,
        .pattern_table_weight = weights.pattern_table,
        .terminal_score_weight = terminal_score_weight,
    };

    if (is_game_over(board)) {
        breakdown.terminal = true;
        breakdown.terminal_disc_difference = score(board, side);
        breakdown.terminal_score =
            breakdown.terminal_disc_difference * breakdown.terminal_score_weight;
        breakdown.total = breakdown.terminal_score;
        return breakdown;
    }

    breakdown.disc_difference = evaluate_disc_difference(board, side);
    breakdown.disc_difference_score =
        breakdown.disc_difference * breakdown.disc_difference_weight;

    breakdown.mobility = evaluate_mobility(board, side);
    breakdown.mobility_score = breakdown.mobility * breakdown.mobility_weight;

    breakdown.corner_occupancy = evaluation_detail::corner_occupancy_score(board, side);
    breakdown.corner_occupancy_score =
        breakdown.corner_occupancy * breakdown.corner_occupancy_weight;

    // Raw feature values are positive when they are good for `side`.
    breakdown.potential_mobility = evaluation_detail::potential_mobility_score(board, side);
    breakdown.potential_mobility_score =
        breakdown.potential_mobility * breakdown.potential_mobility_weight;

    breakdown.corner_access = evaluation_detail::corner_access_score(board, side);
    breakdown.corner_access_score = breakdown.corner_access * breakdown.corner_access_weight;

    breakdown.x_square_danger = evaluation_detail::x_square_danger_score(board, side);
    breakdown.x_square_danger_score =
        breakdown.x_square_danger * breakdown.x_square_danger_weight;

    breakdown.frontier = evaluation_detail::frontier_score(board, side);
    breakdown.frontier_score = breakdown.frontier * breakdown.frontier_weight;

    if (breakdown.corner_local_2x3_weight != 0) {
        breakdown.corner_local_2x3 = evaluation_detail::corner_local_2x3_score(board, side);
    }
    breakdown.corner_local_2x3_score =
        breakdown.corner_local_2x3 * breakdown.corner_local_2x3_weight;

    if (breakdown.corner_2x3_pattern_weight != 0) {
        breakdown.corner_2x3_pattern = corner_2x3_pattern_score(board, side);
    }
    breakdown.corner_2x3_pattern_score =
        breakdown.corner_2x3_pattern * breakdown.corner_2x3_pattern_weight;

    if (breakdown.edge_stability_lite_weight != 0) {
        breakdown.edge_stability_lite =
            evaluation_detail::edge_stability_lite_score(board, side);
    }
    breakdown.edge_stability_lite_score =
        breakdown.edge_stability_lite * breakdown.edge_stability_lite_weight;

    if (breakdown.edge_8_pattern_weight != 0) {
        breakdown.edge_8_pattern = edge_8_pattern_score(board, side);
    }
    breakdown.edge_8_pattern_score =
        breakdown.edge_8_pattern * breakdown.edge_8_pattern_weight;

    const PatternTableBundle* pattern_tables =
        breakdown.pattern_table_weight == 0 ? nullptr : pattern_tables_for_phase(config, phase);
    if (pattern_tables != nullptr) {
        breakdown.pattern_table =
            evaluation_pattern_table_score(board, side, *pattern_tables);
    }
    breakdown.pattern_table_score = breakdown.pattern_table * breakdown.pattern_table_weight;

    breakdown.total = breakdown.disc_difference_score + breakdown.mobility_score +
                      breakdown.corner_occupancy_score + breakdown.potential_mobility_score +
                      breakdown.corner_access_score + breakdown.x_square_danger_score +
                      breakdown.frontier_score + breakdown.corner_local_2x3_score +
                      breakdown.corner_2x3_pattern_score +
                      breakdown.edge_stability_lite_score + breakdown.edge_8_pattern_score +
                      breakdown.pattern_table_score;
    return breakdown;
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side) noexcept {
    return evaluate_basic_breakdown(board, side, default_evaluation_config());
}

int evaluate_with_config(const Board& board, Side side, const EvaluationConfig& config) noexcept {
    return evaluate_score_only(board, side, config);
}

int evaluate_basic(const Board& board, Side side) noexcept {
    return evaluate_with_config(board, side, default_evaluation_config());
}

} // namespace othello
