#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace othello::benchmarks {

[[nodiscard]] std::vector<std::string_view> split_tags(std::string_view tags);
[[nodiscard]] bool has_tag(std::string_view tags, std::string_view tag);

} // namespace othello::benchmarks
