#pragma once

#include <othello/evaluation.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace othello::evaluation_detail {

struct EvaluationFeatureSpec {
    std::string_view key;
    int EvaluationFeatureWeights::*weight;
    bool required_in_full_config = true;
};

inline constexpr std::array<EvaluationFeatureSpec, 12> evaluation_feature_specs{{
    {"disc_difference", &EvaluationFeatureWeights::disc_difference},
    {"mobility", &EvaluationFeatureWeights::mobility},
    {"potential_mobility", &EvaluationFeatureWeights::potential_mobility},
    {"corner_occupancy", &EvaluationFeatureWeights::corner_occupancy},
    {"corner_access", &EvaluationFeatureWeights::corner_access},
    {"x_square_danger", &EvaluationFeatureWeights::x_square_danger},
    {"frontier", &EvaluationFeatureWeights::frontier},
    {"corner_local_2x3", &EvaluationFeatureWeights::corner_local_2x3},
    {"corner_2x3_pattern", &EvaluationFeatureWeights::corner_2x3_pattern},
    {"edge_stability_lite", &EvaluationFeatureWeights::edge_stability_lite},
    {"edge_8_pattern", &EvaluationFeatureWeights::edge_8_pattern},
    {"pattern_table", &EvaluationFeatureWeights::pattern_table, false},
}};

inline constexpr std::size_t evaluation_feature_count =
    evaluation_feature_specs.size();

} // namespace othello::evaluation_detail
