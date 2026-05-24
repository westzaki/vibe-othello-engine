#pragma once

#include <cstdint>
#include <othello/endgame.hpp>
#include <othello/search.hpp>

namespace othello::tools {

[[nodiscard]] double rate(std::uint64_t numerator, std::uint64_t denominator) noexcept;
[[nodiscard]] double percentage(std::uint64_t numerator, std::uint64_t denominator) noexcept;
[[nodiscard]] double tt_hit_percentage(const SearchStats& stats) noexcept;
[[nodiscard]] double tt_hit_percentage(const ExactEndgameStats& stats) noexcept;

void add_search_stats(SearchStats& total, const SearchStats& stats) noexcept;
void add_exact_endgame_stats(ExactEndgameStats& total,
                             const ExactEndgameStats& stats) noexcept;

} // namespace othello::tools
