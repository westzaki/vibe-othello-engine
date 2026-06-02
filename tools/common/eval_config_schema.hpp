#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <othello/evaluation.hpp>
#include <string>
#include <string_view>

namespace othello::tools {

struct EvaluationConfigLoadResult;

namespace eval_config_schema {

enum class Mode {
    Full,
    PatternOnly,
};

struct FeatureKeySpec {
    std::string_view key;
    EvaluationFeatureWeights EvaluationConfig::*phase;
    int EvaluationFeatureWeights::*weight;
    bool required = true;
};

struct ConfigKeySpec {
    std::string_view key;
    int EvaluationConfig::*value;
};

struct PatternTablePathKeySpec {
    std::string_view key;
    std::optional<std::string> EvaluationConfigLoadResult::*path;
};

inline constexpr std::string_view schema_version = "eval.v1";
inline constexpr std::size_t feature_key_count = 36;
inline constexpr std::size_t config_key_count = 2;

[[nodiscard]] const FeatureKeySpec& feature_key(std::size_t index) noexcept;
[[nodiscard]] const ConfigKeySpec& config_key(std::size_t index) noexcept;
[[nodiscard]] const PatternTablePathKeySpec&
phase_pattern_table_path_key(std::size_t index) noexcept;

[[nodiscard]] std::optional<std::size_t> feature_key_index(std::string_view key) noexcept;
[[nodiscard]] std::optional<std::size_t> config_key_index(std::string_view key) noexcept;
[[nodiscard]] std::optional<std::size_t>
phase_pattern_table_path_key_index(std::string_view key) noexcept;

void assign_feature_key(EvaluationConfig& config, std::size_t index, int value) noexcept;
void assign_missing_feature_keys(
    EvaluationConfig& config,
    const std::array<bool, feature_key_count>& seen,
    int value) noexcept;
void assign_config_key(EvaluationConfig& config, std::size_t index, int value) noexcept;

} // namespace eval_config_schema
} // namespace othello::tools
