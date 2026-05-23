#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

using othello::Board;
using othello::Side;

TEST_CASE("Exact endgame solver returns terminal margin immediately", "[endgame]") {
    const Board board{
        .black = ~othello::Bitboard{0},
        .white = 0,
        .side_to_move = Side::White,
    };

    const othello::ExactEndgameResult result = othello::solve_exact_endgame(board);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.nodes > 0);
    CHECK(result.principal_variation.empty());
    CHECK(result.empties == 0);
    CHECK(result.disc_margin == othello::score(board, board.side_to_move));
}

TEST_CASE("Exact endgame solver solves a one-empty forced move", "[endgame]") {
    const Board board = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)");

    const othello::ExactEndgameResult result = othello::solve_exact_endgame(board);

    REQUIRE(result.best_move.has_value());
    CHECK(*result.best_move == othello::test::square("h1"));
    CHECK(result.disc_margin == 64);
    CHECK(result.empties == 1);
    CHECK(result.nodes > 1);
    REQUIRE_FALSE(result.principal_variation.empty());
    CHECK(result.principal_variation.front() == *result.best_move);
    CHECK(result.principal_variation.size() <= static_cast<std::size_t>(result.empties));
}

TEST_CASE("Exact endgame solver handles root pass without adding a pass to PV", "[endgame]") {
    const Board board = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBWB.
side=B)");

    REQUIRE(othello::legal_moves(board) == 0);
    const auto after_pass = othello::pass_turn(board);
    REQUIRE(after_pass.has_value());

    const othello::ExactEndgameResult result = othello::solve_exact_endgame(board);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.disc_margin == 58);
    CHECK(result.empties == 1);
    REQUIRE_FALSE(result.principal_variation.empty());
    CHECK(result.principal_variation.front() == othello::test::square("h1"));
    CHECK((othello::legal_moves(*after_pass) & result.principal_variation.front().bit()) != 0);
    CHECK(result.principal_variation.size() <= static_cast<std::size_t>(result.empties));
}

TEST_CASE("Exact endgame solver is deterministic", "[endgame]") {
    const Board board = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)");

    const othello::ExactEndgameResult first = othello::solve_exact_endgame(board);
    const othello::ExactEndgameResult second = othello::solve_exact_endgame(board);

    CHECK(first.best_move == second.best_move);
    CHECK(first.disc_margin == second.disc_margin);
    CHECK(first.empties == second.empties);
    CHECK(first.nodes == second.nodes);
    CHECK(first.principal_variation == second.principal_variation);
}

TEST_CASE("Exact endgame solver breaks equal scores by lower square index", "[endgame]") {
    const Board board = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
.WBBBBW.
side=B)");

    const othello::ExactEndgameResult result = othello::solve_exact_endgame(board);

    REQUIRE(result.best_move.has_value());
    CHECK(*result.best_move == othello::test::square("a1"));
    CHECK(result.disc_margin == 64);
    REQUIRE_FALSE(result.principal_variation.empty());
    CHECK(result.principal_variation.front() == *result.best_move);
    CHECK(result.principal_variation.size() <= static_cast<std::size_t>(result.empties));
}

TEST_CASE("Exact endgame PV starts with the selected best move", "[endgame]") {
    const Board board = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)");

    const othello::ExactEndgameResult result = othello::solve_exact_endgame(board);

    REQUIRE(result.best_move.has_value());
    REQUIRE_FALSE(result.principal_variation.empty());
    CHECK(result.principal_variation.front() == *result.best_move);
    CHECK(result.principal_variation.size() <= static_cast<std::size_t>(result.empties));
}

TEST_CASE("Exact endgame stats mirror node count and leave TT counters zero when disabled",
          "[endgame]") {
    const Board board = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)");

    const othello::ExactEndgameResult result = othello::solve_exact_endgame(board);

    CHECK(result.stats.nodes == result.nodes);
    CHECK(result.stats.tt_lookups == 0);
    CHECK(result.stats.tt_hits == 0);
    CHECK(result.stats.tt_exact_hits == 0);
    CHECK(result.stats.tt_lower_hits == 0);
    CHECK(result.stats.tt_upper_hits == 0);
    CHECK(result.stats.tt_stores == 0);
    CHECK(result.stats.tt_overwrites == 0);
    CHECK(result.stats.tt_collisions == 0);
    CHECK(result.stats.tt_rejected_stores == 0);
}

TEST_CASE("Exact endgame reports TT activity and consistent TT stats when enabled", "[endgame]") {
    const Board board = othello::test::board_from_text(R"(...B....
WWBWWW.B
WWWWWWWW
WBWWWWW.
WWWBBWBB
WBBBWB.B
WWBWBWBB
WWWWWWWB
side=B)");

    const othello::ExactEndgameResult result = othello::solve_exact_endgame(board);

    CHECK(result.empties == 10);
    CHECK(result.stats.nodes == result.nodes);
    CHECK(result.stats.tt_lookups > 0);
    CHECK(result.stats.tt_stores > 0);
    CHECK(result.stats.tt_hits <= result.stats.tt_lookups);
    CHECK(result.stats.tt_collisions <= result.stats.tt_overwrites);
    CHECK(result.stats.tt_exact_hits + result.stats.tt_lower_hits + result.stats.tt_upper_hits ==
          result.stats.tt_hits);
}
