#pragma once

#include <optional>
#include <string_view>

namespace othello::tools {

enum class OutputFormat {
    Text,
    Jsonl,
};

[[nodiscard]] std::optional<OutputFormat> parse_output_format(std::string_view text) noexcept;

} // namespace othello::tools
