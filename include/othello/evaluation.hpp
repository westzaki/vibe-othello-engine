#pragma once

#include <othello/board.hpp>
#include <othello/types.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace othello {

enum class EvaluationPhase {
    Opening,
    Midgame,
    Late,
};

// Public preset names are compatibility surface. Prefer .eval config files for
// experimental evaluator candidates; add enum entries only for stable,
// long-lived built-in presets.
enum class EvaluationPreset {
    Default,
    PhaseAwareV1,
    MobilityPlusSmoke,
    FrontierOpen2Mid2LatePlus1,
    ClassicCornerLiteV1,
    ClassicEdgeLiteV1,
    ClassicFeaturesLiteV1,
    ClassicFeaturesLiteAggressive,
    FrontierClassicFeaturesLiteV1,
    CornerPattern2x3V1,
    CornerPattern2x3Aggressive,
    FrontierCornerPattern2x3V1,
    FrontierCornerPatternEdgeLiteV1,
    EdgePattern8V1,
    EdgePattern8Aggressive,
    DefaultEdgePattern8V1,
    DefaultEdgePattern8NoEdgeLite,
    DefaultEdgePattern8Aggressive,
};

enum class Corner2x3PatternCorner {
    A1,
    H1,
    A8,
    H8,
};

inline constexpr int corner_2x3_pattern_table_size = 729;

enum class Edge8PatternEdge {
    Top,
    Bottom,
    Left,
    Right,
};

inline constexpr int edge_8_pattern_table_size = 6561;

struct EvaluationPatternTables {
    bool enabled = false;
    std::array<std::int16_t, corner_2x3_pattern_table_size> corner_2x3{};
    std::array<std::int16_t, edge_8_pattern_table_size> edge_8{};

    [[nodiscard]] friend bool operator==(const EvaluationPatternTables&,
                                         const EvaluationPatternTables&) = default;
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
    int corner_2x3_pattern = 0;
    int edge_stability_lite = 0;
    int edge_8_pattern = 0;
    int pattern_table = 0;

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
        .frontier = 5,
        .corner_2x3_pattern = 4,
        .edge_stability_lite = 2,
        .edge_8_pattern = 2,
    };
    EvaluationFeatureWeights midgame{
        .disc_difference = 1,
        .mobility = 10,
        .potential_mobility = 5,
        .corner_occupancy = 40,
        .corner_access = 35,
        .x_square_danger = 30,
        .frontier = 6,
        .corner_2x3_pattern = 6,
        .edge_stability_lite = 4,
        .edge_8_pattern = 4,
    };
    EvaluationFeatureWeights late{
        .disc_difference = 4,
        .mobility = 6,
        .potential_mobility = 2,
        .corner_occupancy = 45,
        .corner_access = 20,
        .x_square_danger = 20,
        .frontier = 3,
        .corner_2x3_pattern = 4,
        .edge_stability_lite = 8,
        .edge_8_pattern = 6,
    };
    int opening_max_occupied = 20;
    int midgame_max_occupied = 44;
    EvaluationPatternTables pattern_tables{};

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

    int corner_2x3_pattern = 0;
    int corner_2x3_pattern_weight = 0;
    int corner_2x3_pattern_score = 0;

    int edge_stability_lite = 0;
    int edge_stability_lite_weight = 0;
    int edge_stability_lite_score = 0;

    int edge_8_pattern = 0;
    int edge_8_pattern_weight = 0;
    int edge_8_pattern_score = 0;

    int pattern_table = 0;
    int pattern_table_weight = 0;
    int pattern_table_score = 0;

    bool terminal = false;
    int terminal_disc_difference = 0;
    int terminal_score_weight = 1000;
    int terminal_score = 0;

    int total = 0;
};

[[nodiscard]] int evaluate_disc_difference(const Board& board, Side side) noexcept;
[[nodiscard]] int evaluate_mobility(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_2x3_pattern_index(const Board& board, Side side,
                                           Corner2x3PatternCorner corner) noexcept;
[[nodiscard]] int corner_2x3_pattern_table_value(int index) noexcept;
[[nodiscard]] int corner_2x3_pattern_value(const Board& board, Side side) noexcept;
[[nodiscard]] int corner_2x3_pattern_score(const Board& board, Side side) noexcept;
[[nodiscard]] int edge_8_pattern_index(const Board& board, Side side,
                                       Edge8PatternEdge edge) noexcept;
[[nodiscard]] int edge_8_pattern_table_value(int index) noexcept;
[[nodiscard]] int edge_8_pattern_value(const Board& board, Side side) noexcept;
[[nodiscard]] int edge_8_pattern_score(const Board& board, Side side) noexcept;
[[nodiscard]] int evaluation_pattern_table_value(
    const Board& board, Side side, const EvaluationPatternTables& tables) noexcept;
[[nodiscard]] int evaluation_pattern_table_score(
    const Board& board, Side side, const EvaluationPatternTables& tables) noexcept;
[[nodiscard]] EvaluationBreakdown evaluate_basic_breakdown(const Board& board,
                                                           Side side) noexcept;
[[nodiscard]] EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                                           const EvaluationConfig& config) noexcept;
[[nodiscard]] int evaluate_with_config(const Board& board, Side side,
                                       const EvaluationConfig& config) noexcept;
[[nodiscard]] int evaluate_basic(const Board& board, Side side) noexcept;

} // namespace othello
