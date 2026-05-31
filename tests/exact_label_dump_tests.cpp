#include "exact_labels/exact_label_dump.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
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
using othello::tools::exact_labels::InputPosition;

namespace {

[[nodiscard]] InputPosition input_position(std::string position_id, const Board& board) {
    return InputPosition{
        .position_id = std::move(position_id),
        .board = board,
        .source_line = 1,
    };
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
    CHECK(error.find("incomplete board block") != std::string::npos);
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

    CHECK(jsonl.find("\"schema\":\"exact_label.v1\"") != std::string::npos);
    CHECK(jsonl.find("\"position_id\":\"pos-000001\"") != std::string::npos);
    CHECK(jsonl.find("\"board\":\"BBBBBBBB\\n") != std::string::npos);
    CHECK(jsonl.find("\"legal_moves\":[\"H1\"]") != std::string::npos);
    CHECK(jsonl.find("\"move_scores\":[{\"move\":\"H1\"") != std::string::npos);
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
    CHECK(output.str().find("\"position_id\":\"pos-000001\"") != std::string::npos);
    CHECK(output.str().find("\"position_id\":\"pos-000002\"") == std::string::npos);
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
    CHECK(output.str().find("\"position_id\":\"pos-000001\"") != std::string::npos);
    CHECK(output.str().find("\"position_id\":\"pos-000002\"") == std::string::npos);
}
