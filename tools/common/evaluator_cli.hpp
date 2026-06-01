#pragma once

#include "common/evaluator_selection.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace othello::tools {

enum class EvaluatorCliParseResult {
    NotMatched,
    Parsed,
    Error,
};

struct EvaluatorCliParseOptions {
    std::string_view missing_eval_config_message = "invalid --eval-config value";
    std::string_view empty_eval_config_message = "invalid --eval-config value";
    std::string_view duplicate_eval_config_message =
        "--eval-config may only be specified once";
    bool reject_empty_eval_config = true;
    bool reject_duplicate_eval_config = true;
};

struct EvaluatorCliParseState {
    EvaluatorSelectionInput input;
    bool eval_config_seen = false;
};

[[nodiscard]] std::string_view evaluator_cli_usage() noexcept;
[[nodiscard]] std::string_view evaluator_cli_help() noexcept;

[[nodiscard]] EvaluatorCliParseResult parse_evaluator_cli_option(
    std::span<char* const> args, std::size_t& index, EvaluatorCliParseState& state,
    std::string& error_message,
    const EvaluatorCliParseOptions& options = EvaluatorCliParseOptions{});

} // namespace othello::tools
