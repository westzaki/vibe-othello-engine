#pragma once

#include <filesystem>
#include <optional>
#include <othello/evaluation.hpp>
#include <string>
#include <string_view>

namespace othello::tools {

struct EvaluationConfigLoadResult {
    EvaluationConfig config{};
    std::optional<std::string> name;
    std::optional<std::string> pattern_table_path;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

[[nodiscard]] EvaluationConfigLoadResult parse_evaluation_config(std::string_view text);
[[nodiscard]] EvaluationConfigLoadResult
load_evaluation_config_file(const std::filesystem::path& path);

} // namespace othello::tools
