#include "common/eval_config_io.hpp"
#include "common/eval_config_schema.hpp"
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

using eval_config_schema::Mode;

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

} // namespace

EvaluationConfigLoadResult parse_evaluation_config(std::string_view text) {
    EvaluationConfigLoadResult result;
    std::array<bool, eval_config_schema::feature_key_count> seen_features{};
    std::array<bool, eval_config_schema::config_key_count> seen_configs{};
    std::vector<std::string> seen_keys;
    Mode mode = Mode::Full;

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
                if (value != eval_config_schema::schema_version) {
                    result.error = line_error(line_number, "unsupported schema_version: " +
                                                              std::string{value});
                    return result;
                }
            } else if (key == "mode") {
                if (value == "full") {
                    mode = Mode::Full;
                } else if (value == "pattern_only") {
                    mode = Mode::PatternOnly;
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
                           eval_config_schema::phase_pattern_table_path_key_index(key);
                       index.has_value()) {
                if (value.empty()) {
                    result.error = line_error(line_number, "empty " + std::string{key} + " path");
                    return result;
                }
                result.*(eval_config_schema::phase_pattern_table_path_key(*index).path) =
                    std::string{value};
            } else if (const std::optional<std::size_t> index =
                           eval_config_schema::feature_key_index(key);
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
                        "duplicate feature key: " +
                            std::string{eval_config_schema::feature_key(*index).key});
                    return result;
                }
                eval_config_schema::assign_feature_key(result.config, *index, *parsed);
                seen_features[*index] = true;
            } else if (const std::optional<std::size_t> index =
                           eval_config_schema::config_key_index(key);
                       index.has_value()) {
                const std::optional<int> parsed = parse_eval_int(value);
                if (!parsed.has_value()) {
                    result.error = line_error(line_number, "invalid integer for key: " +
                                                              std::string{key});
                    return result;
                }
                eval_config_schema::assign_config_key(result.config, *index, *parsed);
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

    if (mode == Mode::PatternOnly) {
        eval_config_schema::assign_missing_feature_keys(result.config, seen_features, 0);
    } else {
        for (std::size_t index = 0; index < eval_config_schema::feature_key_count; ++index) {
            const eval_config_schema::FeatureKeySpec spec =
                eval_config_schema::feature_key(index);
            if (spec.required && !seen_features[index]) {
                result.error = "missing required key: " + std::string{spec.key};
                return result;
            }
        }
    }
    for (std::size_t index = 0; index < eval_config_schema::config_key_count; ++index) {
        if (mode == Mode::Full && !seen_configs[index]) {
            result.error =
                "missing required key: " + std::string{eval_config_schema::config_key(index).key};
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
