#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace othello::match_summary {

struct GameRecord {
    int game_index = 0;
    std::string player_a_spec;
    std::string player_b_spec;
    std::string black_spec;
    std::string white_spec;
    bool black_is_player_a = true;
    int opening_index = 0;
    std::string opening_name;
    std::string winner;
    int black_score = 0;
    int white_score = 0;
    int score_diff_from_player_a = 0;
    int plies = 0;
    int passes = 0;
    bool illegal_or_error = false;
};

struct ParseResult {
    bool ok = false;
    GameRecord record;
    std::string error;
};

struct OpeningSummary {
    int opening_index = 0;
    std::string opening_name;
    int games = 0;
    int valid_games = 0;
    int player_a_wins = 0;
    int player_b_wins = 0;
    int draws = 0;
    double average_disc_diff_from_player_a = 0.0;
};

struct Summary {
    int games = 0;
    int valid_games = 0;
    int error_games = 0;
    int player_a_wins = 0;
    int player_b_wins = 0;
    int draws = 0;
    double player_a_win_rate = 0.0;
    double player_b_win_rate = 0.0;
    double average_disc_diff_from_player_a = 0.0;
    double average_plies = 0.0;
    double average_passes = 0.0;
    int unique_openings_count = 0;
    std::vector<std::string> player_a_specs;
    std::vector<std::string> player_b_specs;
    std::vector<OpeningSummary> openings;
};

