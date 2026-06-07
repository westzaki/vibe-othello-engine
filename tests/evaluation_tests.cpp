#include "../src/evaluation_internal.hpp"
#include "common/eval_config_io.hpp"
#include "common/evaluator_selection.hpp"
#include "evaluation_test_helpers.hpp"
#include "positions/metrics.hpp"
#include "positions/search_positions.hpp"
#include "positions/tags.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/evaluation_feature_specs.hpp>
#include <othello/othello.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

using othello::Bitboard;
using othello::Board;
using othello::Side;
using namespace othello::test::evaluation;

namespace {

struct GoldenEvaluationFixture {
    std::string_view name;
    Board board;
    Side side = Side::Black;
    othello::EvaluationPhase phase = othello::EvaluationPhase::Opening;
    int occupied_count = 0;
    int empty_count = 64;
    bool terminal = false;
    int total = 0;
    int disc_difference = 0;
    int mobility = 0;
    int corner_occupancy = 0;
    int potential_mobility = 0;
    int corner_access = 0;
    int x_square_danger = 0;
    int frontier = 0;
    int corner_local_2x3 = 0;
    int corner_2x3_pattern = 0;
    int edge_stability_lite = 0;
    int edge_8_pattern = 0;
    int pattern_table = 0;
    int terminal_disc_difference = 0;
};

void check_golden_evaluation(const GoldenEvaluationFixture& fixture) {
    CAPTURE(std::string{fixture.name});

    const othello::EvaluationConfig config = othello::default_evaluation_config();
    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(fixture.board, fixture.side, config);

    CHECK(othello::evaluate_basic(fixture.board, fixture.side) == fixture.total);
    CHECK(othello::evaluate_with_config(fixture.board, fixture.side, config) ==
          fixture.total);
    CHECK(breakdown.total == fixture.total);
    CHECK(breakdown.phase == fixture.phase);
    CHECK(breakdown.occupied_count == fixture.occupied_count);
    CHECK(breakdown.empty_count == fixture.empty_count);
    CHECK(breakdown.terminal == fixture.terminal);
    CHECK(breakdown.disc_difference == fixture.disc_difference);
    CHECK(breakdown.mobility == fixture.mobility);
    CHECK(breakdown.corner_occupancy == fixture.corner_occupancy);
    CHECK(breakdown.potential_mobility == fixture.potential_mobility);
    CHECK(breakdown.corner_access == fixture.corner_access);
    CHECK(breakdown.x_square_danger == fixture.x_square_danger);
    CHECK(breakdown.frontier == fixture.frontier);
    CHECK(breakdown.corner_local_2x3 == fixture.corner_local_2x3);
    CHECK(breakdown.corner_2x3_pattern == fixture.corner_2x3_pattern);
    CHECK(breakdown.edge_stability_lite == fixture.edge_stability_lite);
    CHECK(breakdown.edge_8_pattern == fixture.edge_8_pattern);
    CHECK(breakdown.pattern_table == fixture.pattern_table);
    CHECK(breakdown.terminal_disc_difference == fixture.terminal_disc_difference);
}

void check_direct_bitboard_evaluation_matches_board(
    const Board& board, Side side, const othello::EvaluationConfig& config) {
    const Bitboard player = board.discs(side);
    const Bitboard opponent = board.discs(othello::opponent(side));

    const othello::evaluation_detail::EvaluationScratch board_scratch =
        othello::evaluation_detail::make_evaluation_scratch(board, side);
    const othello::evaluation_detail::EvaluationScratch bitboard_scratch =
        othello::evaluation_detail::make_evaluation_scratch(player, opponent);

    CHECK(bitboard_scratch.player == board_scratch.player);
    CHECK(bitboard_scratch.opponent == board_scratch.opponent);
    CHECK(bitboard_scratch.occupied == board_scratch.occupied);
    CHECK(bitboard_scratch.empty == board_scratch.empty);
    CHECK(bitboard_scratch.own_moves == board_scratch.own_moves);
    CHECK(bitboard_scratch.opp_moves == board_scratch.opp_moves);
    CHECK(bitboard_scratch.own_mobility == board_scratch.own_mobility);
    CHECK(bitboard_scratch.opp_mobility == board_scratch.opp_mobility);
    CHECK(bitboard_scratch.occupied_count == board_scratch.occupied_count);
    CHECK(bitboard_scratch.empty_count == board_scratch.empty_count);
    CHECK(bitboard_scratch.game_over == board_scratch.game_over);
    CHECK(othello::evaluation_detail::evaluate_with_config(player, opponent, config) ==
          othello::evaluate_with_config(board, side, config));
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

TEST_CASE("Golden evaluation fixtures preserve scratch refactor behavior",
          "[evaluation]") {
    const std::array fixtures{
        GoldenEvaluationFixture{.name = "initial",
                                .board = Board::initial(),
                                .side = Side::Black,
                                .phase = othello::EvaluationPhase::Opening,
                                .occupied_count = 4,
                                .empty_count = 60},
        GoldenEvaluationFixture{
            .name = "early-asymmetric",
            .board = othello::test::board_from_text(R"(........
........
........
...BW...
...BBB..
........
........
........
side=W)"),
            .side = Side::White,
            .phase = othello::EvaluationPhase::Opening,
            .occupied_count = 5,
            .empty_count = 59,
            .total = 51,
            .disc_difference = -3,
            .potential_mobility = 9,
            .frontier = 3},
        GoldenEvaluationFixture{.name = "midgame",
                                .board = phase_midgame_board(),
                                .side = Side::Black,
                                .phase = othello::EvaluationPhase::Midgame,
                                .occupied_count = 28,
                                .empty_count = 36,
                                .total = 206,
                                .disc_difference = -20,
                                .mobility = 4,
                                .potential_mobility = 14,
                                .frontier = 15,
                                .corner_2x3_pattern = 1,
                                .edge_8_pattern = 5},
        GoldenEvaluationFixture{.name = "late",
                                .board = phase_late_board(),
                                .side = Side::Black,
                                .phase = othello::EvaluationPhase::Late,
                                .occupied_count = 54,
                                .empty_count = 10,
                                .total = -132,
                                .disc_difference = -18,
                                .mobility = 4,
                                .potential_mobility = 1,
                                .x_square_danger = 1,
                                .frontier = 2,
                                .corner_2x3_pattern = -1,
                                .edge_stability_lite = -9,
                                .edge_8_pattern = -6},
        GoldenEvaluationFixture{.name = "pass-available",
                                .board = othello::test::black_must_pass_board(),
                                .side = Side::Black,
                                .phase = othello::EvaluationPhase::Opening,
                                .occupied_count = 5,
                                .empty_count = 59,
                                .total = 21,
                                .disc_difference = -3,
                                .mobility = -1,
                                .potential_mobility = 3,
                                .frontier = 3,
                                .edge_8_pattern = 1},
        GoldenEvaluationFixture{.name = "gameover",
                                .board = terminal_black_win_board(),
                                .side = Side::Black,
                                .phase = othello::EvaluationPhase::Late,
                                .occupied_count = 64,
                                .empty_count = 0,
                                .terminal = true,
                                .total = 64'000,
                                .terminal_disc_difference = 64},
    };

    for (const GoldenEvaluationFixture& fixture : fixtures) {
        check_golden_evaluation(fixture);
    }
}

TEST_CASE("Default evaluation config promotes frontier corner and edge pattern weights",
          "[evaluation]") {
    const othello::EvaluationConfig config = othello::default_evaluation_config();

    CHECK(config.opening == othello::EvaluationFeatureWeights{.disc_difference = 0,
                                                              .mobility = 8,
                                                              .potential_mobility = 4,
                                                              .corner_occupancy = 35,
                                                              .corner_access = 30,
                                                              .x_square_danger = 25,
                                                              .frontier = 5,
                                                              .corner_local_2x3 = 0,
                                                              .corner_2x3_pattern = 4,
                                                              .edge_stability_lite = 2,
                                                              .edge_8_pattern = 2});
    CHECK(config.midgame == othello::EvaluationFeatureWeights{.disc_difference = 1,
                                                              .mobility = 10,
                                                              .potential_mobility = 5,
                                                              .corner_occupancy = 40,
                                                              .corner_access = 35,
                                                              .x_square_danger = 30,
                                                              .frontier = 6,
                                                              .corner_local_2x3 = 0,
                                                              .corner_2x3_pattern = 6,
                                                              .edge_stability_lite = 4,
                                                              .edge_8_pattern = 4});
    CHECK(config.late == othello::EvaluationFeatureWeights{.disc_difference = 4,
                                                           .mobility = 6,
                                                           .potential_mobility = 2,
                                                           .corner_occupancy = 45,
                                                           .corner_access = 20,
                                                           .x_square_danger = 20,
                                                           .frontier = 3,
                                                           .corner_local_2x3 = 0,
                                                           .corner_2x3_pattern = 4,
                                                           .edge_stability_lite = 8,
                                                           .edge_8_pattern = 6});
    CHECK(config.opening_max_occupied == 20);
    CHECK(config.midgame_max_occupied == 44);
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
    const Board terminal = terminal_black_win_board();

    for (const Board& board :
         {Board::initial(), midgame_board(), corner_occupancy_board(), corner_access_board(),
          x_square_danger_board(), frontier_sensitive_board(), terminal}) {
        check_breakdown_matches_basic(board, Side::Black);
        check_breakdown_matches_basic(board, Side::White);
    }
}

TEST_CASE("Score-only evaluator matches breakdown totals across configs and phases",
          "[evaluation]") {
    struct BoardFixture {
        std::string_view name;
        Board board;
        othello::EvaluationPhase expected_phase;
        bool terminal = false;
    };

    const othello::tools::EvaluationConfigLoadResult pattern_only =
        othello::tools::load_evaluation_config_file(
            test_eval_config_fixture_path("minimal_pattern_only_eval.eval"));
    REQUIRE(pattern_only.ok());
    REQUIRE(pattern_only.config.pattern_tables != nullptr);

    const std::vector<std::pair<std::string, othello::EvaluationConfig>> configs{
        {std::string{"default"}, othello::default_evaluation_config()},
        {std::string{"sparse_scalar"}, sparse_scalar_config()},
        {std::string{"all_feature_guard"}, all_feature_guard_config()},
        {std::string{"minimal_pattern_only_fixture"}, pattern_only.config},
    };
    const std::array boards{
        BoardFixture{.name = "opening",
                     .board = Board::initial(),
                     .expected_phase = othello::EvaluationPhase::Opening},
        BoardFixture{.name = "midgame",
                     .board = phase_midgame_board(),
                     .expected_phase = othello::EvaluationPhase::Midgame},
        BoardFixture{.name = "late",
                     .board = phase_late_board(),
                     .expected_phase = othello::EvaluationPhase::Late},
        BoardFixture{.name = "terminal",
                     .board = terminal_black_win_board(),
                     .expected_phase = othello::EvaluationPhase::Late,
                     .terminal = true},
    };

    for (const auto& [config_name, config] : configs) {
        for (const BoardFixture& fixture : boards) {
            for (const Side side : {Side::Black, Side::White}) {
                CAPTURE(config_name);
                CAPTURE(std::string{fixture.name});
                CAPTURE(side == Side::Black ? "black" : "white");

                const othello::EvaluationBreakdown breakdown =
                    othello::evaluate_basic_breakdown(fixture.board, side, config);
                CHECK(breakdown.phase == fixture.expected_phase);
                CHECK(breakdown.terminal == fixture.terminal);
                CHECK(othello::evaluate_with_config(fixture.board, side, config) ==
                      breakdown.total);
            }
        }
    }
}

TEST_CASE("Evaluation feature specs map weights raw values and weighted scores",
          "[evaluation]") {
    const othello::EvaluationConfig config = all_feature_guard_config();
    const Board board = phase_midgame_board();
    const othello::EvaluationFeatureWeights& weights = config.midgame;

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, config);

    REQUIRE_FALSE(breakdown.terminal);
    REQUIRE(breakdown.phase == othello::EvaluationPhase::Midgame);

    int total = 0;
    for (const othello::evaluation_detail::EvaluationFeatureSpec& spec :
         othello::evaluation_detail::evaluation_feature_specs) {
        CAPTURE(std::string{spec.key});
        CHECK(breakdown.*spec.breakdown_weight == weights.*spec.weight);
        CHECK(breakdown.*spec.weighted_score ==
              (breakdown.*spec.raw_value) * (breakdown.*spec.breakdown_weight));
        total += breakdown.*spec.weighted_score;
    }
    CHECK(breakdown.total == total);
    CHECK(breakdown.total == othello::evaluate_with_config(board, Side::Black, config));
}

TEST_CASE("Direct bitboard evaluator matches Board evaluator on fixture positions",
          "[evaluation]") {
    struct BoardFixture {
        std::string_view name;
        Board board;
    };

    const othello::tools::EvaluationConfigLoadResult pattern_only =
        othello::tools::load_evaluation_config_file(
            test_eval_config_fixture_path("minimal_pattern_only_eval.eval"));
    REQUIRE(pattern_only.ok());
    REQUIRE(pattern_only.config.pattern_tables != nullptr);

    const std::vector<std::pair<std::string, othello::EvaluationConfig>> configs{
        {std::string{"default"}, othello::default_evaluation_config()},
        {std::string{"sparse_scalar"}, sparse_scalar_config()},
        {std::string{"all_feature_guard"}, all_feature_guard_config()},
        {std::string{"minimal_pattern_only_fixture"}, pattern_only.config},
    };
    const std::array boards{
        BoardFixture{.name = "opening", .board = Board::initial()},
        BoardFixture{.name = "midgame", .board = phase_midgame_board()},
        BoardFixture{.name = "late", .board = phase_late_board()},
        BoardFixture{.name = "root-pass", .board = othello::test::black_must_pass_board()},
        BoardFixture{.name = "terminal", .board = terminal_black_win_board()},
    };

    for (const auto& [config_name, config] : configs) {
        for (const BoardFixture& fixture : boards) {
            for (const Side side : {Side::Black, Side::White}) {
                CAPTURE(config_name);
                CAPTURE(std::string{fixture.name});
                CAPTURE(side == Side::Black ? "black" : "white");

                check_direct_bitboard_evaluation_matches_board(fixture.board, side, config);
            }
        }
    }
}

TEST_CASE("Default config matches basic evaluator overloads", "[evaluation]") {
    const Board terminal = terminal_black_win_board();

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
            const othello::EvaluationBreakdown basic =
                othello::evaluate_basic_breakdown(board, side);
            CHECK(configured.phase == basic.phase);
            CHECK(configured.disc_difference_weight == basic.disc_difference_weight);
            CHECK(configured.mobility_weight == basic.mobility_weight);
            CHECK(configured.corner_occupancy_weight == basic.corner_occupancy_weight);
            CHECK(configured.potential_mobility_weight == basic.potential_mobility_weight);
            CHECK(configured.corner_access_weight == basic.corner_access_weight);
            CHECK(configured.x_square_danger_weight == basic.x_square_danger_weight);
            CHECK(configured.frontier_weight == basic.frontier_weight);
            CHECK(configured.corner_local_2x3_weight == basic.corner_local_2x3_weight);
            CHECK(configured.corner_2x3_pattern_weight == basic.corner_2x3_pattern_weight);
            CHECK(configured.edge_stability_lite_weight == basic.edge_stability_lite_weight);
            CHECK(configured.edge_8_pattern_weight == basic.edge_8_pattern_weight);
            CHECK(configured.total == basic.total);
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

TEST_CASE("Corner-local 2x3 lite penalizes empty-corner X and C squares", "[evaluation]") {
    const othello::EvaluationConfig config = corner_local_only_config(10);
    const Board own_x = extra_disc_board(othello::test::bit("b2"));
    const Board opponent_x = extra_disc_board(0, othello::test::bit("b2"));
    const Board own_c = extra_disc_board(othello::test::bit("b1"));

    const othello::EvaluationBreakdown own_x_breakdown =
        othello::evaluate_basic_breakdown(own_x, Side::Black, config);
    const othello::EvaluationBreakdown opponent_x_breakdown =
        othello::evaluate_basic_breakdown(opponent_x, Side::Black, config);
    const othello::EvaluationBreakdown own_c_breakdown =
        othello::evaluate_basic_breakdown(own_c, Side::Black, config);

    CHECK(own_x_breakdown.corner_local_2x3 == -2);
    CHECK(own_x_breakdown.corner_local_2x3_weight == 10);
    CHECK(own_x_breakdown.corner_local_2x3_score == -20);
    CHECK(opponent_x_breakdown.corner_local_2x3 == 2);
    CHECK(opponent_x_breakdown.corner_local_2x3_score == 20);
    CHECK(own_c_breakdown.corner_local_2x3 == -1);
    CHECK(own_c_breakdown.corner_local_2x3_score == -10);
}

TEST_CASE("Corner-local 2x3 lite is symmetric across corners and colors", "[evaluation]") {
    const othello::EvaluationConfig config = corner_local_only_config(10);

    for (const std::string_view square : {"b2", "g2", "b7", "g7"}) {
        const Board board = extra_disc_board(othello::test::bit(square));
        const othello::EvaluationBreakdown black =
            othello::evaluate_basic_breakdown(board, Side::Black, config);
        const othello::EvaluationBreakdown white =
            othello::evaluate_basic_breakdown(board, Side::White, config);
        CHECK(black.corner_local_2x3 == -2);
        CHECK(white.corner_local_2x3 == 2);
    }

    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1"));
    const Board swapped = swapped_colors(board);
    CHECK(othello::evaluate_with_config(board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped, Side::Black, config));
}

TEST_CASE("Corner-local 2x3 lite rewards owned-corner adjacent support", "[evaluation]") {
    const othello::EvaluationConfig config = corner_local_only_config(10);
    const Board owned =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));
    const Board opponent_owned =
        extra_disc_board(0, othello::test::bit("a1") | othello::test::bit("b1") |
                                othello::test::bit("a2"));

