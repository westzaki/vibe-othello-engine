#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

#include <optional>
#include <string>

using othello::Bitboard;
using othello::Board;
using othello::Side;

namespace {

[[nodiscard]] Board midgame_board() {
    return othello::test::board_from_text(R"(........
.B......
..B.B...
...BB...
.WWWWW..
..B.....
........
........
side=W)");
}

[[nodiscard]] Board corner_occupancy_board() {
    return Board{
        .black = Board::initial().black | othello::test::bit("a1"),
        .white = Board::initial().white | othello::test::bit("h8"),
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] Board corner_access_board() {
    return othello::test::board_from_text(R"(........
........
........
...BW...
...WB...
........
........
.WB.....
side=B)");
}

[[nodiscard]] Board x_square_danger_board() {
    return Board{
        .black = Board::initial().black | othello::test::bit("b2"),
        .white = Board::initial().white,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] Board frontier_sensitive_board() {
    return othello::test::board_from_text(R"(........
........
..WWW...
..WBW...
..WWW...
........
........
........
side=B)");
}

[[nodiscard]] Board swapped_colors(const Board& board) noexcept {
    return Board{
        .black = board.white,
        .white = board.black,
        .side_to_move = othello::opponent(board.side_to_move),
    };
}

void check_breakdown_matches_basic(const Board& board, Side side) {
    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, side);

    CHECK(breakdown.total == othello::evaluate_basic(board, side));
    CHECK(breakdown.total ==
          othello::evaluate_with_config(board, side, othello::default_evaluation_config()));
}

[[nodiscard]] int non_terminal_score_sum(const othello::EvaluationBreakdown& breakdown) noexcept {
    return breakdown.disc_difference_score + breakdown.mobility_score +
           breakdown.corner_occupancy_score + breakdown.potential_mobility_score +
           breakdown.corner_access_score + breakdown.x_square_danger_score +
           breakdown.frontier_score;
}

void check_non_terminal_breakdown_math(const Board& board, Side side) {
    REQUIRE_FALSE(othello::is_game_over(board));

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, side);

    CHECK_FALSE(breakdown.terminal);
    CHECK(breakdown.disc_difference_score ==
          breakdown.disc_difference * breakdown.disc_difference_weight);
    CHECK(breakdown.mobility_score == breakdown.mobility * breakdown.mobility_weight);
    CHECK(breakdown.corner_occupancy_score ==
          breakdown.corner_occupancy * breakdown.corner_occupancy_weight);
    CHECK(breakdown.potential_mobility_score ==
          breakdown.potential_mobility * breakdown.potential_mobility_weight);
    CHECK(breakdown.corner_access_score ==
          breakdown.corner_access * breakdown.corner_access_weight);
    CHECK(breakdown.x_square_danger_score ==
          breakdown.x_square_danger * breakdown.x_square_danger_weight);
    CHECK(breakdown.frontier_score == breakdown.frontier * breakdown.frontier_weight);
    CHECK(breakdown.terminal_disc_difference == 0);
    CHECK(breakdown.terminal_score == 0);
    CHECK(breakdown.total == non_terminal_score_sum(breakdown));
}

} // namespace

TEST_CASE("Initial board evaluation is symmetric", "[evaluation]") {
    const Board board = Board::initial();

    CHECK(othello::evaluate_disc_difference(board, Side::Black) == 0);
    CHECK(othello::evaluate_disc_difference(board, Side::White) == 0);
    CHECK(othello::evaluate_mobility(board, Side::Black) == 0);
    CHECK(othello::evaluate_mobility(board, Side::White) == 0);
    CHECK(othello::evaluate_basic(board, Side::Black) ==
          -othello::evaluate_basic(board, Side::White));
}

TEST_CASE("Default evaluation config preserves phase-aware weights", "[evaluation]") {
    const othello::EvaluationConfig config = othello::default_evaluation_config();

    CHECK(config.opening == othello::EvaluationFeatureWeights{.disc_difference = 0,
                                                              .mobility = 8,
                                                              .potential_mobility = 4,
                                                              .corner_occupancy = 35,
                                                              .corner_access = 30,
                                                              .x_square_danger = 25,
                                                              .frontier = 3});
    CHECK(config.midgame == othello::EvaluationFeatureWeights{.disc_difference = 1,
                                                              .mobility = 10,
                                                              .potential_mobility = 5,
                                                              .corner_occupancy = 40,
                                                              .corner_access = 35,
                                                              .x_square_danger = 30,
                                                              .frontier = 4});
    CHECK(config.late == othello::EvaluationFeatureWeights{.disc_difference = 4,
                                                           .mobility = 6,
                                                           .potential_mobility = 2,
                                                           .corner_occupancy = 45,
                                                           .corner_access = 20,
                                                           .x_square_danger = 20,
                                                           .frontier = 2});
    CHECK(config.opening_max_occupied == 20);
    CHECK(config.midgame_max_occupied == 44);
    CHECK(othello::evaluation_config_for_preset(othello::EvaluationPreset::Default) == config);
    CHECK(std::string{othello::evaluation_preset_name(othello::EvaluationPreset::Default)} ==
          "default");
    const std::optional<othello::EvaluationPreset> parsed_default =
        othello::evaluation_preset_from_name("default");
    REQUIRE(parsed_default.has_value());
    CHECK(*parsed_default == othello::EvaluationPreset::Default);
}

