#include "common/evaluator_cli.hpp"

namespace othello::tools {

std::string_view evaluator_cli_usage() noexcept {
    return "[--eval-config PATH]";
}

std::string_view evaluator_cli_help() noexcept {
    return "  --eval-config PATH load evaluator weights from a .eval config file\n";
}

EvaluatorCliParseResult parse_evaluator_cli_option(
    std::span<char* const> args, std::size_t& index, EvaluatorCliParseState& state,
    std::string& error_message, const EvaluatorCliParseOptions& options) {
    error_message.clear();

    const std::string_view option{args[index]};
    if (option != "--eval-config") {
        return EvaluatorCliParseResult::NotMatched;
    }

    if (index + 1 >= args.size()) {
        error_message = options.missing_eval_config_message;
        return EvaluatorCliParseResult::Error;
    }

    ++index;
    const std::string_view value{args[index]};
    if (options.reject_empty_eval_config && value.empty()) {
        error_message = options.empty_eval_config_message;
        return EvaluatorCliParseResult::Error;
    }
    if (options.reject_duplicate_eval_config && state.eval_config_seen) {
        error_message = options.duplicate_eval_config_message;
        return EvaluatorCliParseResult::Error;
    }

    state.input.config_path = std::string{value};
    state.eval_config_seen = true;
    return EvaluatorCliParseResult::Parsed;
}

} // namespace othello::tools
