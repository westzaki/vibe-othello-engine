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
    }

    return selection;
}

} // namespace othello::tools