    const othello::EvaluationBreakdown owned_breakdown =
        othello::evaluate_basic_breakdown(owned, Side::Black, config);
    const othello::EvaluationBreakdown opponent_owned_breakdown =
        othello::evaluate_basic_breakdown(opponent_owned, Side::Black, config);

    CHECK(owned_breakdown.corner_local_2x3 == 2);
    CHECK(owned_breakdown.corner_local_2x3_score == 20);
    CHECK(opponent_owned_breakdown.corner_local_2x3 == -2);
    CHECK(opponent_owned_breakdown.corner_local_2x3_score == -20);
}

TEST_CASE("Corner 2x3 pattern index is deterministic across symmetric corners",
          "[evaluation]") {
    using Corner = othello::Corner2x3PatternCorner;

    CHECK(othello::corner_2x3_pattern_table_size == 729);
    CHECK(othello::corner_2x3_pattern_table_value(0) == 0);
    CHECK(othello::corner_2x3_pattern_table_value(-1) == 0);
    CHECK(othello::corner_2x3_pattern_table_value(729) == 0);
    CHECK(othello::corner_2x3_pattern_index(Board::initial(), Side::Black, Corner::A1) == 0);

    const Board mixed = extra_disc_board(othello::test::bit("a1") |
                                             othello::test::bit("a2"),
                                         othello::test::bit("b1") |
                                             othello::test::bit("b2"));
    CHECK(othello::corner_2x3_pattern_index(mixed, Side::Black, Corner::A1) == 196);
    CHECK(othello::corner_2x3_pattern_index(mixed, Side::Black, Corner::A1) >= 0);
    CHECK(othello::corner_2x3_pattern_index(mixed, Side::Black, Corner::A1) < 729);

    for (const auto [corner, corner_square, c_square, x_square] : {
             std::tuple{Corner::A1, "a1", "b1", "b2"},
             std::tuple{Corner::H1, "h1", "g1", "g2"},
             std::tuple{Corner::A8, "a8", "b8", "b7"},
             std::tuple{Corner::H8, "h8", "g8", "g7"},
         }) {
        const Board board = extra_disc_board(othello::test::bit(corner_square) |
                                             othello::test::bit(c_square) |
                                             othello::test::bit(x_square));
        CHECK(othello::corner_2x3_pattern_index(board, Side::Black, corner) == 85);
    }
}

