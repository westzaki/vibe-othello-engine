#pragma once

#include "types.hpp"

#include <span>

namespace othello::match_summary {

[[nodiscard]] Summary summarize(std::span<const GameRecord> records);

} // namespace othello::match_summary
