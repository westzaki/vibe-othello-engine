#include "parser.hpp"

#include "json_cursor.hpp"

#include <string>
#include <utility>

namespace othello::match_summary {

ParseResult parse_game_record(std::string_view line) {
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

} // namespace othello::match_summary
