#include "evaluation_internal.hpp"

#include <othello/evaluation.hpp>

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

struct NonTerminalEvaluation {
    int disc_difference = 0;
    int disc_difference_score = 0;
    int mobility = 0;
    int mobility_score = 0;
    int corner_occupancy = 0;
    int corner_occupancy_score = 0;
    int potential_mobility = 0;
    int potential_mobility_score = 0;
    int corner_access = 0;
    int corner_access_score = 0;
    int x_square_danger = 0;
    int x_square_danger_score = 0;
    int frontier = 0;
    int frontier_score = 0;
    int corner_local_2x3 = 0;
    int corner_local_2x3_score = 0;
    int corner_2x3_pattern = 0;
    int corner_2x3_pattern_score = 0;
    int edge_stability_lite = 0;
    int edge_stability_lite_score = 0;
    int edge_8_pattern = 0;
    int edge_8_pattern_score = 0;
    int pattern_table = 0;
    int pattern_table_score = 0;
    int total = 0;
};

enum class RawFeatureMode {
    ScoreOnly,
    Breakdown,
};

using ScratchFeatureFunction =
    int (*)(const evaluation_detail::EvaluationScratch&) noexcept;

void accumulate_feature(const evaluation_detail::EvaluationScratch& scratch, int weight,
                        bool compute_when_unweighted, ScratchFeatureFunction compute,
                        int& value, int& weighted_score, int& total) noexcept {
    if (weight == 0 && !compute_when_unweighted) {
        return;
    }
    value = compute(scratch);
    weighted_score = value * weight;
    total += weighted_score;
}

[[nodiscard]] NonTerminalEvaluation accumulate_non_terminal_evaluation(
    Bitboard player, Bitboard opponent,
    const evaluation_detail::EvaluationScratch& scratch, const EvaluationConfig& config,
    EvaluationPhase phase, RawFeatureMode mode) noexcept {
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);
    const bool preserve_breakdown_raw_values = mode == RawFeatureMode::Breakdown;

    NonTerminalEvaluation result;
    accumulate_feature(scratch, weights.disc_difference,
                       preserve_breakdown_raw_values,
                       evaluation_detail::disc_difference_score,
                       result.disc_difference, result.disc_difference_score, result.total);
    accumulate_feature(scratch, weights.mobility, preserve_breakdown_raw_values,
                       evaluation_detail::mobility_score, result.mobility,
                       result.mobility_score, result.total);
    accumulate_feature(scratch, weights.corner_occupancy,
                       preserve_breakdown_raw_values,
                       evaluation_detail::corner_occupancy_score,
                       result.corner_occupancy, result.corner_occupancy_score,
                       result.total);
    accumulate_feature(scratch, weights.potential_mobility,
                       preserve_breakdown_raw_values,
                       evaluation_detail::potential_mobility_score,
                       result.potential_mobility, result.potential_mobility_score,
                       result.total);
    accumulate_feature(scratch, weights.corner_access,
                       preserve_breakdown_raw_values, evaluation_detail::corner_access_score,
                       result.corner_access, result.corner_access_score, result.total);
    accumulate_feature(scratch, weights.x_square_danger,
                       preserve_breakdown_raw_values,
                       evaluation_detail::x_square_danger_score,
                       result.x_square_danger, result.x_square_danger_score, result.total);
    accumulate_feature(scratch, weights.frontier, preserve_breakdown_raw_values,
                       evaluation_detail::frontier_score, result.frontier,
                       result.frontier_score, result.total);
    accumulate_feature(scratch, weights.corner_local_2x3, false,
                       evaluation_detail::corner_local_2x3_score,
                       result.corner_local_2x3, result.corner_local_2x3_score,
                       result.total);
    if (weights.corner_2x3_pattern != 0) {
        result.corner_2x3_pattern =
            evaluation_detail::corner_2x3_pattern_score(player, opponent);
        result.corner_2x3_pattern_score =
            result.corner_2x3_pattern * weights.corner_2x3_pattern;
        result.total += result.corner_2x3_pattern_score;
    }
    accumulate_feature(scratch, weights.edge_stability_lite, false,
                       evaluation_detail::edge_stability_lite_score,
                       result.edge_stability_lite, result.edge_stability_lite_score,
                       result.total);
    if (weights.edge_8_pattern != 0) {
        result.edge_8_pattern = evaluation_detail::edge_8_pattern_score(player, opponent);
        result.edge_8_pattern_score = result.edge_8_pattern * weights.edge_8_pattern;
        result.total += result.edge_8_pattern_score;
    }

    if (weights.pattern_table != 0) {
        const PatternTableBundle* pattern_tables = pattern_tables_for_phase(config, phase);
        if (pattern_tables != nullptr) {
            result.pattern_table =
                evaluation_detail::evaluation_pattern_table_score(player, opponent,
                                                                  *pattern_tables);
            result.pattern_table_score = result.pattern_table * weights.pattern_table;
            result.total += result.pattern_table_score;
        }
    }

    return result;
}

