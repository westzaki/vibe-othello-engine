#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace othello::benchmarks {

[[nodiscard]] std::vector<std::string_view> split_tags(std::string_view tags);
[[nodiscard]] bool has_tag(std::string_view tags, std::string_view tag);
[[nodiscard]] std::string_view mobility_bucket(int legal_move_count) noexcept;
void check_tag_consistency(std::string_view position_name, std::string_view tags,
                           std::string_view tag, bool expected, int& warning_count);

} // namespace othello::benchmarks
