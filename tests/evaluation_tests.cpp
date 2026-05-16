#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

using othello::Bitboard;
using othello::Board;
using othello::Side;

TEST_CASE("Initial board evaluation is symmetric", "[evaluation]") {
    const Board board = Board::initial();

    CHECK(othello::evaluate_disc_difference(board, Side::Black) == 0);
    CHECK(othello::evaluate_disc_difference(board, Side::White) == 0);
    CHECK(othello::evaluate_mobility(board, Side::Black) == 0);
    CHECK(othello::evaluate_mobility(board, Side::White) == 0);
    CHECK(othello::evaluate_basic(board, Side::Black) ==
          -othello::evaluate_basic(board, Side::White));
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

TEST_CASE("Terminal board evaluation is strongly scaled", "[evaluation]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    CHECK(othello::evaluate_basic(board, Side::Black) > 10'000);
    CHECK(othello::evaluate_basic(board, Side::White) < -10'000);
}

TEST_CASE("Evaluation does not mutate the board", "[evaluation]") {
    const Board board = Board::initial();
    const Board before = board;

    static_cast<void>(othello::evaluate_disc_difference(board, Side::Black));
    static_cast<void>(othello::evaluate_mobility(board, Side::Black));
    static_cast<void>(othello::evaluate_basic(board, Side::Black));

    CHECK(othello::test::same_board(board, before));
}
