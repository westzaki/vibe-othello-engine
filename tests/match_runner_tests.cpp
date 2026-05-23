#include "../tools/match_runner_core.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace runner = othello::match_runner;

TEST_CASE("Search player specs require positive depth", "[match-runner]") {
    CHECK_FALSE(runner::parse_player_spec("search:depth=0").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=-1").has_value());
    CHECK(runner::parse_player_spec("search:depth=1").has_value());
}

TEST_CASE("First versus first reaches a legal terminal result", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    REQUIRE(first.has_value());

    const runner::GameRecord record = runner::run_game(0, *first, *first, true, 7);

    CHECK_FALSE(record.illegal_or_error);
    CHECK(record.plies > 0);
    CHECK(record.black_score + record.white_score == 64);
    CHECK(record.score_diff_from_black == record.black_score - record.white_score);
    CHECK(record.score_diff_from_player_a == record.score_diff_from_black);
    CHECK(static_cast<int>(record.moves.size()) == record.plies);
}

TEST_CASE("Random versus random is reproducible with a fixed seed", "[match-runner]") {
    const auto random = runner::parse_player_spec("random");
    REQUIRE(random.has_value());

    const runner::GameRecord first_run = runner::run_game(0, *random, *random, true, 1234);
    const runner::GameRecord second_run = runner::run_game(0, *random, *random, true, 1234);

    CHECK(first_run == second_run);
    CHECK_FALSE(first_run.illegal_or_error);
    CHECK(first_run.black_score + first_run.white_score == 64);
}

TEST_CASE("Swap sides alternates black and white specs", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    const auto random = runner::parse_player_spec("random");
    REQUIRE(first.has_value());
    REQUIRE(random.has_value());

    const runner::MatchConfig config{
        .player_a = *first,
        .player_b = *random,
        .games = 2,
        .swap_sides = true,
        .seed = 99,
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 2);
    CHECK(records[0].black_spec == "first");
    CHECK(records[0].white_spec == "random");
    CHECK(records[0].black_is_player_a);
    CHECK(records[0].player_a_spec == "first");
    CHECK(records[0].player_b_spec == "random");
    CHECK(records[0].score_diff_from_player_a == records[0].score_diff_from_black);
    CHECK(records[1].black_spec == "random");
    CHECK(records[1].white_spec == "first");
    CHECK_FALSE(records[1].black_is_player_a);
    CHECK(records[1].player_a_spec == "first");
    CHECK(records[1].player_b_spec == "random");
    CHECK(records[1].score_diff_from_player_a == -records[1].score_diff_from_black);
}

TEST_CASE("Run match emits the requested number of games", "[match-runner]") {
    const auto eval = runner::parse_player_spec("eval");
    const auto first = runner::parse_player_spec("first");
    REQUIRE(eval.has_value());
    REQUIRE(first.has_value());

    const runner::MatchConfig config{
        .player_a = *eval,
        .player_b = *first,
        .games = 3,
        .swap_sides = false,
        .seed = 1,
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 3);
    for (const runner::GameRecord& record : records) {
        CHECK_FALSE(record.illegal_or_error);
        CHECK(record.black_score + record.white_score == 64);
    }
}
