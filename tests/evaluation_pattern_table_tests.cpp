#include "common/eval_config_io.hpp"
#include "common/evaluator_selection.hpp"
#include "evaluation_test_helpers.hpp"
#include "positions/metrics.hpp"
#include "positions/search_positions.hpp"
#include "positions/tags.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
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

TEST_CASE("Zero-weighted pattern tables are not required for score evaluation",
          "[evaluation]") {
    othello::EvaluationConfig config{
        .opening = othello::EvaluationFeatureWeights{.mobility = 3},
        .midgame = othello::EvaluationFeatureWeights{.frontier = 5},
        .late = othello::EvaluationFeatureWeights{.disc_difference = 7},
    };
    config.pattern_tables.reset();
    config.opening_pattern_tables.reset();
    config.midgame_pattern_tables.reset();
    config.late_pattern_tables.reset();

    for (const Board& board : {Board::initial(), phase_midgame_board(), phase_late_board()}) {
        for (const Side side : {Side::Black, Side::White}) {
            const othello::EvaluationBreakdown breakdown =
                othello::evaluate_basic_breakdown(board, side, config);
            REQUIRE(breakdown.pattern_table_weight == 0);
            CHECK(breakdown.pattern_table_score == 0);
            CHECK(othello::evaluate_with_config(board, side, config) == breakdown.total);
        }
    }
}

TEST_CASE("External pattern table is deterministic and config gated", "[evaluation]") {
    auto tables = std::make_shared<othello::PatternTableBundle>();
    othello::EvaluationConfig config = pattern_table_only_config(7, tables);
    tables->corner_2x3[1] = 3;
    tables->corner_2x3[2] = -3;
    tables->edge_8[1] = 2;
    tables->edge_8[2] = -2;

    const Board board = extra_disc_board(othello::test::bit("a1"));
    const othello::EvaluationBreakdown first =
        othello::evaluate_basic_breakdown(board, Side::Black, config);
    const othello::EvaluationBreakdown second =
        othello::evaluate_basic_breakdown(board, Side::Black, config);

    CHECK(first.pattern_table == 7);
    CHECK(first.pattern_table_weight == 7);
    CHECK(first.pattern_table_score == 49);
    CHECK(first.total == 49);
    CHECK(second.pattern_table == first.pattern_table);
    CHECK(second.total == first.total);

    othello::EvaluationConfig disabled = config;
    disabled.pattern_tables.reset();
    const othello::EvaluationBreakdown disabled_breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, disabled);
    CHECK(disabled_breakdown.pattern_table == 0);
    CHECK(disabled_breakdown.pattern_table_score == 0);
    CHECK(disabled_breakdown.total == 0);

    othello::EvaluationConfig zero_weight = config;
    zero_weight.opening.pattern_table = 0;
    const othello::EvaluationBreakdown zero_weight_breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, zero_weight);
    CHECK(zero_weight_breakdown.pattern_table == 0);
    CHECK(zero_weight_breakdown.pattern_table_score == 0);
    CHECK(zero_weight_breakdown.total == 0);
}

TEST_CASE("External broad pattern table is antisymmetric under color swap",
          "[evaluation]") {
    auto tables = std::make_shared<othello::PatternTableBundle>();
    othello::EvaluationConfig config = pattern_table_only_config(1, tables);
    tables->corner_3x3[1] = 3;
    tables->corner_3x3[2] = -3;
    tables->edge_x_10[1] = 5;
    tables->edge_x_10[2] = -5;
    tables->diagonal_8[1] = 7;
    tables->diagonal_8[2] = -7;
    tables->inner_row_8[1] = 11;
    tables->inner_row_8[2] = -11;

    const Board corner_board = pattern_disc_board(othello::test::bit("a1"));
    const Board swapped_corner = swapped_colors(corner_board);
    CHECK(othello::evaluation_pattern_table_value(corner_board, Side::Black,
                                                  *tables) == 20);
    CHECK(othello::evaluation_pattern_table_value(corner_board, Side::Black,
                                                  *tables) ==
          -othello::evaluation_pattern_table_value(swapped_corner, Side::Black,
                                                   *tables));

    const Board inner_board = pattern_disc_board(othello::test::bit("b1"));
    const Board swapped_inner = swapped_colors(inner_board);
    CHECK(othello::evaluation_pattern_table_value(inner_board, Side::Black,
                                                  *tables) == 11);
    CHECK(othello::evaluate_with_config(inner_board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped_inner, Side::Black, config));
}

