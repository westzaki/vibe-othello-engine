#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace othello::tools {

[[nodiscard]] std::optional<int> parse_int(std::string_view text) noexcept;
[[nodiscard]] std::optional<int> parse_non_negative_int(std::string_view text) noexcept;
[[nodiscard]] std::optional<int> parse_positive_int(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::uint64_t> parse_positive_count(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::size_t> parse_size_t(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::size_t> parse_entry_count(std::string_view text) noexcept;
[[nodiscard]] std::optional<bool> parse_on_off(std::string_view text) noexcept;
[[nodiscard]] std::optional<bool> parse_bool_true_false(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::set<int>>
parse_comma_separated_int_set(std::string_view text);
[[nodiscard]] std::optional<std::vector<int>>
parse_comma_separated_positive_depths(std::string_view text);

[[nodiscard]] std::optional<std::string_view>
next_argument(std::span<char* const> args, std::size_t& index, std::string_view option);

} // namespace othello::tools
