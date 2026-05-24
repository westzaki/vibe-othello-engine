#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::nboard {

[[nodiscard]] std::string nboard_command(int version);
[[nodiscard]] std::string set_depth_command(int depth);
[[nodiscard]] std::string set_game_command(const std::vector<std::string>& moves);
[[nodiscard]] std::string ping_command(int id);
[[nodiscard]] std::string go_command();
[[nodiscard]] std::string quit_command();
[[nodiscard]] std::string move_command(std::string_view move);

} // namespace othello::tools::nboard
