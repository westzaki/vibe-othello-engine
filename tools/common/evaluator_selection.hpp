#pragma once

#include <filesystem>
#include <optional>
#include <othello/search.hpp>
#include <string>

namespace othello::tools {

enum class EvaluatorSelectionSource {
    BuiltInFallback,
    ProjectDefaultConfig,
    ExplicitConfig,
};

struct EvaluatorSelection {
    std::optional<EvaluationConfig> config_override = std::nullopt;
    std::optional<std::string> config_path = std::nullopt;
    EvaluatorSelectionSource source = EvaluatorSelectionSource::BuiltInFallback;

    [[nodiscard]] friend bool operator==(const EvaluatorSelection&,
                                         const EvaluatorSelection&) = default;
};

struct EvaluatorSelectionInput {
    std::optional<std::string> config_path = std::nullopt;
    std::optional<std::filesystem::path> project_default_config_path = std::nullopt;
};

[[nodiscard]] bool has_custom_eval_config(const EvaluatorSelection& selection) noexcept;
[[nodiscard]] bool uses_project_default_eval_config(
    const EvaluatorSelection& selection) noexcept;
[[nodiscard]] bool uses_built_in_fallback_eval_config(
    const EvaluatorSelection& selection) noexcept;
[[nodiscard]] EvaluationConfig
resolve_evaluator_selection(const EvaluatorSelection& selection) noexcept;
[[nodiscard]] SearchOptions apply_evaluator_selection(
    SearchOptions options, const EvaluatorSelection& selection) noexcept;
[[nodiscard]] std::optional<EvaluatorSelection>
parse_evaluator_selection(const EvaluatorSelectionInput& input, std::string& error_message);

} // namespace othello::tools
