#pragma once

#include "positions/position.hpp"

#include <cstdint>
#include <optional>
#include <othello/othello.hpp>
#include <string_view>
#include <vector>

namespace othello::benchmarks {

[[nodiscard]] std::uint64_t mix_checksum(std::uint64_t checksum,
                                         std::uint64_t value) noexcept;
[[nodiscard]] std::uint64_t side_checksum(Side side) noexcept;
[[nodiscard]] std::uint64_t board_checksum(const Board& board) noexcept;
[[nodiscard]] std::uint64_t search_result_checksum(const SearchResult& result) noexcept;
[[nodiscard]] std::optional<std::uint64_t> parse_positive_count(std::string_view text) noexcept;
[[nodiscard]] std::vector<Square> squares_from_bitboard(Bitboard bits);
[[nodiscard]] bool add_position(std::vector<Position>& positions, std::string_view name,
                                std::string_view board_text,
                                std::string_view phase = "smoke", std::string_view tags = {},
                                std::string_view notes = {});
[[nodiscard]] std::optional<std::vector<Position>> make_fixed_positions();

} // namespace othello::benchmarks
