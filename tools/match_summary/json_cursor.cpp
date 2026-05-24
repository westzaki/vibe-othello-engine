#include "json_cursor.hpp"

#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>

namespace othello::match_summary::detail {

bool is_ascii_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

bool is_hex_digit(char character) noexcept {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

JsonCursor::JsonCursor(std::string_view text) noexcept : text_{text} {}

std::string_view JsonCursor::error() const noexcept {
    return error_;
}

void JsonCursor::skip_ws() noexcept {
    while (!text_.empty() && is_ascii_space(text_.front())) {
        text_.remove_prefix(1);
    }
}

bool JsonCursor::at_end() noexcept {
    skip_ws();
    return text_.empty();
}

bool JsonCursor::consume(char expected) {
    skip_ws();
    if (text_.empty() || text_.front() != expected) {
        set_error(std::string{"expected '"} + expected + "'");
        return false;
    }
    text_.remove_prefix(1);
    return true;
}

bool JsonCursor::consume_if(char expected) noexcept {
    skip_ws();
    if (!text_.empty() && text_.front() == expected) {
        text_.remove_prefix(1);
        return true;
    }
    return false;
}

bool JsonCursor::parse_string(std::string& value) {
    skip_ws();
    if (text_.empty() || text_.front() != '"') {
        set_error("expected string");
        return false;
    }
    text_.remove_prefix(1);

    value.clear();
    while (!text_.empty()) {
        const char character = text_.front();
        text_.remove_prefix(1);

        if (character == '"') {
            return true;
        }
        if (static_cast<unsigned char>(character) < 0x20) {
            set_error("unescaped control character in string");
            return false;
        }
        if (character != '\\') {
            value += character;
            continue;
        }
        if (text_.empty()) {
            set_error("unterminated string escape");
            return false;
        }

        const char escaped = text_.front();
        text_.remove_prefix(1);
        switch (escaped) {
        case '"':
            value += '"';
            break;
        case '\\':
            value += '\\';
            break;
        case '/':
            value += '/';
            break;
        case 'b':
            value += '\b';
            break;
        case 'f':
            value += '\f';
            break;
        case 'n':
            value += '\n';
            break;
        case 'r':
            value += '\r';
            break;
        case 't':
            value += '\t';
            break;
        case 'u':
            if (!skip_unicode_escape()) {
                return false;
            }
            value += '?';
            break;
        default:
            set_error("invalid string escape");
            return false;
        }
    }

    set_error("unterminated string");
    return false;
}

bool JsonCursor::parse_int(int& value) {
    skip_ws();
    const char* begin = text_.data();
    const char* end = text_.data() + text_.size();
    int parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{}) {
        set_error("expected integer");
        return false;
    }

    value = parsed;
    text_.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
    return true;
}

bool JsonCursor::parse_bool(bool& value) {
    skip_ws();
    if (text_.starts_with("true")) {
        value = true;
        text_.remove_prefix(4);
        return true;
    }
    if (text_.starts_with("false")) {
        value = false;
        text_.remove_prefix(5);
        return true;
    }

    set_error("expected boolean");
    return false;
}

bool JsonCursor::skip_value() {
    skip_ws();
    if (text_.empty()) {
        set_error("expected value");
        return false;
    }

    if (text_.front() == '"') {
        std::string ignored;
        return parse_string(ignored);
    }
    if (text_.front() == '{') {
        return skip_object();
    }
    if (text_.front() == '[') {
        return skip_array();
    }
    if (text_.starts_with("true")) {
        text_.remove_prefix(4);
        return true;
    }
    if (text_.starts_with("false")) {
        text_.remove_prefix(5);
        return true;
    }
    if (text_.starts_with("null")) {
        text_.remove_prefix(4);
        return true;
    }
    return skip_number();
}

void JsonCursor::set_error(std::string error) {
    if (error_.empty()) {
        error_ = std::move(error);
    }
}

bool JsonCursor::skip_unicode_escape() {
    if (text_.size() < 4) {
        set_error("short unicode escape");
        return false;
    }
    for (int index = 0; index < 4; ++index) {
        if (!is_hex_digit(text_[static_cast<std::size_t>(index)])) {
            set_error("invalid unicode escape");
            return false;
        }
    }
    text_.remove_prefix(4);
    return true;
}

bool JsonCursor::skip_object() {
    if (!consume('{')) {
        return false;
    }
    skip_ws();
    if (!text_.empty() && text_.front() == '}') {
        text_.remove_prefix(1);
        return true;
    }

    while (true) {
        std::string key;
        if (!parse_string(key) || !consume(':') || !skip_value()) {
            return false;
        }
        skip_ws();
        if (!text_.empty() && text_.front() == '}') {
            text_.remove_prefix(1);
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

bool JsonCursor::skip_array() {
    if (!consume('[')) {
        return false;
    }
    skip_ws();
    if (!text_.empty() && text_.front() == ']') {
        text_.remove_prefix(1);
        return true;
    }

    while (true) {
        if (!skip_value()) {
            return false;
        }
        skip_ws();
        if (!text_.empty() && text_.front() == ']') {
            text_.remove_prefix(1);
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

bool JsonCursor::skip_number() {
    skip_ws();
    std::size_t index = 0;
    if (index < text_.size() && text_[index] == '-') {
        ++index;
    }
    const std::size_t integer_begin = index;
    while (index < text_.size() && text_[index] >= '0' && text_[index] <= '9') {
        ++index;
    }
    if (index == integer_begin) {
        set_error("expected value");
        return false;
    }
    if (index < text_.size() && text_[index] == '.') {
        ++index;
        const std::size_t fraction_begin = index;
        while (index < text_.size() && text_[index] >= '0' && text_[index] <= '9') {
            ++index;
        }
        if (index == fraction_begin) {
            set_error("invalid number");
            return false;
        }
    }
    if (index < text_.size() && (text_[index] == 'e' || text_[index] == 'E')) {
        ++index;
        if (index < text_.size() && (text_[index] == '+' || text_[index] == '-')) {
            ++index;
        }
        const std::size_t exponent_begin = index;
        while (index < text_.size() && text_[index] >= '0' && text_[index] <= '9') {
            ++index;
        }
        if (index == exponent_begin) {
            set_error("invalid number");
            return false;
        }
    }

    text_.remove_prefix(index);
    return true;
}

} // namespace othello::match_summary::detail
