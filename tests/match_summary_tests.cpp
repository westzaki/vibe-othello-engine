#include "../tools/match_summary_core.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace summary = othello::match_summary;

namespace {

[[nodiscard]] std::string record_json(int game_index, int opening_index, std::string opening_name,
                                      int score_diff_from_player_a, int plies, int passes,
                                      bool illegal_or_error = false) {
    return "{\"game_index\":" + std::to_string(game_index) +
           ",\"player_a_spec\":\"search:depth=3\""
           ",\"player_b_spec\":\"random\""
           ",\"black_spec\":\"search:depth=3\""
           ",\"white_spec\":\"random\""
           ",\"black_is_player_a\":true"
           ",\"opening_index\":" +
           std::to_string(opening_index) + ",\"opening_name\":\"" + opening_name +
           "\",\"winner\":\"black\""
           ",\"black_score\":40"
           ",\"white_score\":24"
           ",\"score_diff_from_player_a\":" +
           std::to_string(score_diff_from_player_a) + ",\"plies\":" + std::to_string(plies) +
           ",\"passes\":" + std::to_string(passes) + ",\"illegal_or_error\":" +
           (illegal_or_error ? "true" : "false") + "}";
}

} // namespace

TEST_CASE("Match summary parses a valid JSONL record", "[match-summary]") {
    const summary::ParseResult result = summary::parse_game_record(record_json(0, 0, "initial", 16, 60, 2));

    REQUIRE(result.ok);
    CHECK(result.record.game_index == 0);
    CHECK(result.record.player_a_spec == "search:depth=3");
    CHECK(result.record.player_b_spec == "random");
    CHECK(result.record.opening_name == "initial");
    CHECK(result.record.score_diff_from_player_a == 16);
    CHECK_FALSE(result.record.illegal_or_error);
}

TEST_CASE("Match summary skips escaped strings and arrays", "[match-summary]") {
    const std::string line =
        "{\"game_index\":1"
        ",\"player_a_spec\":\"search:depth=3\""
        ",\"player_b_spec\":\"random\""
        ",\"black_spec\":\"random\""
        ",\"white_spec\":\"search:depth=3\""
        ",\"black_is_player_a\":false"
        ",\"opening_index\":1"
        ",\"opening_name\":\"c4-c3\""
        ",\"winner\":\"white\""
        ",\"black_score\":10"
        ",\"white_score\":54"
        ",\"score_diff_from_player_a\":44"
        ",\"plies\":58"
        ",\"passes\":3"
        ",\"illegal_or_error\":false"
        ",\"start_board\":\"........\\n........\\nside=B\""
        ",\"moves\":[\"d3\",\"c3\"]"
        "}";

    const summary::ParseResult result = summary::parse_game_record(line);

    REQUIRE(result.ok);
    CHECK(result.record.opening_index == 1);
    CHECK(result.record.opening_name == "c4-c3");
    CHECK(result.record.score_diff_from_player_a == 44);
}

TEST_CASE("Match summary ignores unknown fields", "[match-summary]") {
    std::string line = record_json(0, 0, "initial", 4, 60, 2);
    line.insert(line.size() - 1,
                ",\"nodes_black\":123,\"nodes_white\":0,\"nodes_player_a\":123,"
                "\"nodes_player_b\":0,\"time_ms_black\":1.25,\"time_ms_white\":0,"
                "\"time_ms_player_a\":1.25,\"time_ms_player_b\":0,"
                "\"extra\":{\"nested\":[true,false,null,12,\"x\"]}");

    const summary::ParseResult result = summary::parse_game_record(line);

    REQUIRE(result.ok);
    CHECK(result.record.score_diff_from_player_a == 4);
}

TEST_CASE("Match summary rejects missing required fields", "[match-summary]") {
    const summary::ParseResult result =
        summary::parse_game_record("{\"game_index\":0,\"player_a_spec\":\"a\"}");

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("Match summary rejects invalid JSONL", "[match-summary]") {
    const summary::ParseResult result = summary::parse_game_record("{\"game_index\":0,]");

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("Match summary aggregates wins draws and averages", "[match-summary]") {
    const std::vector<summary::GameRecord> records{
        summary::parse_game_record(record_json(0, 0, "initial", 10, 60, 2)).record,
        summary::parse_game_record(record_json(1, 0, "initial", -4, 58, 3)).record,
        summary::parse_game_record(record_json(2, 1, "c4-c3", 0, 62, 1)).record,
    };

    const summary::Summary result = summary::summarize(records);

    CHECK(result.games == 3);
    CHECK(result.valid_games == 3);
    CHECK(result.error_games == 0);
    CHECK(result.player_a_wins == 1);
    CHECK(result.player_b_wins == 1);
    CHECK(result.draws == 1);
    CHECK(result.average_disc_diff_from_player_a == 2.0);
    CHECK(result.average_plies == 60.0);
    CHECK(result.average_passes == 2.0);
}

TEST_CASE("Match summary aggregates by opening", "[match-summary]") {
    const std::vector<summary::GameRecord> records{
        summary::parse_game_record(record_json(0, 0, "initial", 10, 60, 2)).record,
        summary::parse_game_record(record_json(1, 0, "initial", -4, 58, 3)).record,
        summary::parse_game_record(record_json(2, 1, "c4-c3", 6, 62, 1)).record,
    };

    const summary::Summary result = summary::summarize(records);

    REQUIRE(result.openings.size() == 2);
    CHECK(result.openings[0].opening_index == 0);
    CHECK(result.openings[0].games == 2);
    CHECK(result.openings[0].player_a_wins == 1);
    CHECK(result.openings[0].player_b_wins == 1);
    CHECK(result.openings[0].average_disc_diff_from_player_a == 3.0);
    CHECK(result.openings[1].opening_index == 1);
    CHECK(result.openings[1].games == 1);
    CHECK(result.openings[1].player_a_wins == 1);
    CHECK(result.openings[1].average_disc_diff_from_player_a == 6.0);
}

TEST_CASE("Match summary counts illegal games as error games", "[match-summary]") {
    const std::vector<summary::GameRecord> records{
        summary::parse_game_record(record_json(0, 0, "initial", 10, 60, 2)).record,
        summary::parse_game_record(record_json(1, 0, "initial", -4, 12, 0, true)).record,
    };

    const summary::Summary result = summary::summarize(records);

    CHECK(result.games == 2);
    CHECK(result.valid_games == 1);
    CHECK(result.error_games == 1);
    CHECK(result.player_a_wins == 1);
    CHECK(result.player_b_wins == 0);
    CHECK(result.average_disc_diff_from_player_a == 10.0);
    REQUIRE(result.openings.size() == 1);
    CHECK(result.openings[0].games == 2);
    CHECK(result.openings[0].valid_games == 1);
    CHECK(result.openings[0].error_games == 1);
}