namespace detail {

[[nodiscard]] inline bool is_ascii_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

[[nodiscard]] inline bool is_hex_digit(char character) noexcept {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

class JsonCursor {
public:
    explicit JsonCursor(std::string_view text) noexcept : text_{text} {}

    [[nodiscard]] std::string_view error() const noexcept {
        return error_;
    }

    void skip_ws() noexcept {
        while (!text_.empty() && is_ascii_space(text_.front())) {
            text_.remove_prefix(1);
        }
    }

    [[nodiscard]] bool at_end() noexcept {
        skip_ws();
        return text_.empty();
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        skip_ws();
        if (text_.empty() || text_.front() != expected) {
            set_error(std::string{"expected '"} + expected + "'");
            return false;
        }
        text_.remove_prefix(1);
        return true;
    }

    [[nodiscard]] bool consume_if(char expected) noexcept {
        skip_ws();
        if (!text_.empty() && text_.front() == expected) {
            text_.remove_prefix(1);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool parse_string(std::string& value) noexcept {
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

    [[nodiscard]] bool parse_int(int& value) noexcept {
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

    [[nodiscard]] bool parse_bool(bool& value) noexcept {
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

    [[nodiscard]] bool skip_value() noexcept {
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

private:
    std::string_view text_;
    std::string error_;

    void set_error(std::string error) noexcept {
        if (error_.empty()) {
            error_ = std::move(error);
        }
    }

    [[nodiscard]] bool skip_unicode_escape() noexcept {
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

    [[nodiscard]] bool skip_object() noexcept {
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

    [[nodiscard]] bool skip_array() noexcept {
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

    [[nodiscard]] bool skip_number() noexcept {
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
};

[[nodiscard]] inline bool contains_string(std::span<const std::string> values,
                                          const std::string& value) {
    for (const std::string& existing : values) {
        if (existing == value) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::size_t opening_summary_index(std::vector<OpeningSummary>& openings,
                                                       const GameRecord& record) {
    for (std::size_t index = 0; index < openings.size(); ++index) {
        if (openings[index].opening_index == record.opening_index &&
            openings[index].opening_name == record.opening_name) {
            return index;
        }
    }

    openings.push_back(OpeningSummary{.opening_index = record.opening_index,
                                      .opening_name = record.opening_name});
    return openings.size() - 1;
}

} // namespace detail

[[nodiscard]] inline ParseResult parse_game_record(std::string_view line) {
    detail::JsonCursor cursor{line};
    GameRecord record;

    bool has_game_index = false;
    bool has_player_a_spec = false;
    bool has_player_b_spec = false;
    bool has_black_spec = false;
    bool has_white_spec = false;
    bool has_black_is_player_a = false;
    bool has_opening_index = false;
    bool has_opening_name = false;
    bool has_winner = false;
    bool has_black_score = false;
    bool has_white_score = false;
    bool has_score_diff_from_player_a = false;
    bool has_plies = false;
    bool has_passes = false;
    bool has_illegal_or_error = false;

    if (!cursor.consume('{')) {
        return ParseResult{.error = std::string{cursor.error()}};
    }
    bool first = true;
    while (true) {
        if (cursor.consume_if('}')) {
            break;
        }
        if (!first && !cursor.consume(',')) {
            return ParseResult{.error = std::string{cursor.error()}};
        }
        first = false;

        cursor.skip_ws();
        std::string key;
        if (!cursor.parse_string(key) || !cursor.consume(':')) {
            return ParseResult{.error = std::string{cursor.error()}};
        }

        if (key == "game_index") {
            has_game_index = cursor.parse_int(record.game_index);
        } else if (key == "player_a_spec") {
            has_player_a_spec = cursor.parse_string(record.player_a_spec);
        } else if (key == "player_b_spec") {
            has_player_b_spec = cursor.parse_string(record.player_b_spec);
        } else if (key == "black_spec") {
            has_black_spec = cursor.parse_string(record.black_spec);
        } else if (key == "white_spec") {
            has_white_spec = cursor.parse_string(record.white_spec);
        } else if (key == "black_is_player_a") {
            has_black_is_player_a = cursor.parse_bool(record.black_is_player_a);
        } else if (key == "opening_index") {
            has_opening_index = cursor.parse_int(record.opening_index);
        } else if (key == "opening_name") {
            has_opening_name = cursor.parse_string(record.opening_name);
        } else if (key == "winner") {
            has_winner = cursor.parse_string(record.winner);
        } else if (key == "black_score") {
            has_black_score = cursor.parse_int(record.black_score);
        } else if (key == "white_score") {
            has_white_score = cursor.parse_int(record.white_score);
        } else if (key == "score_diff_from_player_a") {
            has_score_diff_from_player_a = cursor.parse_int(record.score_diff_from_player_a);
        } else if (key == "plies") {
            has_plies = cursor.parse_int(record.plies);
        } else if (key == "passes") {
            has_passes = cursor.parse_int(record.passes);
        } else if (key == "illegal_or_error") {
            has_illegal_or_error = cursor.parse_bool(record.illegal_or_error);
        } else if (!cursor.skip_value()) {
            return ParseResult{.error = std::string{cursor.error()}};
        }

        if (!cursor.error().empty()) {
            return ParseResult{.error = std::string{cursor.error()}};
        }

        cursor.skip_ws();
        if (!cursor.at_end()) {
            // The loop consumes either a comma or closing brace at its start. If a closing brace is
            // next, leave it for the next iteration so trailing data is still detected below.
            continue;
        }
        return ParseResult{.error = "unterminated object"};
    }

    if (!cursor.at_end()) {
        return ParseResult{.error = "trailing data after object"};
    }

    if (!has_game_index || !has_player_a_spec || !has_player_b_spec || !has_black_spec ||
        !has_white_spec || !has_black_is_player_a || !has_opening_index || !has_opening_name ||
        !has_winner || !has_black_score || !has_white_score || !has_score_diff_from_player_a ||
        !has_plies || !has_passes || !has_illegal_or_error) {
        return ParseResult{.error = "missing required field"};
    }

    return ParseResult{.ok = true, .record = std::move(record)};
}

[[nodiscard]] inline Summary summarize(std::span<const GameRecord> records) {
    Summary summary;
    summary.games = static_cast<int>(records.size());

    int total_disc_diff = 0;
    int total_plies = 0;
    int total_passes = 0;
    std::vector<int> opening_diff_totals;

    for (const GameRecord& record : records) {
        if (!detail::contains_string(summary.player_a_specs, record.player_a_spec)) {
            summary.player_a_specs.push_back(record.player_a_spec);
        }
        if (!detail::contains_string(summary.player_b_specs, record.player_b_spec)) {
            summary.player_b_specs.push_back(record.player_b_spec);
        }

        const std::size_t opening_index = detail::opening_summary_index(summary.openings, record);
        if (opening_diff_totals.size() < summary.openings.size()) {
            opening_diff_totals.push_back(0);
        }
        OpeningSummary& opening = summary.openings[opening_index];
        ++opening.games;

        if (record.illegal_or_error) {
            ++summary.error_games;
            continue;
        }

        ++summary.valid_games;
        ++opening.valid_games;
        total_disc_diff += record.score_diff_from_player_a;
        total_plies += record.plies;
        total_passes += record.passes;
        opening_diff_totals[opening_index] += record.score_diff_from_player_a;

        if (record.score_diff_from_player_a > 0) {
            ++summary.player_a_wins;
            ++opening.player_a_wins;
        } else if (record.score_diff_from_player_a < 0) {
            ++summary.player_b_wins;
            ++opening.player_b_wins;
        } else {
            ++summary.draws;
            ++opening.draws;
        }
    }

    summary.unique_openings_count = static_cast<int>(summary.openings.size());
    if (summary.valid_games > 0) {
        const double valid_games = static_cast<double>(summary.valid_games);
        summary.player_a_win_rate = static_cast<double>(summary.player_a_wins) / valid_games;
        summary.player_b_win_rate = static_cast<double>(summary.player_b_wins) / valid_games;
        summary.average_disc_diff_from_player_a = static_cast<double>(total_disc_diff) / valid_games;
        summary.average_plies = static_cast<double>(total_plies) / valid_games;
        summary.average_passes = static_cast<double>(total_passes) / valid_games;
    }

    for (std::size_t index = 0; index < summary.openings.size(); ++index) {
        OpeningSummary& opening = summary.openings[index];
        if (opening.valid_games > 0) {
            opening.average_disc_diff_from_player_a =
                static_cast<double>(opening_diff_totals[index]) /
                static_cast<double>(opening.valid_games);
        }
    }

    return summary;
}

} // namespace othello::match_summary