TEST_CASE("Smoke evaluation preset is explicit and lightweight", "[evaluation]") {
    const othello::EvaluationConfig default_config = othello::default_evaluation_config();
    const othello::EvaluationConfig smoke_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::MobilityPlusSmoke);

    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::MobilityPlusSmoke)} == "mobility_plus_smoke");
    const std::optional<othello::EvaluationPreset> parsed_smoke =
        othello::evaluation_preset_from_name("mobility_plus_smoke");
    REQUIRE(parsed_smoke.has_value());
    CHECK(*parsed_smoke == othello::EvaluationPreset::MobilityPlusSmoke);
    CHECK_FALSE(othello::evaluation_preset_from_name("unknown").has_value());
    CHECK(smoke_config.opening.mobility == default_config.opening.mobility + 2);
    CHECK(smoke_config.midgame.mobility == default_config.midgame.mobility + 2);
    CHECK(smoke_config.late.mobility == default_config.late.mobility + 2);
    CHECK(smoke_config.opening.potential_mobility ==
          default_config.opening.potential_mobility);
}

TEST_CASE("Corner ownership improves the owning side evaluation", "[evaluation]") {
    const Board board{
        .black = Board::initial().black | othello::test::bit("a1"),
        .white = Board::initial().white,
        .side_to_move = Side::Black,
    };

    CHECK(othello::evaluate_basic(board, Side::Black) > 0);
    CHECK(othello::evaluate_basic(board, Side::White) < 0);
    CHECK(othello::evaluate_basic(board, Side::Black) >
          othello::evaluate_basic(board, Side::White));
}