TEST_CASE("External pattern table value follows public pattern indexes",
          "[evaluation]") {
    othello::PatternTableBundle tables;
    const Board board =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c1") |
                               othello::test::bit("b2") | othello::test::bit("h1") |
                               othello::test::bit("f3") | othello::test::bit("a8"),
                           othello::test::bit("b1") | othello::test::bit("a2") |
                               othello::test::bit("c3") | othello::test::bit("g2") |
                               othello::test::bit("b8") | othello::test::bit("h8"));

    constexpr std::array corner_2x3_corners{
        othello::Corner2x3PatternCorner::A1,
        othello::Corner2x3PatternCorner::H1,
        othello::Corner2x3PatternCorner::A8,
        othello::Corner2x3PatternCorner::H8,
    };
    constexpr std::array corner_3x3_corners{
        othello::Corner3x3PatternCorner::A1,
        othello::Corner3x3PatternCorner::H1,
        othello::Corner3x3PatternCorner::A8,
        othello::Corner3x3PatternCorner::H8,
    };
    constexpr std::array edge_8_edges{
        othello::Edge8PatternEdge::Top,
        othello::Edge8PatternEdge::Bottom,
        othello::Edge8PatternEdge::Left,
        othello::Edge8PatternEdge::Right,
    };
    constexpr std::array edge_x_10_edges{
        othello::EdgeX10PatternEdge::Top,
        othello::EdgeX10PatternEdge::Bottom,
        othello::EdgeX10PatternEdge::Left,
        othello::EdgeX10PatternEdge::Right,
    };
    constexpr std::array diagonals{
        othello::Diagonal8PatternDiagonal::A1H8,
        othello::Diagonal8PatternDiagonal::H1A8,
    };
    constexpr std::array inner_rows{
        othello::InnerRow8PatternLine::Top,
        othello::InnerRow8PatternLine::Bottom,
        othello::InnerRow8PatternLine::Left,
        othello::InnerRow8PatternLine::Right,
    };

    for (const othello::Corner2x3PatternCorner corner : corner_2x3_corners) {
        const int index = othello::corner_2x3_pattern_index(board, Side::Black, corner);
        tables.corner_2x3[static_cast<std::size_t>(index)] =
            static_cast<std::int16_t>(11 + (index % 17));
    }
    for (const othello::Corner3x3PatternCorner corner : corner_3x3_corners) {
        const int index = othello::corner_3x3_pattern_index(board, Side::Black, corner);
        tables.corner_3x3[static_cast<std::size_t>(index)] =
            static_cast<std::int16_t>(23 + (index % 19));
    }
    for (const othello::Edge8PatternEdge edge : edge_8_edges) {
        const int index = othello::edge_8_pattern_index(board, Side::Black, edge);
        tables.edge_8[static_cast<std::size_t>(index)] =
            static_cast<std::int16_t>(-7 + (index % 23));
    }
    for (const othello::EdgeX10PatternEdge edge : edge_x_10_edges) {
        const int index = othello::edge_x_10_pattern_index(board, Side::Black, edge);
        tables.edge_x_10[static_cast<std::size_t>(index)] =
            static_cast<std::int16_t>(-31 + (index % 29));
    }
    for (const othello::Diagonal8PatternDiagonal diagonal : diagonals) {
        const int index = othello::diagonal_8_pattern_index(board, Side::Black, diagonal);
        tables.diagonal_8[static_cast<std::size_t>(index)] =
            static_cast<std::int16_t>(41 + (index % 31));
    }
    for (const othello::InnerRow8PatternLine line : inner_rows) {
        const int index = othello::inner_row_8_pattern_index(board, Side::Black, line);
        tables.inner_row_8[static_cast<std::size_t>(index)] =
            static_cast<std::int16_t>(-43 + (index % 37));
    }

    int expected = 0;
    for (const othello::Corner2x3PatternCorner corner : corner_2x3_corners) {
        expected += tables.corner_2x3[static_cast<std::size_t>(
            othello::corner_2x3_pattern_index(board, Side::Black, corner))];
    }
    for (const othello::Corner3x3PatternCorner corner : corner_3x3_corners) {
        expected += tables.corner_3x3[static_cast<std::size_t>(
            othello::corner_3x3_pattern_index(board, Side::Black, corner))];
    }
    for (const othello::Edge8PatternEdge edge : edge_8_edges) {
        expected += tables.edge_8[static_cast<std::size_t>(
            othello::edge_8_pattern_index(board, Side::Black, edge))];
    }
    for (const othello::EdgeX10PatternEdge edge : edge_x_10_edges) {
        expected += tables.edge_x_10[static_cast<std::size_t>(
            othello::edge_x_10_pattern_index(board, Side::Black, edge))];
    }
    for (const othello::Diagonal8PatternDiagonal diagonal : diagonals) {
        expected += tables.diagonal_8[static_cast<std::size_t>(
            othello::diagonal_8_pattern_index(board, Side::Black, diagonal))];
    }
    for (const othello::InnerRow8PatternLine line : inner_rows) {
        expected += tables.inner_row_8[static_cast<std::size_t>(
            othello::inner_row_8_pattern_index(board, Side::Black, line))];
    }

    CHECK(othello::evaluation_pattern_table_value(board, Side::Black, tables) ==
          expected);
    CHECK(othello::evaluation_pattern_table_score(board, Side::Black, tables) ==
          expected);
}
