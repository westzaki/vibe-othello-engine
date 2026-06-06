#include "common/eval_config_schema.hpp"

#include "common/eval_config_io.hpp"

#include <array>
#include <string>

namespace othello::tools::eval_config_schema {
namespace {

struct PhaseKeySpec {
    std::string_view key;
    EvaluationFeatureWeights EvaluationConfig::*weights;
};

struct FeatureKeyAliasSpec {
    std::string_view key;
    std::string_view canonical_key;
};

constexpr std::array<PhaseKeySpec, phase_key_count> phase_keys{{
    {"opening", &EvaluationConfig::opening},
    {"midgame", &EvaluationConfig::midgame},
    {"late", &EvaluationConfig::late},
}};

constexpr std::array<FeatureKeyAliasSpec, 2> feature_key_aliases{{
    {"legacy_corner_2x3_rule", "corner_2x3_pattern"},
    {"legacy_edge_8_rule", "edge_8_pattern"},
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

[[nodiscard]] std::string make_feature_key(std::size_t phase_index,
                                           std::size_t feature_index) {
    std::string key{phase_keys[phase_index].key};
    key += '.';
    key += evaluation_detail::evaluation_feature_specs[feature_index].key;
    return key;
}

[[nodiscard]] constexpr std::size_t feature_key_index_for(
    std::size_t phase_index, std::size_t feature_index) noexcept {
    return phase_index * evaluation_detail::evaluation_feature_count + feature_index;
}

[[nodiscard]] constexpr std::size_t phase_index_for_feature_key(
    std::size_t index) noexcept {
    return index / evaluation_detail::evaluation_feature_count;
}

[[nodiscard]] constexpr std::size_t feature_index_for_feature_key(
    std::size_t index) noexcept {
    return index % evaluation_detail::evaluation_feature_count;
}

[[nodiscard]] std::optional<std::size_t>
phase_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < phase_keys.size(); ++index) {
        if (phase_keys[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
feature_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < evaluation_detail::evaluation_feature_count;
         ++index) {
        if (evaluation_detail::evaluation_feature_specs[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
feature_key_index_for_parts(std::string_view phase_key,
                            std::string_view feature_key) noexcept {
    const std::optional<std::size_t> parsed_phase_index = phase_index(phase_key);
    const std::optional<std::size_t> parsed_feature_index = feature_index(feature_key);
    if (!parsed_phase_index.has_value() || !parsed_feature_index.has_value()) {
        return std::nullopt;
    }
    return feature_key_index_for(*parsed_phase_index, *parsed_feature_index);
}

[[nodiscard]] std::optional<std::size_t>
canonical_feature_key_index(std::string_view key) noexcept {
    const std::size_t separator = key.find('.');
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    return feature_key_index_for_parts(key.substr(0, separator),
                                       key.substr(separator + 1));
}

} // namespace

FeatureKeySpec feature_key(std::size_t index) {
    const std::size_t phase_index = phase_index_for_feature_key(index);
    const std::size_t parsed_feature_index = feature_index_for_feature_key(index);
    const evaluation_detail::EvaluationFeatureSpec& feature =
        evaluation_detail::evaluation_feature_specs[parsed_feature_index];
    return {
        .key = make_feature_key(phase_index, parsed_feature_index),
        .phase = phase_keys[phase_index].weights,
        .weight = feature.weight,
        .required = feature.required_in_full_config,
    };
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

    const std::size_t separator = key.find('.');
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view phase_key = key.substr(0, separator);
    const std::string_view feature_key = key.substr(separator + 1);
    for (const FeatureKeyAliasSpec& alias : feature_key_aliases) {
        if (alias.key == feature_key) {
            return feature_key_index_for_parts(phase_key, alias.canonical_key);
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
    const std::size_t parsed_phase_index = phase_index_for_feature_key(index);
    const std::size_t parsed_feature_index = feature_index_for_feature_key(index);
    const evaluation_detail::EvaluationFeatureSpec& feature =
        evaluation_detail::evaluation_feature_specs[parsed_feature_index];
    (config.*phase_keys[parsed_phase_index].weights).*feature.weight = value;
}

void assign_missing_feature_keys(
    EvaluationConfig& config,
    const std::array<bool, feature_key_count>& seen,
    int value) noexcept {
    for (std::size_t index = 0; index < feature_key_count; ++index) {
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