TEST_CASE("Corner 2x3 pattern table follows conservative corner-local rules",
          "[evaluation]") {
    const othello::EvaluationConfig config = corner_pattern_only_config(10);
    const Board own_x = extra_disc_board(othello::test::bit("b2"));
    const Board opponent_x = extra_disc_board(0, othello::test::bit("b2"));
    const Board own_c = extra_disc_board(othello::test::bit("b1"));
    const Board opponent_c = extra_disc_board(0, othello::test::bit("b1"));
    const Board owned_support =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));
    const Board opponent_owned_support =
        extra_disc_board(0, othello::test::bit("a1") | othello::test::bit("b1") |
                                othello::test::bit("a2"));

    CHECK(othello::corner_2x3_pattern_score(Board::initial(), Side::Black) == 0);
    CHECK(othello::corner_2x3_pattern_score(own_x, Side::Black) == -3);
    CHECK(othello::corner_2x3_pattern_score(opponent_x, Side::Black) == 3);
    CHECK(othello::corner_2x3_pattern_score(own_c, Side::Black) == -1);
    CHECK(othello::corner_2x3_pattern_score(opponent_c, Side::Black) == 1);
    CHECK(othello::corner_2x3_pattern_score(owned_support, Side::Black) == 6);
    CHECK(othello::corner_2x3_pattern_score(opponent_owned_support, Side::Black) == -6);

    const othello::EvaluationBreakdown own_x_breakdown =
        othello::evaluate_basic_breakdown(own_x, Side::Black, config);
    const othello::EvaluationBreakdown opponent_x_breakdown =
        othello::evaluate_basic_breakdown(opponent_x, Side::Black, config);
    const othello::EvaluationBreakdown own_c_breakdown =
        othello::evaluate_basic_breakdown(own_c, Side::Black, config);
    const othello::EvaluationBreakdown opponent_c_breakdown =
        othello::evaluate_basic_breakdown(opponent_c, Side::Black, config);

    CHECK(own_x_breakdown.corner_2x3_pattern == -3);
    CHECK(own_x_breakdown.corner_2x3_pattern_weight == 10);
    CHECK(own_x_breakdown.corner_2x3_pattern_score == -30);
    CHECK(opponent_x_breakdown.corner_2x3_pattern == 3);
    CHECK(opponent_x_breakdown.corner_2x3_pattern_score == 30);
    CHECK(own_c_breakdown.corner_2x3_pattern == -1);
    CHECK(own_c_breakdown.corner_2x3_pattern_score == -10);
    CHECK(opponent_c_breakdown.corner_2x3_pattern == 1);
    CHECK(opponent_c_breakdown.corner_2x3_pattern_score == 10);
}

