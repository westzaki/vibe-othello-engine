#include "common/eval_config_io.hpp"
#include "common/pattern_table_io.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace othello::tools {
namespace {

struct FeatureKeySpec {
    std::string_view key;
    EvaluationFeatureWeights EvaluationConfig::*phase;
    int EvaluationFeatureWeights::*weight;
    bool required = true;
};

struct FeatureKeyAliasSpec {
    std::string_view key;
    std::string_view canonical_key;
};

struct ConfigKeySpec {
    std::string_view key;
    int EvaluationConfig::*value;
};

struct PatternTablePathKeySpec {
    std::string_view key;
    std::optional<std::string> EvaluationConfigLoadResult::*path;
};

enum class EvaluationConfigMode {
    Full,
    PatternOnly,
};

inline constexpr std::string_view eval_schema_version = "eval.v1";

constexpr std::array<FeatureKeySpec, 36> feature_keys{{
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

constexpr std::array<ConfigKeySpec, 2> config_keys{{
    {"opening_max_occupied", &EvaluationConfig::opening_max_occupied},
    {"midgame_max_occupied", &EvaluationConfig::midgame_max_occupied},
}};

constexpr std::array<PatternTablePathKeySpec, 3> phase_pattern_table_path_keys{{
    {"pattern_table.opening", &EvaluationConfigLoadResult::opening_pattern_table_path},
    {"pattern_table.midgame", &EvaluationConfigLoadResult::midgame_pattern_table_path},
    {"pattern_table.late", &EvaluationConfigLoadResult::late_pattern_table_path},
}};

[[nodiscard]] constexpr bool is_ascii_space(char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

[[nodiscard]] std::string_view trim_ascii(std::string_view text) noexcept {
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::string line_error(int line_number, std::string_view message) {
    std::ostringstream out;
    out << "line " << line_number << ": " << message;
    return out.str();
}

[[nodiscard]] std::optional<int> parse_eval_int(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool seen_key(std::string_view key, const std::vector<std::string>& keys) {
    return std::ranges::any_of(keys, [key](const std::string& seen) {
        return seen == key;
    });
}

[[nodiscard]] std::optional<std::size_t>
canonical_feature_key_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < feature_keys.size(); ++index) {
        if (feature_keys[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> feature_key_index(std::string_view key) noexcept {
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

[[nodiscard]] std::optional<std::size_t> config_key_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < config_keys.size(); ++index) {
        if (config_keys[index].key == key) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t>
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

void assign_missing_feature_keys(EvaluationConfig& config,
                                 const std::array<bool, feature_keys.size()>& seen,
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

} // namespace

EvaluationConfigLoadResult parse_evaluation_config(std::string_view text) {
    EvaluationConfigLoadResult result;
    std::array<bool, feature_keys.size()> seen_features{};
    std::array<bool, config_keys.size()> seen_configs{};
    std::vector<std::string> seen_keys;
    EvaluationConfigMode mode = EvaluationConfigMode::Full;

    int line_number = 0;
    std::size_t line_begin = 0;
    while (line_begin <= text.size()) {
        ++line_number;
        const std::size_t newline = text.find('\n', line_begin);
        const std::size_t line_end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view line = text.substr(line_begin, line_end - line_begin);
        if (const std::size_t comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim_ascii(line);

        if (!line.empty()) {
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos) {
                result.error = line_error(line_number, "expected key=value");
                return result;
            }

            const std::string_view key = trim_ascii(line.substr(0, equals));
            const std::string_view value = trim_ascii(line.substr(equals + 1));
            if (key.empty()) {
                result.error = line_error(line_number, "empty key");
                return result;
            }
            if (seen_key(key, seen_keys)) {
                result.error = line_error(line_number, "duplicate key: " + std::string{key});
                return result;
            }
            seen_keys.emplace_back(key);

            if (key == "name") {
                result.name = std::string{value};
            } else if (key == "schema_version") {
                if (value != eval_schema_version) {
                    result.error = line_error(line_number, "unsupported schema_version: " +
                                                              std::string{value});
                    return result;
                }
            } else if (key == "mode") {
                if (value == "full") {
                    mode = EvaluationConfigMode::Full;
                } else if (value == "pattern_only") {
                    mode = EvaluationConfigMode::PatternOnly;
                } else {
                    result.error = line_error(line_number, "unknown mode: " +
                                                              std::string{value});
                    return result;
                }
            } else if (key == "pattern_table") {
                if (value.empty()) {
                    result.error = line_error(line_number, "empty pattern_table path");
                    return result;
                }
                result.pattern_table_path = std::string{value};
            } else if (const std::optional<std::size_t> index =
                           phase_pattern_table_path_key_index(key);
                       index.has_value()) {
                if (value.empty()) {
                    result.error = line_error(line_number, "empty " + std::string{key} + " path");
                    return result;
                }
                result.*(phase_pattern_table_path_keys[*index].path) = std::string{value};
            } else if (const std::optional<std::size_t> index = feature_key_index(key);
                       index.has_value()) {
                const std::optional<int> parsed = parse_eval_int(value);
                if (!parsed.has_value()) {
                    result.error = line_error(line_number, "invalid integer for key: " +
                                                              std::string{key});
                    return result;
                }
                if (seen_features[*index]) {
                    result.error = line_error(
                        line_number,
                        "duplicate feature key: " + std::string{feature_keys[*index].key});
                    return result;
                }
                assign_feature_key(result.config, *index, *parsed);
                seen_features[*index] = true;
            } else if (const std::optional<std::size_t> index = config_key_index(key);
                       index.has_value()) {
                const std::optional<int> parsed = parse_eval_int(value);
                if (!parsed.has_value()) {
                    result.error = line_error(line_number, "invalid integer for key: " +
                                                              std::string{key});
                    return result;
                }
                assign_config_key(result.config, *index, *parsed);
                seen_configs[*index] = true;
            } else {
                result.error = line_error(line_number, "unknown key: " + std::string{key});
                return result;
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        line_begin = newline + 1;
    }

    if (mode == EvaluationConfigMode::PatternOnly) {
        assign_missing_feature_keys(result.config, seen_features, 0);
    } else {
        for (std::size_t index = 0; index < feature_keys.size(); ++index) {
            if (feature_keys[index].required && !seen_features[index]) {
                result.error = "missing required key: " + std::string{feature_keys[index].key};
                return result;
            }
        }
    }
    for (std::size_t index = 0; index < config_keys.size(); ++index) {
        if (mode == EvaluationConfigMode::Full && !seen_configs[index]) {
            result.error = "missing required key: " + std::string{config_keys[index].key};
            return result;
        }
    }

    return result;
}

EvaluationConfigLoadResult load_evaluation_config_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        EvaluationConfigLoadResult result;
        result.error = "failed to open evaluation config: " + path.string();
        return result;
    }

    const std::string text{std::istreambuf_iterator<char>{input},
                           std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        EvaluationConfigLoadResult result;
        result.error = "failed to read evaluation config: " + path.string();
        return result;
    }

    EvaluationConfigLoadResult result = parse_evaluation_config(text);
    if (!result.ok()) {
        result.error = path.string() + ": " + result.error;
        return result;
    }

    PatternTableCache table_cache;
    const auto load_optional_pattern_table =
        [&](const std::optional<std::string>& table_path,
            std::shared_ptr<const PatternTableBundle> EvaluationConfig::* target) -> bool {
        if (!table_path.has_value()) {
            return true;
        }
        const std::filesystem::path parsed_table_path{*table_path};
        const std::filesystem::path resolved_table_path =
            parsed_table_path.is_absolute() ? parsed_table_path
                                            : path.parent_path() / parsed_table_path;
        const PatternTableLoadResult table_result = table_cache.load(resolved_table_path);
        if (!table_result.ok()) {
            result.error = resolved_table_path.string() + ": " + table_result.error;
            return false;
        }
        result.config.*target = table_result.tables;
        return true;
    };

    if (!load_optional_pattern_table(result.pattern_table_path,
                                     &EvaluationConfig::pattern_tables) ||
        !load_optional_pattern_table(result.opening_pattern_table_path,
                                     &EvaluationConfig::opening_pattern_tables) ||
        !load_optional_pattern_table(result.midgame_pattern_table_path,
                                     &EvaluationConfig::midgame_pattern_tables) ||
        !load_optional_pattern_table(result.late_pattern_table_path,
                                     &EvaluationConfig::late_pattern_tables)) {
        return result;
    }
    return result;
}

} // namespace othello::tools
