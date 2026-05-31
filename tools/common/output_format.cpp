#include "common/output_format.hpp"

namespace othello::tools {

std::optional<OutputFormat> parse_output_format(std::string_view text) noexcept {
    if (text == "text") {
        return OutputFormat::Text;
    }
    if (text == "jsonl") {
        return OutputFormat::Jsonl;
    }
    return std::nullopt;
}

} // namespace othello::tools
