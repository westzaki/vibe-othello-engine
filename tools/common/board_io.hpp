#pragma once

#include <optional>
#include <string>

namespace othello::tools {

[[nodiscard]] std::optional<std::string> read_text_file(const std::string& path);
[[nodiscard]] std::optional<std::string> read_stdin_text();

} // namespace othello::tools