TEST_CASE("Corner 2x3 pattern is symmetric across colors and contributes to total",
          "[evaluation]") {
    const othello::EvaluationConfig config = corner_pattern_only_config(5);
    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                             othello::test::bit("b2"),
                         othello::test::bit("a2"));
    const Board swapped = swapped_colors(board);

    CHECK(othello::corner_2x3_pattern_score(board, Side::Black) ==
          -othello::corner_2x3_pattern_score(swapped, Side::Black));
    CHECK(othello::evaluate_with_config(board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped, Side::Black, config));

    const Board supported =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));
    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(supported, Side::Black, config);

    CHECK(breakdown.corner_2x3_pattern == 6);
    CHECK(breakdown.corner_2x3_pattern_weight == 5);
    CHECK(breakdown.corner_2x3_pattern_score == 30);
    CHECK(breakdown.total == non_terminal_score_sum(breakdown));
    CHECK(breakdown.total == 30);
}

TEST_CASE("Edge stability lite counts only corner-anchored continuous edge discs",
          "[evaluation]") {
    const othello::EvaluationConfig config = edge_stability_only_config(3);
    const Board anchored =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("c1") | othello::test::bit("a2"));
    const Board unanchored =
        extra_disc_board(othello::test::bit("b1") | othello::test::bit("c1"));
    const Board empty_stop =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("c1"));
    const Board opponent_stop =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("c1"),
                         othello::test::bit("b1"));

    const othello::EvaluationBreakdown anchored_breakdown =
        othello::evaluate_basic_breakdown(anchored, Side::Black, config);
    CHECK(anchored_breakdown.edge_stability_lite == 3);
    CHECK(anchored_breakdown.edge_stability_lite_weight == 3);
    CHECK(anchored_breakdown.edge_stability_lite_score == 9);

    CHECK(othello::evaluate_basic_breakdown(unanchored, Side::Black, config)
              .edge_stability_lite == 0);
    CHECK(othello::evaluate_basic_breakdown(empty_stop, Side::Black, config)
              .edge_stability_lite == 0);
    CHECK(othello::evaluate_basic_breakdown(opponent_stop, Side::Black, config)
              .edge_stability_lite == 0);
}

