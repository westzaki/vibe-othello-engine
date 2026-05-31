#include "common/evaluator_selection.hpp"

#include "common/eval_config_io.hpp"

namespace othello::tools {

bool has_custom_eval_config(const EvaluatorSelection& selection) noexcept {
    return selection.config_override.has_value();
}

EvaluationConfig resolve_evaluator_selection(const EvaluatorSelection& selection) noexcept {
    if (selection.config_override.has_value()) {
        return *selection.config_override;
    }
    return evaluation_config_for_preset(selection.preset);
}

SearchOptions apply_evaluator_selection(SearchOptions options,
                                        const EvaluatorSelection& selection) noexcept {
    options.evaluation_preset = selection.preset;
    options.evaluation_config_override = selection.config_override;
    return options;
}

std::optional<EvaluatorSelection>
parse_evaluator_selection(const EvaluatorSelectionInput& input, std::string& error_message) {
    error_message.clear();

    if (input.preset_name.has_value() && input.config_path.has_value()) {
        error_message = "cannot combine --eval-preset and --eval-config";
        return std::nullopt;
    }

    EvaluatorSelection selection;
    if (input.preset_name.has_value()) {
        const std::optional<EvaluationPreset> preset =
            evaluation_preset_from_name(*input.preset_name);
        if (!preset.has_value()) {
            error_message = "unknown evaluation preset: " + *input.preset_name;
            return std::nullopt;
        }
        selection.preset = *preset;
        return selection;
    }

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
    }

    return selection;
}

} // namespace othello::tools
