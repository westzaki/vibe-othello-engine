#include "evaluation_internal.hpp"

#include <othello/evaluation.hpp>
#include <othello/evaluation_feature_specs.hpp>

#include <array>
#include <bit>

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

[[nodiscard]] bool has_pattern_table_feature_surface(const EvaluationConfig& config) noexcept {
    return config.opening.pattern_table != 0 || config.midgame.pattern_table != 0 ||
           config.late.pattern_table != 0 || config.pattern_tables != nullptr ||
           config.opening_pattern_tables != nullptr || config.midgame_pattern_tables != nullptr ||
           config.late_pattern_tables != nullptr;
}

[[nodiscard]] constexpr bool phase_fast_path_enabled(
    evaluation_detail::PatternTableScoreOnlyFastPathPhases phases,
    EvaluationPhase phase) noexcept {
    switch (phase) {
    case EvaluationPhase::Opening:
        return phases.opening;
    case EvaluationPhase::Midgame:
        return phases.midgame;
    case EvaluationPhase::Late:
        return phases.late;
    }

    return false;
}

struct NonTerminalEvaluation {
    struct FeatureValue {
        int raw_value = 0;
        int weight = 0;
        int weighted_score = 0;
    };

    std::array<FeatureValue, evaluation_detail::evaluation_feature_count> features{};
    int total = 0;
};

enum class RawFeatureMode {
    ScoreOnly,
    Breakdown,
};

[[nodiscard]] int compute_feature_raw_value(
    const evaluation_detail::EvaluationFeatureSpec& spec, Bitboard player,
    Bitboard opponent, const evaluation_detail::EvaluationScratch& scratch,
    const EvaluationConfig& config, EvaluationPhase phase) noexcept {
    switch (spec.computation) {
    case evaluation_detail::EvaluationFeatureComputation::DiscDifference:
        return evaluation_detail::disc_difference_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::Mobility:
        return evaluation_detail::mobility_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::PotentialMobility:
        return evaluation_detail::potential_mobility_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::CornerOccupancy:
        return evaluation_detail::corner_occupancy_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::CornerAccess:
        return evaluation_detail::corner_access_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::XSquareDanger:
        return evaluation_detail::x_square_danger_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::Frontier:
        return evaluation_detail::frontier_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::CornerLocal2x3:
        return evaluation_detail::corner_local_2x3_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::Corner2x3Pattern:
        return evaluation_detail::corner_2x3_pattern_score(player, opponent);
    case evaluation_detail::EvaluationFeatureComputation::EdgeStabilityLite:
        return evaluation_detail::edge_stability_lite_score(scratch);
    case evaluation_detail::EvaluationFeatureComputation::Edge8Pattern:
        return evaluation_detail::edge_8_pattern_score(player, opponent);
    case evaluation_detail::EvaluationFeatureComputation::PatternTable:
        if (const PatternTableBundle* pattern_tables =
                pattern_tables_for_phase(config, phase);
            pattern_tables != nullptr) {
            return evaluation_detail::evaluation_pattern_table_score(player, opponent,
                                                                     *pattern_tables);
        }
        return 0;
    }

    return 0;
}

[[nodiscard]] NonTerminalEvaluation accumulate_non_terminal_evaluation(
    Bitboard player, Bitboard opponent,
    const evaluation_detail::EvaluationScratch& scratch, const EvaluationConfig& config,
    EvaluationPhase phase, RawFeatureMode mode) noexcept {
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);
    const bool preserve_breakdown_raw_values = mode == RawFeatureMode::Breakdown;

    NonTerminalEvaluation result;
    for (std::size_t index = 0; index < evaluation_detail::evaluation_feature_count;
         ++index) {
        const evaluation_detail::EvaluationFeatureSpec& spec =
            evaluation_detail::evaluation_feature_specs[index];
        NonTerminalEvaluation::FeatureValue& feature = result.features[index];
        feature.weight = weights.*spec.weight;
        const bool compute_unweighted =
            preserve_breakdown_raw_values && spec.compute_when_unweighted_breakdown;
        if (feature.weight == 0 && !compute_unweighted) {
            continue;
        }
        feature.raw_value = compute_feature_raw_value(spec, player, opponent, scratch,
                                                      config, phase);
        feature.weighted_score = feature.raw_value * feature.weight;
        result.total += feature.weighted_score;
    }

    return result;
}

