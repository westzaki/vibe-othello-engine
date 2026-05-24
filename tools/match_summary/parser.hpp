#pragma once

#include "types.hpp"

#include <string_view>

namespace othello::match_summary {

[[nodiscard]] ParseResult parse_game_record(std::string_view line);

} // namespace othello::match_summary
