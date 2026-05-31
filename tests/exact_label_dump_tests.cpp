#include "exact_labels/exact_label_dump.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <optional>
#include <othello/othello.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using othello::Board;
using othello::Side;
using othello::tools::exact_labels::DumpOptions;
using othello::tools::exact_labels::DumpSummary;
using othello::tools::exact_labels::ExactLabel;
using othello::tools::exact_labels::InputPosition;
using othello::tools::exact_labels::MoveScoreLabel;

namespace {

[[nodiscard]] InputPosition input_position(std::string position_id, const Board& board) {
    return InputPosition{
        .position_id = std::move(position_id),
        .board = board,
        .source_line = 1,
    };
}

[[nodiscard]] std::filesystem::path position_fixture_path(std::string_view file_name) {
    return std::filesystem::path{OTHELLO_SOURCE_DIR} / "data" / "positions" / file_name;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    REQUIRE(input.is_open());

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

[[nodiscard]] Board forced_one_empty_board() {
    return othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)");
}

[[nodiscard]] Board root_pass_board() {
    return othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBWB.
side=B)");
}

[[nodiscard]] Board terminal_white_to_move_board() {
    return Board{
        .black = ~othello::Bitboard{0},
        .white = 0,
        .side_to_move = Side::White,
    };
}

} // namespace

TEST_CASE("Exact label parser reads board blocks with comments and blanks", "[exact-labels]") {
    const std::string text = std::string{"# terminal fixture\n"} +
                             othello::to_string(terminal_white_to_move_board()) +
                             "\n\n# one-empty fixture\n" +
                             othello::to_string(forced_one_empty_board()) + "\n";
    std::string error;

    const auto positions = othello::tools::exact_labels::parse_position_text(text, error);

    REQUIRE(positions.has_value());
    CHECK(positions->size() == 2);
    CHECK((*positions)[0].position_id == "pos-000001");
    CHECK((*positions)[1].position_id == "pos-000002");
}

TEST_CASE("Exact label parser rejects incomplete board blocks", "[exact-labels]") {
    std::string error;

    const auto positions = othello::tools::exact_labels::parse_position_text("BBBBBBBB\n", error);

    CHECK_FALSE(positions.has_value());
    CHECK(error.contains("incomplete board block"));
}

TEST_CASE("Committed exact label smoke fixture parses and writes labels", "[exact-labels]") {
    std::string error;
    const auto positions = othello::tools::exact_labels::parse_position_text(
        read_text_file(position_fixture_path("exact_label_smoke.txt")), error);
    REQUIRE(positions.has_value());
    REQUIRE(positions->size() == 3);

    std::ostringstream output;
    DumpSummary summary;
    const bool ok = othello::tools::exact_labels::write_exact_label_jsonl(
        *positions,
        DumpOptions{
            .limit = std::nullopt,
            .max_empties = 14,
            .include_move_scores = true,
        },
        output, summary, error);

    REQUIRE(ok);
    CHECK(summary.input_positions == 3);
    CHECK(summary.labeled == 3);
    CHECK(summary.skipped_too_many_empties == 0);
    const std::string jsonl = output.str();
    CHECK(jsonl.contains("\"schema\":\"exact_label.v1\""));
    CHECK(jsonl.contains("\"position_id\":\"pos-000001\""));
    CHECK(jsonl.contains("\"position_id\":\"pos-000002\""));
    CHECK(jsonl.contains("\"position_id\":\"pos-000003\""));
}

TEST_CASE("Committed exact label invalid fixture is rejected", "[exact-labels]") {
    std::string error;

    const auto positions = othello::tools::exact_labels::parse_position_text(
        read_text_file(position_fixture_path("exact_label_invalid.txt")), error);

    CHECK_FALSE(positions.has_value());
    CHECK(error.contains("incomplete board block"));
}

TEST_CASE("Exact label uses side-to-move score perspective", "[exact-labels]") {
    const auto label = othello::tools::exact_labels::make_exact_label(
        input_position("pos-000001", forced_one_empty_board()), true);

    CHECK(label.schema == "exact_label.v1");
    CHECK(label.side_to_move == "B");
    CHECK(label.occupied == 63);
    CHECK(label.empties == 1);
    CHECK(label.exact_score_side_to_move == 64);
    REQUIRE(label.best_move.has_value());
    CHECK(*label.best_move == "H1");
    CHECK(label.best_moves == std::vector<std::string>{"H1"});
    REQUIRE(label.move_scores.size() == 1);
    CHECK(label.move_scores.front().move == "H1");
    CHECK(label.move_scores.front().exact_score_side_to_move == 64);
    CHECK(label.nodes > 0);
}

TEST_CASE("Exact label supports terminal positions", "[exact-labels]") {
    const auto label = othello::tools::exact_labels::make_exact_label(
        input_position("pos-000001", terminal_white_to_move_board()), true);

    CHECK(label.side_to_move == "W");
    CHECK(label.occupied == 64);
    CHECK(label.empties == 0);
    CHECK(label.legal_moves.empty());
    CHECK(label.exact_score_side_to_move == -64);
    CHECK(label.best_moves.empty());
    CHECK_FALSE(label.best_move.has_value());
    CHECK(label.move_scores.empty());
}

