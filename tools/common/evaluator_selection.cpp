#include "common/evaluator_selection.hpp"

#include "common/eval_config_io.hpp"

namespace othello::tools {
namespace {

constexpr auto project_default_eval_config_path = "data/eval/current_default.eval";
constexpr auto build_dir_project_default_eval_config_path = "../data/eval/current_default.eval";

[[nodiscard]] std::optional<std::filesystem::path>
find_project_default_eval_config_path() {
    const std::filesystem::path source_root_path{project_default_eval_config_path};
    if (std::filesystem::exists(source_root_path)) {
        return source_root_path;
    }

    const std::filesystem::path build_root_path{build_dir_project_default_eval_config_path};
    if (std::filesystem::exists(build_root_path)) {
        return build_root_path;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path>
resolve_project_default_eval_config_path(const EvaluatorSelectionInput& input) {
    if (input.project_default_config_path.has_value()) {
        return input.project_default_config_path;
    }
    return find_project_default_eval_config_path();
}

} // namespace

bool has_custom_eval_config(const EvaluatorSelection& selection) noexcept {
    return selection.source == EvaluatorSelectionSource::ExplicitConfig;
}

bool uses_project_default_eval_config(const EvaluatorSelection& selection) noexcept {
    return selection.source == EvaluatorSelectionSource::ProjectDefaultConfig;
}

bool uses_built_in_fallback_eval_config(const EvaluatorSelection& selection) noexcept {
    return selection.source == EvaluatorSelectionSource::BuiltInFallback;
}

EvaluationConfig resolve_evaluator_selection(const EvaluatorSelection& selection) noexcept {
    if (selection.config_override.has_value()) {
        return *selection.config_override;
    }
    return default_evaluation_config();
}

SearchOptions apply_evaluator_selection(SearchOptions options,
                                        const EvaluatorSelection& selection) noexcept {
    options.evaluation_config_override = selection.config_override;
    return options;
}

std::optional<EvaluatorSelection>
parse_evaluator_selection(const EvaluatorSelectionInput& input, std::string& error_message) {
    error_message.clear();

    EvaluatorSelection selection;
    if (input.config_path.has_value()) {
        if (input.config_path->empty()) {
            error_message = "invalid --eval-config value";
            return std::nullopt;
        }

        const EvaluationConfigLoadResult loaded =
            load_evaluation_config_file(*input.config_path);
        if (!loaded.ok()) {
            error_message = loaded.error;
            return std::nullopt;
        }
        selection.config_override = loaded.config;
        selection.config_path = input.config_path;
        selection.source = EvaluatorSelectionSource::ExplicitConfig;
        return selection;
    }

    const std::optional<std::filesystem::path> project_default_path =
        resolve_project_default_eval_config_path(input);
    if (!project_default_path.has_value()) {
        error_message =
            "project default eval config not found: data/eval/current_default.eval "
            "(run from the repository root or pass --eval-config PATH)";
        return std::nullopt;
    }

    const EvaluationConfigLoadResult loaded =
        load_evaluation_config_file(*project_default_path);
    if (!loaded.ok()) {
        error_message = "invalid project default eval config: " + loaded.error;
        return std::nullopt;
    }
    selection.config_override = loaded.config;
    selection.config_path = project_default_path->string();
    selection.source = EvaluatorSelectionSource::ProjectDefaultConfig;

    return selection;
}

} // namespace othello::tools
