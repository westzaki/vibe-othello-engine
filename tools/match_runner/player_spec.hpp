#pragma once

#include "types.hpp"

#include <cstdint>
#include <optional>
#include <othello/othello.hpp>
#include <string_view>

namespace othello::match_runner {

[[nodiscard]] std::optional<int> parse_non_negative_int(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept;
[[nodiscard]] std::optional<bool> parse_on_off(std::string_view text) noexcept;
[[nodiscard]] std::optional<PlayerSpec> parse_player_spec(std::string_view text);
[[nodiscard]] SearchOptions make_search_options(const PlayerSpec& spec) noexcept;

} // namespace othello::match_runner
