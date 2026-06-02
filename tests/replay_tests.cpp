#include "../tools/replay/replay.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <othello/othello.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace replay = othello::replay;

namespace {

[[nodiscard]] replay::GameRecord record(int game_index, bool black_is_player_a,
                                        std::vector<std::string> moves) {
    return replay::GameRecord{
        .game_index = game_index,
        .opening_index = 0,
        .opening_name = "initial",
        .opening_moves = {},
        .start_board = std::string{othello::test::initial_board_text},
        .black_is_player_a = black_is_player_a,
        .score_diff_from_player_a = 12,
        .moves = std::move(moves),
    };
}

[[nodiscard]] std::string read_fixture_text(std::string_view relative_path) {
    const std::filesystem::path path = std::filesystem::path{OTHELLO_SOURCE_DIR} / relative_path;
    std::ifstream input{path};
    REQUIRE(input);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("Replay applies a normal move sequence with core rules", "[replay]") {
    const std::vector<std::string> moves{"c4", "c3"};

    const replay::ReplayResult replay = replay::replay_moves(othello::Board::initial(), moves);

    REQUIRE(replay.ok);
    CHECK(replay.turns == 2);
    CHECK(replay.passes == 0);
    CHECK(othello::to_string(replay.board) == R"(........
........
........
...BW...
..BWB...
..W.....
........
........
side=B)");
}

TEST_CASE("Replay reports illegal moves without changing rules semantics", "[replay]") {
    const std::vector<std::string> moves{"a1"};

    const replay::ReplayResult replay = replay::replay_moves(othello::Board::initial(), moves);

    CHECK_FALSE(replay.ok);
    CHECK(replay.error.find("illegal move a1") != std::string::npos);
}

TEST_CASE("Replay inserts forced passes before recorded moves", "[replay]") {
    const std::vector<std::string> moves{"c4"};

    const replay::ReplayResult replay =
        replay::replay_moves(othello::test::black_must_pass_board(), moves);

    REQUIRE(replay.ok);
    CHECK(replay.turns == 2);
    CHECK(replay.passes == 1);
    CHECK(replay.board.side_to_move == othello::Side::Black);
}

TEST_CASE("Replay accepts an explicit legal pass token", "[replay]") {
    const std::vector<std::string> moves{"pass"};

    const replay::ReplayResult replay =
        replay::replay_moves(othello::test::black_must_pass_board(), moves);

    REQUIRE(replay.ok);
    CHECK(replay.turns == 1);
    CHECK(replay.passes == 1);
    CHECK(replay.board.side_to_move == othello::Side::White);
}

TEST_CASE("Divergence extraction reports the board before first differing move", "[replay]") {
    auto first = record(0, true, {"d3", "c3"});
    first.score_diff_from_player_a = -12;
    auto second = record(1, false, {"d3", "c4"});
    second.score_diff_from_player_a = 8;
    const std::vector<replay::GameRecord> records{first, second};

    const replay::ExtractDivergencesResult extracted = replay::extract_divergences(records);

    REQUIRE(extracted.ok);
    REQUIRE(extracted.divergences.size() == 1);
    const replay::Divergence& divergence = extracted.divergences.front();
    CHECK(divergence.pair_index == 0);
    CHECK(divergence.ply == 1);
    CHECK(divergence.side_to_move == "white");
    CHECK(divergence.head_game_index == 1);
    CHECK(divergence.base_game_index == 0);
    CHECK(divergence.head_move == "c4");
    CHECK(divergence.base_move == "c3");
    CHECK(divergence.board_text == R"(........
........
........
...BW...
...BB...
...B....
........
........
side=W)");
}

TEST_CASE("Divergence extraction omits identical move sequences", "[replay]") {
    const std::vector<replay::GameRecord> records{
        record(0, true, {"d3", "c3"}),
        record(1, false, {"d3", "c3"}),
    };

    const replay::ExtractDivergencesResult extracted = replay::extract_divergences(records);

    REQUIRE(extracted.ok);
    CHECK(extracted.divergences.empty());
}

TEST_CASE("Match JSONL record parser reads escaped board text and moves", "[replay]") {
    const std::string line =
        R"({"game_index":0,"seed":1,"opening_index":0,"opening_name":"initial","opening_moves":[],"start_board":"........\n........\n........\n...BW...\n...WB...\n........\n........\n........\nside=B","black_is_player_a":true,"score_diff_from_player_a":3,"moves":["d3","c3"],"exact_root_events":[],"illegal_or_error":false})";

    const replay::ParseRecordResult parsed = replay::parse_match_jsonl_record(line);

    REQUIRE(parsed.ok);
    CHECK(parsed.record.game_index == 0);
    CHECK(parsed.record.start_board == std::string{othello::test::initial_board_text});
    CHECK(parsed.record.moves == std::vector<std::string>{"d3", "c3"});
}

TEST_CASE("Corner tactical fixture legality is checked through C++ rules", "[replay]") {
    const std::string text =
        read_fixture_text("data/positions/tactical/corner/pr115_immediate_corner.txt");
    const othello::Board board = othello::test::board_from_text(text);

    CHECK(board.side_to_move == othello::Side::White);
    CHECK((othello::legal_moves(board) & othello::test::bit("a1")) != 0);
}
