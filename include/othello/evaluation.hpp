#pragma once

#include <othello/evaluation_patterns.hpp>
#include <othello/board.hpp>
#include <othello/types.hpp>

#include <memory>

namespace othello {

enum class EvaluationPhase {
    Opening,
    Midgame,
    Late,
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
    std::shared_ptr<const PatternTableBundle> pattern_tables{};
    std::shared_ptr<const PatternTableBundle> opening_pattern_tables{};
    std::shared_ptr<const PatternTableBundle> midgame_pattern_tables{};
    std::shared_ptr<const PatternTableBundle> late_pattern_tables{};

    [[nodiscard]] friend bool operator==(const EvaluationConfig& lhs,
                                         const EvaluationConfig& rhs) {
        if (!(lhs.opening == rhs.opening && lhs.midgame == rhs.midgame &&
              lhs.late == rhs.late &&
              lhs.opening_max_occupied == rhs.opening_max_occupied &&
              lhs.midgame_max_occupied == rhs.midgame_max_occupied)) {
            return false;
        }
        const auto same_pattern_table_bundle =
            [](const std::shared_ptr<const PatternTableBundle>& lhs_tables,
               const std::shared_ptr<const PatternTableBundle>& rhs_tables) {
                if (lhs_tables == rhs_tables) {
                    return true;
                }
                if (lhs_tables == nullptr || rhs_tables == nullptr) {
                    return false;
                }
                return *lhs_tables == *rhs_tables;
            };
        return same_pattern_table_bundle(lhs.pattern_tables, rhs.pattern_tables) &&
               same_pattern_table_bundle(lhs.opening_pattern_tables,
                                         rhs.opening_pattern_tables) &&
               same_pattern_table_bundle(lhs.midgame_pattern_tables,
                                         rhs.midgame_pattern_tables) &&
               same_pattern_table_bundle(lhs.late_pattern_tables,
                                         rhs.late_pattern_tables);
    }
};

[[nodiscard]] inline EvaluationConfig default_evaluation_config() noexcept {
    return EvaluationConfig{};
}

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
[[nodiscard]] EvaluationBreakdown evaluate_basic_breakdown(const Board& board,
                                                           Side side) noexcept;
[[nodiscard]] EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                                           const EvaluationConfig& config) noexcept;
[[nodiscard]] int evaluate_with_config(const Board& board, Side side,
                                       const EvaluationConfig& config) noexcept;
[[nodiscard]] int evaluate_basic(const Board& board, Side side) noexcept;

} // namespace othello