TEST_CASE("Edge stability lite is symmetric across corners and colors", "[evaluation]") {
    const othello::EvaluationConfig config = edge_stability_only_config(3);

    for (const auto [corner, near, far] : {
             std::tuple{"a1", "b1", "c1"},
             std::tuple{"h1", "g1", "f1"},
             std::tuple{"a8", "b8", "c8"},
             std::tuple{"h8", "g8", "f8"},
         }) {
        const Board board =
            extra_disc_board(othello::test::bit(corner) | othello::test::bit(near) |
                             othello::test::bit(far));
        const othello::EvaluationBreakdown black =
            othello::evaluate_basic_breakdown(board, Side::Black, config);
        CHECK(black.edge_stability_lite == 2);
        CHECK(black.edge_stability_lite_score == 6);
    }

    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1"));
    const Board swapped = swapped_colors(board);
    CHECK(othello::evaluate_with_config(board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped, Side::Black, config));
}

TEST_CASE("Edge stability lite does not double-count full edges from both corners",
          "[evaluation]") {
    const othello::EvaluationConfig config = edge_stability_only_config(3);
    const Board full_top_edge = extra_disc_board(
        othello::test::bit("a1") | othello::test::bit("b1") | othello::test::bit("c1") |
        othello::test::bit("d1") | othello::test::bit("e1") | othello::test::bit("f1") |
        othello::test::bit("g1") | othello::test::bit("h1"));

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(full_top_edge, Side::Black, config);

    CHECK(breakdown.edge_stability_lite == 8);
    CHECK(breakdown.edge_stability_lite_score == 24);
}

