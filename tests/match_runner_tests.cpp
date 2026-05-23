#include "../tools/match_runner_core.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace runner = othello::match_runner;

namespace {

[[nodiscard]] bool same_board(const othello::Board& lhs, const othello::Board& rhs) noexcept {
    return lhs.black == rhs.black && lhs.white == rhs.white && lhs.side_to_move == rhs.side_to_move;
}

} // namespace

TEST_CASE("Search player specs require positive depth", "[match-runner]") {
    CHECK_FALSE(runner::parse_player_spec("search:depth=0").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=-1").has_value());
    CHECK(runner::parse_player_spec("search:depth=1").has_value());
}

TEST_CASE("Opening parser accepts initial positions", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("initial:");

    REQUIRE(result.ok);
    REQUIRE(result.has_opening);
    CHECK(result.opening.name == "initial");
    CHECK(result.opening.moves.empty());
    CHECK(same_board(result.opening.start_board, othello::Board::initial()));
}

TEST_CASE("Opening parser accepts legal move sequences", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("c4-c3: c4 c3");

    REQUIRE(result.ok);
    REQUIRE(result.has_opening);
    CHECK(result.opening.name == "c4-c3");
    REQUIRE(result.opening.moves.size() == 2);
    CHECK(result.opening.moves[0] == "c4");
    CHECK(result.opening.moves[1] == "c3");
    CHECK_FALSE(same_board(result.opening.start_board, othello::Board::initial()));
    CHECK(result.opening.start_board.side_to_move == othello::Side::Black);
}

TEST_CASE("Opening parser rejects invalid coordinates", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("bad: z9");

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("Opening parser rejects illegal moves", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("bad: a1");

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
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

TEST_CASE("Swap-side match pairs games on the same opening", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    const auto random = runner::parse_player_spec("random");
    const runner::OpeningParseResult initial = runner::parse_opening_line("initial:");
    const runner::OpeningParseResult c4_c3 = runner::parse_opening_line("c4-c3: c4 c3");
    REQUIRE(first.has_value());
    REQUIRE(random.has_value());
    REQUIRE(initial.ok);
    REQUIRE(initial.has_opening);
    REQUIRE(c4_c3.ok);
    REQUIRE(c4_c3.has_opening);

    const runner::MatchConfig config{
        .player_a = *first,
        .player_b = *random,
        .games = 4,
        .swap_sides = true,
        .seed = 99,
        .openings = {initial.opening, c4_c3.opening},
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 4);
    CHECK(records[0].opening_index == 0);
    CHECK(records[1].opening_index == 0);
    CHECK(records[0].opening_name == "initial");
    CHECK(records[1].opening_name == "initial");
    CHECK(records[2].opening_index == 1);
    CHECK(records[3].opening_index == 1);
    CHECK(records[2].opening_name == "c4-c3");
    CHECK(records[3].opening_name == "c4-c3");
}

TEST_CASE("Games can start from an opening and finish legally", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    const auto random = runner::parse_player_spec("random");
    const runner::OpeningParseResult opening = runner::parse_opening_line("c4-c3: c4 c3");
    REQUIRE(first.has_value());
    REQUIRE(random.has_value());
    REQUIRE(opening.ok);
    REQUIRE(opening.has_opening);

    const runner::GameRecord record =
        runner::run_game(0, *first, *random, true, 7, 0, opening.opening);

    CHECK_FALSE(record.illegal_or_error);
    CHECK(record.opening_index == 0);
    CHECK(record.opening_name == "c4-c3");
    REQUIRE(record.opening_moves.size() == 2);
    CHECK(record.opening_moves[0] == "c4");
    CHECK(record.opening_moves[1] == "c3");
    CHECK(record.start_board == othello::to_string(opening.opening.start_board));
    CHECK(record.black_score + record.white_score == 64);
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