[[nodiscard]] int evaluate_score_only(Bitboard player, Bitboard opponent,
                                      const EvaluationConfig& config) noexcept {
    const evaluation_detail::EvaluationScratch scratch =
        evaluation_detail::make_evaluation_scratch(player, opponent);
    const EvaluationPhase phase = phase_for_occupied_count(scratch.occupied_count, config);

    if (scratch.game_over) {
        return evaluation_detail::disc_difference_score(scratch) * terminal_score_weight;
    }

    return accumulate_non_terminal_evaluation(
               player, opponent, scratch, config, phase, RawFeatureMode::ScoreOnly)
        .total;
}

} // namespace

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                             const EvaluationConfig& config) noexcept {
    const evaluation_detail::EvaluationScratch scratch =
        evaluation_detail::make_evaluation_scratch(board, side);
    const EvaluationPhase phase = phase_for_occupied_count(scratch.occupied_count, config);
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);

    EvaluationBreakdown breakdown{
        .phase = phase,
        .occupied_count = scratch.occupied_count,
        .empty_count = scratch.empty_count,
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

    if (scratch.game_over) {
        breakdown.terminal = true;
        breakdown.terminal_disc_difference =
            evaluation_detail::disc_difference_score(scratch);
        breakdown.terminal_score =
            breakdown.terminal_disc_difference * breakdown.terminal_score_weight;
        breakdown.total = breakdown.terminal_score;
        return breakdown;
    }

    const NonTerminalEvaluation features = accumulate_non_terminal_evaluation(
        scratch.player, scratch.opponent, scratch, config, phase, RawFeatureMode::Breakdown);
    breakdown.disc_difference = features.disc_difference;
    breakdown.disc_difference_score = features.disc_difference_score;
    breakdown.mobility = features.mobility;
    breakdown.mobility_score = features.mobility_score;
    breakdown.corner_occupancy = features.corner_occupancy;
    breakdown.corner_occupancy_score = features.corner_occupancy_score;
    breakdown.potential_mobility = features.potential_mobility;
    breakdown.potential_mobility_score = features.potential_mobility_score;
    breakdown.corner_access = features.corner_access;
    breakdown.corner_access_score = features.corner_access_score;
    breakdown.x_square_danger = features.x_square_danger;
    breakdown.x_square_danger_score = features.x_square_danger_score;
    breakdown.frontier = features.frontier;
    breakdown.frontier_score = features.frontier_score;
    breakdown.corner_local_2x3 = features.corner_local_2x3;
    breakdown.corner_local_2x3_score = features.corner_local_2x3_score;
    breakdown.corner_2x3_pattern = features.corner_2x3_pattern;
    breakdown.corner_2x3_pattern_score = features.corner_2x3_pattern_score;
    breakdown.edge_stability_lite = features.edge_stability_lite;
    breakdown.edge_stability_lite_score = features.edge_stability_lite_score;
    breakdown.edge_8_pattern = features.edge_8_pattern;
    breakdown.edge_8_pattern_score = features.edge_8_pattern_score;
    breakdown.pattern_table = features.pattern_table;
    breakdown.pattern_table_score = features.pattern_table_score;
    breakdown.total = features.total;
    return breakdown;
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side) noexcept {
    return evaluate_basic_breakdown(board, side, default_evaluation_config());
}

int evaluate_with_config(const Board& board, Side side, const EvaluationConfig& config) noexcept {
    return evaluation_detail::evaluate_with_config(board.discs(side),
                                                   board.discs(opponent(side)), config);
}

int evaluate_basic(const Board& board, Side side) noexcept {
    return evaluate_with_config(board, side, default_evaluation_config());
}

namespace evaluation_detail {

int evaluate_with_config(Bitboard player, Bitboard opponent,
                         const EvaluationConfig& config) noexcept {
    return evaluate_score_only(player, opponent, config);
}

} // namespace evaluation_detail

} // namespace othello
