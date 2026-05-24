#include "protocols/nboard/parser.hpp"

#include <algorithm>
#include <cctype>

namespace othello::tools::nboard {

namespace {

[[nodiscard]] bool is_ascii_space(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] std::string lower_ascii(std::string_view text) {
    std::string lowered{text};
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return lowered;
}

} // namespace

std::string trim_ascii(std::string_view text) {
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

std::vector<std::string_view> split_ascii_words(std::string_view text) {
    std::vector<std::string_view> words;
    while (!text.empty()) {
        while (!text.empty() && is_ascii_space(text.front())) {
            text.remove_prefix(1);
        }
        if (text.empty()) {
            break;
        }
        std::size_t end = 0;
        while (end < text.size() && !is_ascii_space(text[end])) {
            ++end;
        }
        words.push_back(text.substr(0, end));
        text.remove_prefix(end);
    }
    return words;
}

std::optional<NBoardMove> parse_move_token(std::string_view token) {
    std::string normalized = lower_ascii(trim_ascii(token));
    if (const std::size_t slash = normalized.find('/'); slash != std::string::npos) {
        normalized.erase(slash);
    }
    if (normalized == "pass" || normalized == "pa") {
        return NBoardMove{.pass = true, .square = std::nullopt, .text = "pass"};
    }

    if (normalized.size() != 2) {
        return std::nullopt;
    }
    if (normalized[0] < 'a' || normalized[0] > 'h' || normalized[1] < '1' ||
        normalized[1] > '8') {
        return std::nullopt;
    }

    const auto square = square_from_string(normalized);
    if (!square.has_value()) {
        return std::nullopt;
    }
    return NBoardMove{.pass = false, .square = *square, .text = normalized};
}

std::optional<NBoardMove> parse_go_move_line(std::string_view line) {
    const std::string trimmed = trim_ascii(line);
    constexpr std::string_view prefix = "===";
    if (!std::string_view{trimmed}.starts_with(prefix)) {
        return std::nullopt;
    }

    std::string_view rest{trimmed};
    rest.remove_prefix(prefix.size());
    const auto words = split_ascii_words(rest);
    if (words.empty()) {
        return std::nullopt;
    }
    if (words.size() >= 2 && lower_ascii(words[0]) == "move") {
        return parse_move_token(words[1]);
    }
    return parse_move_token(words[0]);
}

bool is_pong_line(std::string_view line, std::string_view id) {
    const std::string trimmed = trim_ascii(line);
    const auto words = split_ascii_words(trimmed);
    if (words.size() != 2) {
        return false;
    }
    return lower_ascii(words[0]) == "pong" && words[1] == id;
}

} // namespace othello::tools::nboard
