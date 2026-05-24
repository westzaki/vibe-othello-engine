#pragma once

#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::nboard {

struct NBoardMove {
    bool pass = false;
    std::optional<Square> square;
    std::string text;
};

[[nodiscard]] std::string trim_ascii(std::string_view text);
[[nodiscard]] std::vector<std::string_view> split_ascii_words(std::string_view text);
[[nodiscard]] std::optional<NBoardMove> parse_move_token(std::string_view token);
[[nodiscard]] std::optional<NBoardMove> parse_go_move_line(std::string_view line);
[[nodiscard]] bool is_pong_line(std::string_view line, std::string_view id);

} // namespace othello::tools::nboard