TEST_CASE("Edge 8 pattern index is deterministic across edges", "[evaluation]") {
    using Edge = othello::Edge8PatternEdge;

    CHECK(othello::edge_8_pattern_table_size == 6561);
    CHECK(othello::edge_8_pattern_table_value(0) == 0);
    CHECK(othello::edge_8_pattern_table_value(-1) == 0);
    CHECK(othello::edge_8_pattern_table_value(6561) == 0);
    CHECK(othello::edge_8_pattern_index(Board::initial(), Side::Black, Edge::Top) == 0);

    const Board mixed =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("c1"),
                         othello::test::bit("b1"));
    CHECK(othello::edge_8_pattern_index(mixed, Side::Black, Edge::Top) == 16);
    CHECK(othello::edge_8_pattern_index(mixed, Side::Black, Edge::Top) >= 0);
    CHECK(othello::edge_8_pattern_index(mixed, Side::Black, Edge::Top) < 6561);

    for (const auto [edge, first, second, third] : {
             std::tuple{Edge::Top, "a1", "b1", "c1"},
             std::tuple{Edge::Bottom, "a8", "b8", "c8"},
             std::tuple{Edge::Left, "a1", "a2", "a3"},
             std::tuple{Edge::Right, "h1", "h2", "h3"},
         }) {
        const Board board =
            extra_disc_board(othello::test::bit(first) | othello::test::bit(second),
                             othello::test::bit(third));
        CHECK(othello::edge_8_pattern_index(board, Side::Black, edge) == 22);
    }
}