TEST_CASE("Exact label represents root pass explicitly", "[exact-labels]") {
    const auto label = othello::tools::exact_labels::make_exact_label(
        input_position("pos-000001", root_pass_board()), true);

    CHECK(label.legal_moves == std::vector<std::string>{"PASS"});
    CHECK(label.best_moves == std::vector<std::string>{"PASS"});
    REQUIRE(label.best_move.has_value());
    CHECK(*label.best_move == "PASS");
    CHECK(label.exact_score_side_to_move == 58);
    REQUIRE(label.move_scores.size() == 1);
    CHECK(label.move_scores.front().move == "PASS");
    CHECK(label.move_scores.front().exact_score_side_to_move == 58);
}

TEST_CASE("Exact label JSONL writer emits valid schema fields", "[exact-labels]") {
    const auto label = othello::tools::exact_labels::make_exact_label(
        input_position("pos-000001", forced_one_empty_board()), true);
    std::ostringstream output;

    othello::tools::exact_labels::write_jsonl_record(output, label);
    const std::string jsonl = output.str();

    CHECK(jsonl.contains("\"schema\":\"exact_label.v1\""));
    CHECK(jsonl.contains("\"position_id\":\"pos-000001\""));
    CHECK(jsonl.contains("\"board\":\"BBBBBBBB\\n"));
    CHECK(jsonl.contains("\"legal_moves\":[\"H1\"]"));
    CHECK(jsonl.contains("\"move_scores\":[{\"move\":\"H1\""));
}

TEST_CASE("Exact label JSONL writer preserves field order and value formatting",
          "[exact-labels]") {
    const ExactLabel label{
        .schema = "exact_label.v1",
        .position_id = "pos-test",
        .board_text = "BBBBBBBB\nside=B",
        .side_to_move = "B",
        .occupied = 8,
        .empties = 56,
        .legal_moves = {"A1", "PASS"},
        .exact_score_side_to_move = 12,
        .best_moves = {"A1"},
        .best_move = std::nullopt,
        .move_scores = {MoveScoreLabel{.move = "A1", .exact_score_side_to_move = 12}},
        .include_move_scores = true,
        .elapsed_ms = 1.25,
        .nodes = 42,
    };
    std::ostringstream output;

    othello::tools::exact_labels::write_jsonl_record(output, label);

    CHECK(output.str() ==
          "{\"schema\":\"exact_label.v1\",\"position_id\":\"pos-test\","
          "\"board\":\"BBBBBBBB\\nside=B\",\"side_to_move\":\"B\","
          "\"occupied\":8,\"empties\":56,\"legal_moves\":[\"A1\",\"PASS\"],"
          "\"exact_score_side_to_move\":12,\"best_moves\":[\"A1\"],"
          "\"best_move\":null,\"move_scores\":[{\"move\":\"A1\","
          "\"exact_score_side_to_move\":12}],\"elapsed_ms\":1.250,\"nodes\":42}\n");
}

TEST_CASE("Exact label dump limit and max empties keep runs bounded", "[exact-labels]") {
    const std::vector<InputPosition> positions{
        input_position("pos-000001", terminal_white_to_move_board()),
        input_position("pos-000002", forced_one_empty_board()),
    };
    std::ostringstream output;
    DumpSummary summary;
    std::string error;

    const bool ok = othello::tools::exact_labels::write_exact_label_jsonl(
        positions,
        DumpOptions{
            .limit = std::nullopt,
            .max_empties = 0,
            .include_move_scores = true,
        },
        output, summary, error);

    REQUIRE(ok);
    CHECK(summary.input_positions == 2);
    CHECK(summary.labeled == 1);
    CHECK(summary.skipped_too_many_empties == 1);
    const std::string jsonl = output.str();
    CHECK(jsonl.contains("\"position_id\":\"pos-000001\""));
    CHECK_FALSE(jsonl.contains("\"position_id\":\"pos-000002\""));
}

TEST_CASE("Exact label dump limit stops after requested labels", "[exact-labels]") {
    const std::vector<InputPosition> positions{
        input_position("pos-000001", terminal_white_to_move_board()),
        input_position("pos-000002", forced_one_empty_board()),
    };
    std::ostringstream output;
    DumpSummary summary;
    std::string error;

    const bool ok = othello::tools::exact_labels::write_exact_label_jsonl(
        positions,
        DumpOptions{
            .limit = 1,
            .max_empties = 64,
            .include_move_scores = false,
        },
        output, summary, error);

    REQUIRE(ok);
    CHECK(summary.input_positions == 2);
    CHECK(summary.labeled == 1);
    CHECK(summary.skipped_too_many_empties == 0);
    const std::string jsonl = output.str();
    CHECK(jsonl.contains("\"position_id\":\"pos-000001\""));
    CHECK_FALSE(jsonl.contains("\"position_id\":\"pos-000002\""));
}
