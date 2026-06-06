#pragma once

#include <othello/evaluation.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace othello::evaluation_detail {

enum class EvaluationFeatureComputation {
    DiscDifference,
    Mobility,
    PotentialMobility,
    CornerOccupancy,
    CornerAccess,
    XSquareDanger,
    Frontier,
    CornerLocal2x3,
    Corner2x3Pattern,
    EdgeStabilityLite,
    Edge8Pattern,
    PatternTable,
};

struct EvaluationFeatureSpec {
    std::string_view key;
    int EvaluationFeatureWeights::*weight;
    int EvaluationBreakdown::*raw_value;
    int EvaluationBreakdown::*breakdown_weight;
    int EvaluationBreakdown::*weighted_score;
    EvaluationFeatureComputation computation;
    bool compute_when_unweighted_breakdown = false;
    bool required_in_full_config = true;
};

inline constexpr std::array<EvaluationFeatureSpec, 12> evaluation_feature_specs{{
    {.key = "disc_difference",
     .weight = &EvaluationFeatureWeights::disc_difference,
     .raw_value = &EvaluationBreakdown::disc_difference,
     .breakdown_weight = &EvaluationBreakdown::disc_difference_weight,
     .weighted_score = &EvaluationBreakdown::disc_difference_score,
     .computation = EvaluationFeatureComputation::DiscDifference,
     .compute_when_unweighted_breakdown = true},
    {.key = "mobility",
     .weight = &EvaluationFeatureWeights::mobility,
     .raw_value = &EvaluationBreakdown::mobility,
     .breakdown_weight = &EvaluationBreakdown::mobility_weight,
     .weighted_score = &EvaluationBreakdown::mobility_score,
     .computation = EvaluationFeatureComputation::Mobility,
     .compute_when_unweighted_breakdown = true},
    {.key = "potential_mobility",
     .weight = &EvaluationFeatureWeights::potential_mobility,
     .raw_value = &EvaluationBreakdown::potential_mobility,
     .breakdown_weight = &EvaluationBreakdown::potential_mobility_weight,
     .weighted_score = &EvaluationBreakdown::potential_mobility_score,
     .computation = EvaluationFeatureComputation::PotentialMobility,
     .compute_when_unweighted_breakdown = true},
    {.key = "corner_occupancy",
     .weight = &EvaluationFeatureWeights::corner_occupancy,
     .raw_value = &EvaluationBreakdown::corner_occupancy,
     .breakdown_weight = &EvaluationBreakdown::corner_occupancy_weight,
     .weighted_score = &EvaluationBreakdown::corner_occupancy_score,
     .computation = EvaluationFeatureComputation::CornerOccupancy,
     .compute_when_unweighted_breakdown = true},
    {.key = "corner_access",
     .weight = &EvaluationFeatureWeights::corner_access,
     .raw_value = &EvaluationBreakdown::corner_access,
     .breakdown_weight = &EvaluationBreakdown::corner_access_weight,
     .weighted_score = &EvaluationBreakdown::corner_access_score,
     .computation = EvaluationFeatureComputation::CornerAccess,
     .compute_when_unweighted_breakdown = true},
    {.key = "x_square_danger",
     .weight = &EvaluationFeatureWeights::x_square_danger,
     .raw_value = &EvaluationBreakdown::x_square_danger,
     .breakdown_weight = &EvaluationBreakdown::x_square_danger_weight,
     .weighted_score = &EvaluationBreakdown::x_square_danger_score,
     .computation = EvaluationFeatureComputation::XSquareDanger,
     .compute_when_unweighted_breakdown = true},
    {.key = "frontier",
     .weight = &EvaluationFeatureWeights::frontier,
     .raw_value = &EvaluationBreakdown::frontier,
     .breakdown_weight = &EvaluationBreakdown::frontier_weight,
     .weighted_score = &EvaluationBreakdown::frontier_score,
     .computation = EvaluationFeatureComputation::Frontier,
     .compute_when_unweighted_breakdown = true},
    {.key = "corner_local_2x3",
     .weight = &EvaluationFeatureWeights::corner_local_2x3,
     .raw_value = &EvaluationBreakdown::corner_local_2x3,
     .breakdown_weight = &EvaluationBreakdown::corner_local_2x3_weight,
     .weighted_score = &EvaluationBreakdown::corner_local_2x3_score,
     .computation = EvaluationFeatureComputation::CornerLocal2x3},
    {.key = "corner_2x3_pattern",
     .weight = &EvaluationFeatureWeights::corner_2x3_pattern,
     .raw_value = &EvaluationBreakdown::corner_2x3_pattern,
     .breakdown_weight = &EvaluationBreakdown::corner_2x3_pattern_weight,
     .weighted_score = &EvaluationBreakdown::corner_2x3_pattern_score,
     .computation = EvaluationFeatureComputation::Corner2x3Pattern},
    {.key = "edge_stability_lite",
     .weight = &EvaluationFeatureWeights::edge_stability_lite,
     .raw_value = &EvaluationBreakdown::edge_stability_lite,
     .breakdown_weight = &EvaluationBreakdown::edge_stability_lite_weight,
     .weighted_score = &EvaluationBreakdown::edge_stability_lite_score,
     .computation = EvaluationFeatureComputation::EdgeStabilityLite},
    {.key = "edge_8_pattern",
     .weight = &EvaluationFeatureWeights::edge_8_pattern,
     .raw_value = &EvaluationBreakdown::edge_8_pattern,
     .breakdown_weight = &EvaluationBreakdown::edge_8_pattern_weight,
     .weighted_score = &EvaluationBreakdown::edge_8_pattern_score,
     .computation = EvaluationFeatureComputation::Edge8Pattern},
    {.key = "pattern_table",
     .weight = &EvaluationFeatureWeights::pattern_table,
     .raw_value = &EvaluationBreakdown::pattern_table,
     .breakdown_weight = &EvaluationBreakdown::pattern_table_weight,
     .weighted_score = &EvaluationBreakdown::pattern_table_score,
     .computation = EvaluationFeatureComputation::PatternTable,
     .required_in_full_config = false},
}};

inline constexpr std::size_t evaluation_feature_count =
    evaluation_feature_specs.size();

} // namespace othello::evaluation_detail