TEST_CASE("Classic broad pattern indexes are deterministic across symmetric lines",
          "[evaluation]") {
    using Corner3 = othello::Corner3x3PatternCorner;
    using Column = othello::Column8PatternColumn;
    using Diagonal = othello::Diagonal8PatternDiagonal;
    using EdgeX = othello::EdgeX10PatternEdge;
    using Inner = othello::InnerRow8PatternLine;
    using Row = othello::Row8PatternRow;

    CHECK(othello::corner_3x3_pattern_table_size == 19683);
    CHECK(othello::edge_x_10_pattern_table_size == 59049);
    CHECK(othello::row_8_pattern_table_size == 6561);
    CHECK(othello::column_8_pattern_table_size == 6561);
    CHECK(othello::diagonal_4_pattern_table_size == 81);
    CHECK(othello::diagonal_5_pattern_table_size == 243);
    CHECK(othello::diagonal_6_pattern_table_size == 729);
    CHECK(othello::diagonal_7_pattern_table_size == 2187);
    CHECK(othello::diagonal_8_pattern_table_size == 6561);
    CHECK(othello::inner_row_8_pattern_table_size == 6561);
    CHECK(othello::corner_2x4_pattern_table_size == 6561);
    CHECK(othello::corner_3x3_pattern_index(pattern_disc_board(0), Side::Black,
                                            Corner3::A1) == 0);
    CHECK(othello::edge_x_10_pattern_index(pattern_disc_board(0), Side::Black,
                                           EdgeX::Top) == 0);
    CHECK(othello::row_8_pattern_index(pattern_disc_board(0), Side::Black,
                                       Row::Rank1) == 0);
    CHECK(othello::column_8_pattern_index(pattern_disc_board(0), Side::Black,
                                          Column::FileA) == 0);
    CHECK(othello::diagonal_4_pattern_index(pattern_disc_board(0), Side::Black, 0) == 0);
    CHECK(othello::diagonal_5_pattern_index(pattern_disc_board(0), Side::Black, 0) == 0);
    CHECK(othello::diagonal_6_pattern_index(pattern_disc_board(0), Side::Black, 0) == 0);
    CHECK(othello::diagonal_7_pattern_index(pattern_disc_board(0), Side::Black, 0) == 0);
    CHECK(othello::diagonal_8_pattern_index(pattern_disc_board(0), Side::Black,
                                            Diagonal::A1H8) == 0);
    CHECK(othello::inner_row_8_pattern_index(pattern_disc_board(0), Side::Black,
                                             Inner::Top) == 0);
    CHECK(othello::corner_2x4_pattern_index(pattern_disc_board(0), Side::Black,
                                            othello::Corner2x4PatternCorner::A1) == 0);

    const Board corner_mixed =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c1") |
                               othello::test::bit("b2"),
                           othello::test::bit("b1") | othello::test::bit("a2") |
                               othello::test::bit("c3"));
    CHECK(othello::corner_3x3_pattern_index(corner_mixed, Side::Black,
                                            Corner3::A1) == 13273);

    for (const auto [corner, corner_square, c_square, x_square] : {
             std::tuple{Corner3::A1, "a1", "b1", "b2"},
             std::tuple{Corner3::H1, "h1", "g1", "g2"},
             std::tuple{Corner3::A8, "a8", "b8", "b7"},
             std::tuple{Corner3::H8, "h8", "g8", "g7"},
         }) {
        const Board board = pattern_disc_board(othello::test::bit(corner_square) |
                                               othello::test::bit(c_square) |
                                               othello::test::bit(x_square));
        CHECK(othello::corner_3x3_pattern_index(board, Side::Black, corner) == 85);
    }

    const Board edge_context =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c1") |
                               othello::test::bit("b2"),
                           othello::test::bit("b1") | othello::test::bit("g2"));
    CHECK(othello::edge_x_10_pattern_index(edge_context, Side::Black,
                                           EdgeX::Top) == 45943);
    for (const auto [edge, first, second, third] : {
             std::tuple{EdgeX::Top, "a1", "b1", "c1"},
             std::tuple{EdgeX::Bottom, "a8", "b8", "c8"},
             std::tuple{EdgeX::Left, "a1", "a2", "a3"},
             std::tuple{EdgeX::Right, "h1", "h2", "h3"},
         }) {
        const Board board =
            pattern_disc_board(othello::test::bit(first) | othello::test::bit(second),
                               othello::test::bit(third));
        CHECK(othello::edge_x_10_pattern_index(board, Side::Black, edge) == 22);
    }

    const Board diagonal =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c3"),
                           othello::test::bit("b2"));
    CHECK(othello::diagonal_8_pattern_index(diagonal, Side::Black,
                                            Diagonal::A1H8) == 16);

    const Board anti_diagonal =
        pattern_disc_board(othello::test::bit("h1") | othello::test::bit("f3"),
                           othello::test::bit("g2"));
    CHECK(othello::diagonal_8_pattern_index(anti_diagonal, Side::Black,
                                            Diagonal::H1A8) == 16);

    const Board broad_lines =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c1"),
                           othello::test::bit("b1"));
    CHECK(othello::row_8_pattern_index(broad_lines, Side::Black, Row::Rank1) == 16);
    CHECK(othello::column_8_pattern_index(broad_lines, Side::Black, Column::FileA) == 1);
    CHECK(othello::corner_2x4_pattern_index(
              broad_lines, Side::Black, othello::Corner2x4PatternCorner::A1) == 16);

    const Board short_diagonal =
        pattern_disc_board(othello::test::bit("e1") | othello::test::bit("g3"),
                           othello::test::bit("f2"));
    CHECK(othello::diagonal_4_pattern_index(short_diagonal, Side::Black, 0) == 16);
    CHECK(othello::diagonal_5_pattern_index(short_diagonal, Side::Black, 2) == 1);

    for (const auto [line, first, second, third] : {
             std::tuple{Inner::Top, "a2", "b2", "c2"},
             std::tuple{Inner::Bottom, "a7", "b7", "c7"},
             std::tuple{Inner::Left, "b1", "b2", "b3"},
             std::tuple{Inner::Right, "g1", "g2", "g3"},
         }) {
        const Board board =
            pattern_disc_board(othello::test::bit(first) | othello::test::bit(third),
                               othello::test::bit(second));
        CHECK(othello::inner_row_8_pattern_index(board, Side::Black, line) == 16);
    }
}

TEST_CASE("Edge 8 pattern table follows conservative edge rules", "[evaluation]") {
    const othello::EvaluationConfig config = edge_pattern_only_config(5);
    const Board own_full_edge = extra_disc_board(
        othello::test::bit("a1") | othello::test::bit("b1") | othello::test::bit("c1") |
        othello::test::bit("d1") | othello::test::bit("e1") | othello::test::bit("f1") |
        othello::test::bit("g1") | othello::test::bit("h1"));
    const Board opponent_full_edge = extra_disc_board(
        0, othello::test::bit("a1") | othello::test::bit("b1") |
               othello::test::bit("c1") | othello::test::bit("d1") |
               othello::test::bit("e1") | othello::test::bit("f1") |
               othello::test::bit("g1") | othello::test::bit("h1"));
    const Board own_anchor =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("c1"));
    const Board opponent_anchor =
        extra_disc_board(0, othello::test::bit("a1") | othello::test::bit("b1") |
                                othello::test::bit("c1"));
    const Board own_c = extra_disc_board(othello::test::bit("b1"));
    const Board opponent_c = extra_disc_board(0, othello::test::bit("b1"));
    const Board unanchored =
        extra_disc_board(othello::test::bit("b1") | othello::test::bit("c1"));

    CHECK(othello::edge_8_pattern_score(Board::initial(), Side::Black) == 0);
    CHECK(othello::edge_8_pattern_score(own_full_edge, Side::Black) > 0);
    CHECK(othello::edge_8_pattern_score(opponent_full_edge, Side::Black) < 0);
    CHECK(othello::edge_8_pattern_score(own_anchor, Side::Black) > 0);
    CHECK(othello::edge_8_pattern_score(opponent_anchor, Side::Black) < 0);
    CHECK(othello::edge_8_pattern_score(own_c, Side::Black) < 0);
    CHECK(othello::edge_8_pattern_score(opponent_c, Side::Black) > 0);
    CHECK(othello::edge_8_pattern_score(unanchored, Side::Black) <= 0);

    const othello::EvaluationBreakdown own_anchor_breakdown =
        othello::evaluate_basic_breakdown(own_anchor, Side::Black, config);
    CHECK(own_anchor_breakdown.edge_8_pattern > 0);
    CHECK(own_anchor_breakdown.edge_8_pattern_weight == 5);
    CHECK(own_anchor_breakdown.edge_8_pattern_score ==
          own_anchor_breakdown.edge_8_pattern * own_anchor_breakdown.edge_8_pattern_weight);
    CHECK(own_anchor_breakdown.total == non_terminal_score_sum(own_anchor_breakdown));
    CHECK(own_anchor_breakdown.total == own_anchor_breakdown.edge_8_pattern_score);
}

