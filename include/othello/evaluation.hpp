#pragma once

#include <othello/board.hpp>
#include <othello/types.hpp>

#include <optional>
#include <string_view>

namespace othello {

enum class EvaluationPhase {
    Opening,
    Midgame,
    Late,
};

enum class EvaluationPreset {
    Default,
    MobilityPlusSmoke,
    FrontierOpen2Mid2LatePlus1,
    ClassicCornerLiteV1,
    ClassicEdgeLiteV1,
    ClassicFeaturesLiteV1,
    ClassicFeaturesLiteAggressive,
    FrontierClassicFeaturesLiteV1,
};

struct EvaluationFeatureWeights {
    int disc_difference = 0;
    int mobility = 0;
    int potential_mobility = 0;
    int corner_occupancy = 0;
    int corner_access = 0;
    int x_square_danger = 0;
    int frontier = 0;
    int corner_local_2x3 = 0;
    int edge_stability_lite = 0;

    [[nodiscard]] friend bool operator==(const EvaluationFeatureWeights&,
                                         const EvaluationFeatureWeights&) = default;
};

struct EvaluationConfig {
    EvaluationFeatureWeights opening{
        .disc_difference = 0,
        .mobility = 8,
        .potential_mobility = 4,
        .corner_occupancy = 35,
        .corner_access = 30,
        .x_square_danger = 25,
        .frontier = 3,
    };
    EvaluationFeatureWeights midgame{
        .disc_difference = 1,
        .mobility = 10,
        .potential_mobility = 5,
        .corner_occupancy = 40,
        .corner_access = 35,
        .x_square_danger = 30,
        .frontier = 4,
    };
    EvaluationFeatureWeights late{
        .disc_difference = 4,
        .mobility = 6,
        .potential_mobility = 2,
        .corner_occupancy = 45,
        .corner_access = 20,
        .x_square_danger = 20,
        .frontier = 2,
    };
    int opening_max_occupied = 20;
    int midgame_max_occupied = 44;

    [[nodiscard]] friend bool operator==(const EvaluationConfig&,
                                         const EvaluationConfig&) = default;
};

[[nodiscard]] constexpr EvaluationConfig default_evaluation_config() noexcept {
    return EvaluationConfig{};
}

[[nodiscard]] EvaluationConfig evaluation_config_for_preset(EvaluationPreset preset) noexcept;
[[nodiscard]] std::optional<EvaluationPreset>
evaluation_preset_from_name(std::string_view name) noexcept;
[[nodiscard]] std::string_view evaluation_preset_name(EvaluationPreset preset) noexcept;

// Component view of the current basic evaluator. This is intended for developer
// tooling and tests; fields may evolve as the evaluator itself evolves. The
// total field matches the evaluator call that produced the breakdown. For
// terminal boards, the current evaluator uses only the terminal fields and
// leaves non-terminal component scores at zero.
struct EvaluationBreakdown {
    EvaluationPhase phase = EvaluationPhase::Opening;
    int occupied_count = 0;
    int empty_count = 64;

    int disc_difference = 0;
    int disc_difference_weight = 0;
    int disc_difference_score = 0;

    int mobility = 0;
    int mobility_weight = 0;
    int mobility_score = 0;

    int corner_occupancy = 0;
    int corner_occupancy_weight = 0;
    int corner_occupancy_score = 0;

    int potential_mobility = 0;
    int potential_mobility_weight = 0;
    int potential_mobility_score = 0;

    int corner_access = 0;
    int corner_access_weight = 0;
    int corner_access_score = 0;

    int x_square_danger = 0;
    int x_square_danger_weight = 0;
    int x_square_danger_score = 0;

    int frontier = 0;
    int frontier_weight = 0;
    int frontier_score = 0;

    int corner_local_2x3 = 0;
    int corner_local_2x3_weight = 0;
    int corner_local_2x3_score = 0;

    int edge_stability_lite = 0;
    int edge_stability_lite_weight = 0;
    int edge_stability_lite_score = 0;

    bool terminal = false;
    int terminal_disc_difference = 0;
    int terminal_score_weight = 1000;
    int terminal_score = 0;

    int total = 0;
};

[[nodiscard]] int evaluate_disc_difference(const Board& board, Side side) noexcept;
[[nodiscard]] int evaluate_mobility(const Board& board, Side side) noexcept;
[[nodiscard]] EvaluationBreakdown evaluate_basic_breakdown(const Board& board,
                                                           Side side) noexcept;
[[nodiscard]] EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                                           const EvaluationConfig& config) noexcept;
[[nodiscard]] int evaluate_with_config(const Board& board, Side side,
                                       const EvaluationConfig& config) noexcept;
[[nodiscard]] int evaluate_basic(const Board& board, Side side) noexcept;

} // namespace othello
