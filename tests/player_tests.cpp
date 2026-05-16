#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

using othello::Board;
using othello::Side;

TEST_CASE("First legal move returns the lowest-index legal move", "[player]") {
    const Board board = Board::initial();

    const auto move = othello::first_legal_move(board);

    REQUIRE(move.has_value());
    CHECK(*move == othello::test::square("d3"));
}

TEST_CASE("First legal move does not mutate the board", "[player]") {
    const Board board = Board::initial();
    const Board before = board;

    const auto move = othello::first_legal_move(board);

    REQUIRE(move.has_value());
    CHECK(othello::test::same_board(board, before));
}

TEST_CASE("First legal move can be applied by the caller", "[player]") {
    const Board board = Board::initial();
    const auto move = othello::first_legal_move(board);

    REQUIRE(move.has_value());

    const auto next = othello::apply_move(board, *move);

    REQUIRE(next.has_value());
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("First legal move returns null when the current side has no legal moves", "[player]") {
    const Board board = othello::test::black_must_pass_board();

    CHECK_FALSE(othello::has_legal_move(board));
    CHECK_FALSE(othello::first_legal_move(board).has_value());
}
