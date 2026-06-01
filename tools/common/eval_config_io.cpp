#include "common/eval_config_io.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace othello::tools {
namespace {

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

struct PatternTableLoadResult {
    std::shared_ptr<const PatternTableBundle> tables;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

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

constexpr std::array<ConfigKeySpec, 2> config_keys{{
    {"opening_max_occupied", &EvaluationConfig::opening_max_occupied},
    {"midgame_max_occupied", &EvaluationConfig::midgame_max_occupied},
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

[[nodiscard]] PatternTableLoadResult pattern_table_error(std::string error) {
    PatternTableLoadResult result;
    result.error = std::move(error);
    return result;
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

template <std::size_t N>
[[nodiscard]] std::string assign_sparse_pattern_entry(std::array<std::int16_t, N>& table,
                                                      std::array<bool, N>& seen,
                                                      int line_number,
                                                      std::string_view family, int index,
                                                      int value) {
    if (index < 0 || index >= static_cast<int>(N)) {
        return line_error(line_number, std::string{family} + " index out of range");
    }
    const auto table_index = static_cast<std::size_t>(index);
    if (seen[table_index]) {
        return line_error(line_number, "duplicate " + std::string{family} + " index");
    }
    table[table_index] = static_cast<std::int16_t>(value);
    seen[table_index] = true;
    return {};
}

[[nodiscard]] std::optional<std::size_t> feature_key_index(std::string_view key) noexcept {
    for (std::size_t index = 0; index < feature_keys.size(); ++index) {
        if (feature_keys[index].key == key) {
            return index;
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

void assign_feature_key(EvaluationConfig& config, std::size_t index, int value) noexcept {
    const FeatureKeySpec& spec = feature_keys[index];
    (config.*spec.phase).*spec.weight = value;
}

void assign_config_key(EvaluationConfig& config, std::size_t index, int value) noexcept {
    const ConfigKeySpec& spec = config_keys[index];
    config.*spec.value = value;
}

[[nodiscard]] PatternTableLoadResult
load_pattern_table_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        return pattern_table_error("failed to open pattern table: " + path.string());
    }

    auto loaded = std::make_shared<PatternTableBundle>();
    std::array<bool, corner_2x3_pattern_table_size> seen_corner{};
    std::array<bool, corner_3x3_pattern_table_size> seen_corner_3x3{};
    std::array<bool, edge_8_pattern_table_size> seen_edge{};
    std::array<bool, edge_x_10_pattern_table_size> seen_edge_x_10{};
    std::array<bool, diagonal_8_pattern_table_size> seen_diagonal{};
    std::array<bool, inner_row_8_pattern_table_size> seen_inner_row{};
    bool has_entries = false;

    int line_number = 0;
    std::string raw_line;
    while (std::getline(input, raw_line)) {
        ++line_number;
        std::string_view line{raw_line};
        if (const std::size_t comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim_ascii(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream parts{std::string{line}};
        std::string family;
        int index = 0;
        int value = 0;
        std::string trailing;
        if (!(parts >> family >> index >> value) || (parts >> trailing)) {
            return pattern_table_error(
                line_error(line_number, "expected '<family> <index> <value>'"));
        }
        if (value < std::numeric_limits<std::int16_t>::min() ||
            value > std::numeric_limits<std::int16_t>::max()) {
            return pattern_table_error(
                line_error(line_number, "pattern value outside int16 range"));
        }

        if (family == "corner_2x3") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->corner_2x3, seen_corner, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "corner_3x3") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->corner_3x3, seen_corner_3x3, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "edge_8") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->edge_8, seen_edge, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "edge_x_10") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->edge_x_10, seen_edge_x_10, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "diagonal_8") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->diagonal_8, seen_diagonal, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else if (family == "inner_row_8") {
            if (const std::string error = assign_sparse_pattern_entry(
                    loaded->inner_row_8, seen_inner_row, line_number, family, index, value);
                !error.empty()) {
                return pattern_table_error(error);
            }
        } else {
            return pattern_table_error(
                line_error(line_number, "unknown pattern family: " + family));
        }
        has_entries = true;
    }

    if (input.bad()) {
        return pattern_table_error("failed to read pattern table: " + path.string());
    }
    if (!has_entries) {
        return pattern_table_error("pattern table has no entries");
    }

    PatternTableLoadResult result;
    result.tables = std::move(loaded);
    return result;
}

} // namespace

EvaluationConfigLoadResult parse_evaluation_config(std::string_view text) {
    EvaluationConfigLoadResult result;
    std::array<bool, feature_keys.size()> seen_features{};
    std::array<bool, config_keys.size()> seen_configs{};
    std::vector<std::string> seen_keys;

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
            } else if (key == "pattern_table") {
                if (value.empty()) {
                    result.error = line_error(line_number, "empty pattern_table path");
                    return result;
                }
                result.pattern_table_path = std::string{value};
            } else if (const std::optional<std::size_t> index = feature_key_index(key);
                       index.has_value()) {
                const std::optional<int> parsed = parse_eval_int(value);
                if (!parsed.has_value()) {
                    result.error = line_error(line_number, "invalid integer for key: " +
                                                              std::string{key});
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

    for (std::size_t index = 0; index < feature_keys.size(); ++index) {
        if (feature_keys[index].required && !seen_features[index]) {
            result.error = "missing required key: " + std::string{feature_keys[index].key};
            return result;
        }
    }
    for (std::size_t index = 0; index < config_keys.size(); ++index) {
        if (!seen_configs[index]) {
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

    if (result.pattern_table_path.has_value()) {
        const std::filesystem::path table_path{*result.pattern_table_path};
        const std::filesystem::path resolved_table_path =
            table_path.is_absolute() ? table_path : path.parent_path() / table_path;
        const PatternTableLoadResult table_result = load_pattern_table_file(resolved_table_path);
        if (!table_result.ok()) {
            result.error = resolved_table_path.string() + ": " + table_result.error;
        } else {
            result.config.pattern_tables = table_result.tables;
        }
    }
    return result;
}

} // namespace othello::tools