void assign_breakdown_feature_weights(EvaluationBreakdown& breakdown,
                                      const EvaluationFeatureWeights& weights) noexcept {
    for (const evaluation_detail::EvaluationFeatureSpec& spec :
         evaluation_detail::evaluation_feature_specs) {
        breakdown.*spec.breakdown_weight = weights.*spec.weight;
    }
}

void assign_breakdown_feature_values(
    EvaluationBreakdown& breakdown, const NonTerminalEvaluation& features) noexcept {
    for (std::size_t index = 0; index < evaluation_detail::evaluation_feature_count;
         ++index) {
        const evaluation_detail::EvaluationFeatureSpec& spec =
            evaluation_detail::evaluation_feature_specs[index];
        const NonTerminalEvaluation::FeatureValue& feature = features.features[index];
        breakdown.*spec.raw_value = feature.raw_value;
        breakdown.*spec.weighted_score = feature.weighted_score;
    }
}

[[nodiscard]] int evaluate_score_only(
    Bitboard player, Bitboard opponent, const EvaluationConfig& config,
    evaluation_detail::PatternTableScoreOnlyFastPathPhases fast_path_phases) noexcept {
    if (fast_path_phases.any()) {
        const int occupied_count = std::popcount(player | opponent);
        const EvaluationPhase phase = phase_for_occupied_count(occupied_count, config);

        if (phase_fast_path_enabled(fast_path_phases, phase)) {
            return evaluation_detail::evaluate_pattern_table_score_only_fast_path(
                player, opponent, config, phase);
        }
    }

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
        .terminal_score_weight = terminal_score_weight,
    };
    assign_breakdown_feature_weights(breakdown, weights);

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
    assign_breakdown_feature_values(breakdown, features);
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

bool can_use_pattern_table_score_only_fast_path(
    const EvaluationConfig& config, EvaluationPhase phase) noexcept {
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);
    for (const EvaluationFeatureSpec& spec : evaluation_feature_specs) {
        if (spec.computation == EvaluationFeatureComputation::PatternTable) {
            continue;
        }
        if (weights.*spec.weight != 0) {
            return false;
        }
    }
    return true;
}

PatternTableScoreOnlyFastPathPhases pattern_table_score_only_fast_path_phases(
    const EvaluationConfig& config) noexcept {
    return PatternTableScoreOnlyFastPathPhases{
        .opening = can_use_pattern_table_score_only_fast_path(config,
                                                              EvaluationPhase::Opening),
        .midgame = can_use_pattern_table_score_only_fast_path(config,
                                                              EvaluationPhase::Midgame),
        .late = can_use_pattern_table_score_only_fast_path(config, EvaluationPhase::Late),
    };
}

int evaluate_pattern_table_score_only_fast_path(
    Bitboard player, Bitboard opponent, const EvaluationConfig& config,
    EvaluationPhase phase) noexcept {
    if (game_over_for_bitboards(player, opponent)) {
        return (std::popcount(player) - std::popcount(opponent)) *
               terminal_score_weight;
    }

    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);
    if (weights.pattern_table == 0) {
        return 0;
    }

    const PatternTableBundle* pattern_tables = pattern_tables_for_phase(config, phase);
    if (pattern_tables == nullptr) {
        return 0;
    }

    return evaluation_pattern_table_score(player, opponent, *pattern_tables) *
           weights.pattern_table;
}

int evaluate_with_config(Bitboard player, Bitboard opponent,
                         const EvaluationConfig& config) noexcept {
    if (!has_pattern_table_feature_surface(config)) {
        return evaluate_score_only(player, opponent, config,
                                   PatternTableScoreOnlyFastPathPhases{});
    }
    return evaluate_score_only(player, opponent, config,
                               pattern_table_score_only_fast_path_phases(config));
}

int evaluate_with_config(
    Bitboard player, Bitboard opponent, const EvaluationConfig& config,
    PatternTableScoreOnlyFastPathPhases fast_path_phases) noexcept {
    return evaluate_score_only(player, opponent, config, fast_path_phases);
}

} // namespace evaluation_detail

} // namespace othello
