#include "common/eval_config_schema.hpp"

#include "common/eval_config_io.hpp"

#include <array>

namespace othello::tools::eval_config_schema {
namespace {

struct FeatureKeyAliasSpec {
    std::string_view key;
    std::string_view canonical_key;
};

constexpr std::array<FeatureKeySpec, feature_key_count> feature_keys{{
    {"opening.disc_difference", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::disc_difference},
    {"opening.mobility", &EvaluationConfig::opening, &EvaluationFeatureWeights::mobility},
    {"opening.potential_mobility", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::potential_mobility},
    {"opening.corner_occupancy", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::corner_occupancy},
    {"opening.corner_access", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::corner_access},
    {"opening.x_square_danger", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::x_square_danger},
    {"opening.frontier", &EvaluationConfig::opening, &EvaluationFeatureWeights::frontier},
    {"opening.corner_local_2x3", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::corner_local_2x3},
    {"opening.corner_2x3_pattern", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::corner_2x3_pattern},
    {"opening.edge_stability_lite", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::edge_stability_lite},
    {"opening.edge_8_pattern", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::edge_8_pattern},
    {"opening.pattern_table", &EvaluationConfig::opening,
     &EvaluationFeatureWeights::pattern_table, false},
    {"midgame.disc_difference", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::disc_difference},
    {"midgame.mobility", &EvaluationConfig::midgame, &EvaluationFeatureWeights::mobility},
    {"midgame.potential_mobility", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::potential_mobility},
    {"midgame.corner_occupancy", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::corner_occupancy},
    {"midgame.corner_access", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::corner_access},
    {"midgame.x_square_danger", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::x_square_danger},
    {"midgame.frontier", &EvaluationConfig::midgame, &EvaluationFeatureWeights::frontier},
    {"midgame.corner_local_2x3", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::corner_local_2x3},
    {"midgame.corner_2x3_pattern", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::corner_2x3_pattern},
    {"midgame.edge_stability_lite", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::edge_stability_lite},
    {"midgame.edge_8_pattern", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::edge_8_pattern},
    {"midgame.pattern_table", &EvaluationConfig::midgame,
     &EvaluationFeatureWeights::pattern_table, false},
    {"late.disc_difference", &EvaluationConfig::late,
     &EvaluationFeatureWeights::disc_difference},
    {"late.mobility", &EvaluationConfig::late, &EvaluationFeatureWeights::mobility},
    {"late.potential_mobility", &EvaluationConfig::late,
     &EvaluationFeatureWeights::potential_mobility},
    {"late.corner_occupancy", &EvaluationConfig::late,
     &EvaluationFeatureWeights::corner_occupancy},
    {"late.corner_access", &EvaluationConfig::late, &EvaluationFeatureWeights::corner_access},
    {"late.x_square_danger", &EvaluationConfig::late,
     &EvaluationFeatureWeights::x_square_danger},
    {"late.frontier", &EvaluationConfig::late, &EvaluationFeatureWeights::frontier},
    {"late.corner_local_2x3", &EvaluationConfig::late,
     &EvaluationFeatureWeights::corner_local_2x3},
    {"late.corner_2x3_pattern", &EvaluationConfig::late,
     &EvaluationFeatureWeights::corner_2x3_pattern},
    {"late.edge_stability_lite", &EvaluationConfig::late,
     &EvaluationFeatureWeights::edge_stability_lite},
    {"late.edge_8_pattern", &EvaluationConfig::late,
     &EvaluationFeatureWeights::edge_8_pattern},
    {"late.pattern_table", &EvaluationConfig::late,
     &EvaluationFeatureWeights::pattern_table, false},
}};

constexpr std::array<FeatureKeyAliasSpec, 6> feature_key_aliases{{
    {"opening.legacy_corner_2x3_rule", "opening.corner_2x3_pattern"},
    {"opening.legacy_edge_8_rule", "opening.edge_8_pattern"},
    {"midgame.legacy_corner_2x3_rule", "midgame.corner_2x3_pattern"},
    {"midgame.legacy_edge_8_rule", "midgame.edge_8_pattern"},
    {"late.legacy_corner_2x3_rule", "late.corner_2x3_pattern"},
    {"late.legacy_edge_8_rule", "late.edge_8_pattern"},
}};

constexpr std::array<ConfigKeySpec, config_key_count> config_keys{{
    {"opening_max_occupied", &EvaluationConfig::opening_max_occupied},
    {"midgame_max_occupied", &EvaluationConfig::midgame_max_occupied},
}};

constexpr std::array<PatternTablePathKeySpec, 3> phase_pattern_table_path_keys{{
    {"pattern_table.opening", &EvaluationConfigLoadResult::opening_pattern_table_path},
    {"pattern_table.midgame", &EvaluationConfigLoadResult::midgame_pattern_table_path},
    {"pattern_table.late", &EvaluationConfigLoadResult::late_pattern_table_path},
}};

[[nodiscard]] std::optional<std::size_t>
canonical_feature_key_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < feature_keys.size(); ++index) {
        if (feature_keys[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace

const FeatureKeySpec& feature_key(std::size_t index) noexcept {
    return feature_keys[index];
}

const ConfigKeySpec& config_key(std::size_t index) noexcept {
    return config_keys[index];
}

const PatternTablePathKeySpec& phase_pattern_table_path_key(std::size_t index) noexcept {
    return phase_pattern_table_path_keys[index];
}

std::optional<std::size_t> feature_key_index(std::string_view key) noexcept {
    if (const std::optional<std::size_t> index = canonical_feature_key_index(key);
        index.has_value()) {
        return index;
    }
    for (const FeatureKeyAliasSpec& alias : feature_key_aliases) {
        if (alias.key == key) {
            return canonical_feature_key_index(alias.canonical_key);
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> config_key_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < config_keys.size(); ++index) {
        if (config_keys[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t>
phase_pattern_table_path_key_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < phase_pattern_table_path_keys.size(); ++index) {
        if (phase_pattern_table_path_keys[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

void assign_feature_key(EvaluationConfig& config, std::size_t index, int value) noexcept {
    const FeatureKeySpec& spec = feature_keys[index];
    (config.*spec.phase).*spec.weight = value;
}

void assign_missing_feature_keys(
    EvaluationConfig& config,
    const std::array<bool, feature_key_count>& seen,
    int value) noexcept {
    for (std::size_t index = 0; index < feature_keys.size(); ++index) {
        if (!seen[index]) {
            assign_feature_key(config, index, value);
        }
    }
}

void assign_config_key(EvaluationConfig& config, std::size_t index, int value) noexcept {
    const ConfigKeySpec& spec = config_keys[index];
    config.*spec.value = value;
}

} // namespace othello::tools::eval_config_schema
