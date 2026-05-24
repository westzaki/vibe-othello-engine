#pragma once

#include <string>
#include <string_view>

namespace othello::match_summary::detail {

[[nodiscard]] bool is_ascii_space(char character) noexcept;
[[nodiscard]] bool is_hex_digit(char character) noexcept;

class JsonCursor {
public:
    explicit JsonCursor(std::string_view text) noexcept;

    [[nodiscard]] std::string_view error() const noexcept;

    void skip_ws() noexcept;
    [[nodiscard]] bool at_end() noexcept;
    [[nodiscard]] bool consume(char expected);
    [[nodiscard]] bool consume_if(char expected) noexcept;
    [[nodiscard]] bool parse_string(std::string& value);
    [[nodiscard]] bool parse_int(int& value);
    [[nodiscard]] bool parse_bool(bool& value);
    [[nodiscard]] bool skip_value();

private:
    std::string_view text_;
    std::string error_;

    void set_error(std::string error);
    [[nodiscard]] bool skip_unicode_escape();
    [[nodiscard]] bool skip_object();
    [[nodiscard]] bool skip_array();
    [[nodiscard]] bool skip_number();
};

} // namespace othello::match_summary::detail