TEST_CASE("Evaluation breakdown total matches basic evaluator", "[evaluation]") {
    const Board terminal{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    for (const Board& board :
         {Board::initial(), midgame_board(), corner_occupancy_board(), corner_access_board(),
          x_square_danger_board(), frontier_sensitive_board(), terminal}) {
        check_breakdown_matches_basic(board, Side::Black);
        check_breakdown_matches_basic(board, Side::White);
    }
}

TEST_CASE("Default config matches compatibility evaluator", "[evaluation]") {
    const Board terminal{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    for (const Board& board :
         {Board::initial(), midgame_board(), corner_occupancy_board(), corner_access_board(),
          x_square_danger_board(), frontier_sensitive_board(), terminal}) {
        for (const Side side : {Side::Black, Side::White}) {
            CAPTURE(othello::to_string(board));
            CHECK(othello::evaluate_with_config(board, side,
                                                othello::default_evaluation_config()) ==
                  othello::evaluate_basic(board, side));
            const othello::EvaluationBreakdown configured = othello::evaluate_basic_breakdown(
                board, side, othello::default_evaluation_config());
            const othello::EvaluationBreakdown compatibility =
                othello::evaluate_basic_breakdown(board, side);
            CHECK(configured.phase == compatibility.phase);
            CHECK(configured.disc_difference_weight == compatibility.disc_difference_weight);
            CHECK(configured.mobility_weight == compatibility.mobility_weight);
            CHECK(configured.corner_occupancy_weight == compatibility.corner_occupancy_weight);
            CHECK(configured.potential_mobility_weight ==
                  compatibility.potential_mobility_weight);
            CHECK(configured.corner_access_weight == compatibility.corner_access_weight);
            CHECK(configured.x_square_danger_weight == compatibility.x_square_danger_weight);
            CHECK(configured.frontier_weight == compatibility.frontier_weight);
            CHECK(configured.total == compatibility.total);
        }
    }
}

TEST_CASE("Non-terminal evaluation breakdown explains component math", "[evaluation]") {
    for (const Board& board : {Board::initial(), midgame_board(), corner_occupancy_board(),
                               corner_access_board(), x_square_danger_board(),
                               frontier_sensitive_board()}) {
        check_non_terminal_breakdown_math(board, Side::Black);
        check_non_terminal_breakdown_math(board, Side::White);
    }
}

TEST_CASE("Evaluation breakdown reports phase and board fill", "[evaluation]") {
    const Board initial = Board::initial();
    const Board midgame = midgame_board();
    const Board late = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
WWWWWWWW
WWWWWWWW
WWWWWW..
........
side=B)");

    const othello::EvaluationBreakdown initial_breakdown =
        othello::evaluate_basic_breakdown(initial, Side::Black);
    const othello::EvaluationBreakdown midgame_breakdown =
        othello::evaluate_basic_breakdown(midgame, Side::Black);
    const othello::EvaluationBreakdown late_breakdown =
        othello::evaluate_basic_breakdown(late, Side::Black);

    CHECK(initial_breakdown.phase == othello::EvaluationPhase::Opening);
    CHECK(initial_breakdown.occupied_count == 4);
    CHECK(initial_breakdown.empty_count == 60);
    CHECK(initial_breakdown.disc_difference_weight == 0);

    CHECK(midgame_breakdown.phase == othello::EvaluationPhase::Opening);
    CHECK(midgame_breakdown.occupied_count == 11);

    CHECK(late_breakdown.phase == othello::EvaluationPhase::Late);
    CHECK(late_breakdown.occupied_count == 54);
    CHECK(late_breakdown.empty_count == 10);
    CHECK(late_breakdown.disc_difference_weight == 4);
}

TEST_CASE("Corner occupancy appears in evaluation breakdown", "[evaluation]") {
    const Board board{
        .black = Board::initial().black | othello::test::bit("a1"),
        .white = Board::initial().white,
        .side_to_move = Side::Black,
    };

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    REQUIRE_FALSE(black.terminal);
    CHECK(black.corner_occupancy == 1);
    CHECK(black.corner_occupancy_weight == 35);
    CHECK(black.corner_occupancy_score == 35);
    CHECK(white.corner_occupancy == -1);
    CHECK(white.corner_occupancy_score == -35);
}

TEST_CASE("Legal corner access is positive for the side with a corner move", "[evaluation]") {
    const Board board = corner_access_board();

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK((othello::legal_moves(board) & othello::test::bit("a1")) != 0);
    CHECK(black.corner_access > 0);
    CHECK(black.corner_access_score > 0);
    CHECK(white.corner_access < 0);
    CHECK(white.corner_access_score < 0);
}

TEST_CASE("X-square next to an empty corner is dangerous", "[evaluation]") {
    const Board board = x_square_danger_board();

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK(black.x_square_danger == -1);
    CHECK(black.x_square_danger_score < 0);
    CHECK(white.x_square_danger == 1);
    CHECK(white.x_square_danger_score > 0);
}

TEST_CASE("Lower own frontier is better", "[evaluation]") {
    const Board board = frontier_sensitive_board();

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK(black.frontier > 0);
    CHECK(black.frontier_score > 0);
    CHECK(white.frontier < 0);
    CHECK(white.frontier_score < 0);
}

TEST_CASE("Evaluation is symmetric under color swap", "[evaluation]") {
    const Board board = midgame_board();
    const Board swapped = swapped_colors(board);

    CHECK(othello::evaluate_basic(board, Side::Black) ==
          -othello::evaluate_basic(swapped, Side::Black));
    CHECK(othello::evaluate_basic(board, Side::White) ==
          -othello::evaluate_basic(swapped, Side::White));
}

TEST_CASE("Terminal board evaluation is strongly scaled", "[evaluation]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    CHECK(othello::evaluate_basic(board, Side::Black) > 10'000);
    CHECK(othello::evaluate_basic(board, Side::White) < -10'000);
}

TEST_CASE("Terminal evaluation breakdown uses terminal score only", "[evaluation]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK(black.terminal);
    CHECK(black.terminal_disc_difference == othello::score(board, Side::Black));
    CHECK(black.terminal_score_weight == 1000);
    CHECK(black.terminal_score == black.terminal_disc_difference * black.terminal_score_weight);
    CHECK(black.total == black.terminal_score);
    CHECK(black.disc_difference_score == 0);
    CHECK(black.mobility_score == 0);
    CHECK(black.corner_occupancy_score == 0);
    CHECK(black.potential_mobility_score == 0);
    CHECK(black.corner_access_score == 0);
    CHECK(black.x_square_danger_score == 0);
    CHECK(black.frontier_score == 0);

    CHECK(white.terminal);
    CHECK(white.terminal_disc_difference == othello::score(board, Side::White));
    CHECK(white.terminal_score == white.terminal_disc_difference * white.terminal_score_weight);
    CHECK(white.total == white.terminal_score);
}

TEST_CASE("Terminal evaluation keeps fixed score scale across configs", "[evaluation]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };
    othello::EvaluationConfig config = othello::default_evaluation_config();
    config.opening.disc_difference = 99;
    config.midgame.mobility = 99;
    config.late.corner_occupancy = 99;

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, config);

    CHECK(breakdown.terminal);
    CHECK(breakdown.terminal_score_weight == 1000);
    CHECK(breakdown.total == othello::score(board, Side::Black) * 1000);
}

TEST_CASE("Evaluation does not mutate the board", "[evaluation]") {
    const Board board = Board::initial();
    const Board before = board;

    static_cast<void>(othello::evaluate_disc_difference(board, Side::Black));
    static_cast<void>(othello::evaluate_mobility(board, Side::Black));
    static_cast<void>(othello::evaluate_basic_breakdown(board, Side::Black));
    static_cast<void>(othello::evaluate_basic_breakdown(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::MobilityPlusSmoke)));
    static_cast<void>(othello::evaluate_with_config(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::MobilityPlusSmoke)));
    static_cast<void>(othello::evaluate_basic(board, Side::Black));

    CHECK(othello::test::same_board(board, before));
}
