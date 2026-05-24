#pragma once

#include "types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace othello::match_runner {

[[nodiscard]] bool is_ascii_space(char character) noexcept;
[[nodiscard]] std::string_view trim_ascii_space(std::string_view text) noexcept;
[[nodiscard]] Opening default_opening();
[[nodiscard]] std::vector<std::string> split_ascii_whitespace(std::string_view text);
[[nodiscard]] OpeningParseResult parse_opening_line(std::string_view line);

} // namespace othello::match_runner