TEST_CASE("Edge 8 pattern is symmetric across colors and separable from edge stability",
          "[evaluation]") {
    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                             othello::test::bit("c1"),
                         othello::test::bit("e1"));
    const Board swapped = swapped_colors(board);

    CHECK(othello::edge_8_pattern_score(board, Side::Black) ==
          -othello::edge_8_pattern_score(swapped, Side::Black));
    CHECK(othello::evaluate_with_config(board, Side::Black, edge_pattern_only_config(3)) ==
          -othello::evaluate_with_config(swapped, Side::Black, edge_pattern_only_config(3)));

    othello::EvaluationConfig no_edge_lite_config = othello::default_evaluation_config();
    no_edge_lite_config.opening.edge_stability_lite = 0;
    no_edge_lite_config.midgame.edge_stability_lite = 0;
    no_edge_lite_config.late.edge_stability_lite = 0;

    const othello::EvaluationBreakdown combined =
        othello::evaluate_basic_breakdown(board, Side::Black,
                                          othello::default_evaluation_config());
    const othello::EvaluationBreakdown no_edge_lite =
        othello::evaluate_basic_breakdown(board, Side::Black, no_edge_lite_config);

    CHECK(combined.edge_stability_lite_weight > 0);
    CHECK(combined.edge_8_pattern_weight > 0);
    CHECK(no_edge_lite.edge_stability_lite_weight == 0);
    CHECK(no_edge_lite.edge_8_pattern_weight > 0);
}

TEST_CASE("Classic lite scores participate in configured total math", "[evaluation]") {
    othello::EvaluationConfig config{
        .opening = othello::EvaluationFeatureWeights{
            .corner_local_2x3 = 10,
            .edge_stability_lite = 3,
        },
        .midgame = othello::EvaluationFeatureWeights{
            .corner_local_2x3 = 10,
            .edge_stability_lite = 3,
        },
        .late = othello::EvaluationFeatureWeights{
            .corner_local_2x3 = 10,
            .edge_stability_lite = 3,
        },
    };
    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, config);

    CHECK(breakdown.corner_local_2x3 == 2);
    CHECK(breakdown.edge_stability_lite == 2);
    CHECK(breakdown.corner_local_2x3_score == 20);
    CHECK(breakdown.edge_stability_lite_score == 6);
    CHECK(breakdown.total == non_terminal_score_sum(breakdown));
    CHECK(breakdown.total == 26);
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
    CHECK(black.corner_local_2x3_score == 0);
    CHECK(black.corner_2x3_pattern_score == 0);
    CHECK(black.edge_stability_lite_score == 0);
    CHECK(black.edge_8_pattern_score == 0);

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
    config.opening.corner_local_2x3 = 99;
    config.opening.corner_2x3_pattern = 99;
    config.midgame.edge_stability_lite = 99;
    config.late.edge_8_pattern = 99;

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
    static_cast<void>(
        othello::evaluate_basic_breakdown(board, Side::Black, sparse_scalar_config()));
    static_cast<void>(
        othello::evaluate_basic_breakdown(board, Side::Black, corner_pattern_only_config(3)));
    static_cast<void>(
        othello::evaluate_basic_breakdown(board, Side::Black, edge_pattern_only_config(3)));
    static_cast<void>(
        othello::evaluate_basic_breakdown(board, Side::Black, pattern_table_only_config(1)));
    static_cast<void>(
        othello::evaluate_with_config(board, Side::Black, sparse_scalar_config()));
    static_cast<void>(
        othello::evaluate_with_config(board, Side::Black, corner_pattern_only_config(3)));
    static_cast<void>(
        othello::evaluate_with_config(board, Side::Black, edge_pattern_only_config(3)));
    static_cast<void>(
        othello::evaluate_with_config(board, Side::Black, pattern_table_only_config(1)));
    static_cast<void>(othello::evaluate_basic(board, Side::Black));

    CHECK(othello::test::same_board(board, before));
}
