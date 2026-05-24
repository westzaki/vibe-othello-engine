#include "common/cli.hpp"

#include <charconv>
#include <iostream>
#include <limits>
#include <system_error>

namespace othello::tools {

std::optional<int> parse_int(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> parse_non_negative_int(std::string_view text) noexcept {
    const auto value = parse_int(text);
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> parse_positive_int(std::string_view text) noexcept {
    const auto value = parse_int(text);
    if (!value.has_value() || *value <= 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> parse_positive_count(std::string_view text) noexcept {
    const auto value = parse_u64(text);
    if (!value.has_value() || *value == 0) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::size_t> parse_size_t(std::string_view text) noexcept {
    const auto value = parse_u64(text);
    if (!value.has_value() || *value > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
}

std::optional<std::size_t> parse_entry_count(std::string_view text) noexcept {
    return parse_size_t(text);
}

std::optional<bool> parse_on_off(std::string_view text) noexcept {
    if (text == "on") {
        return true;
    }
    if (text == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<bool> parse_bool_true_false(std::string_view text) noexcept {
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::set<int>> parse_comma_separated_int_set(std::string_view text) {
    std::set<int> values;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto token = text.substr(begin, end - begin);
        const auto value = parse_int(token);
        if (!value.has_value()) {
            return std::nullopt;
        }
        values.insert(*value);
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    return values;
}

std::optional<std::vector<int>> parse_comma_separated_positive_depths(std::string_view text) {
    std::vector<int> depths;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto token = text.substr(begin, end - begin);
        const auto depth = parse_positive_int(token);
        if (!depth.has_value()) {
            return std::nullopt;
        }
        depths.push_back(*depth);
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    if (depths.empty()) {
        return std::nullopt;
    }
    return depths;
}

std::optional<std::string_view>
next_argument(std::span<char* const> args, std::size_t& index, std::string_view option) {
    if (index + 1 >= args.size()) {
        std::cerr << "missing value for " << option << '\n';
        return std::nullopt;
    }
    ++index;
    return std::string_view{args[index]};
}

} // namespace othello::tools
