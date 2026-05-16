#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

using othello::Board;
using othello::Side;

TEST_CASE("Zobrist hash is stable for repeated calls", "[hash]") {
    const Board board = Board::initial();

    const othello::ZobristHash first = othello::zobrist_hash(board);
    const othello::ZobristHash second = othello::zobrist_hash(board);

    CHECK(first == second);
}

TEST_CASE("Zobrist hash includes side to move", "[hash]") {
    const Board black_to_move = Board::initial();
    Board white_to_move = black_to_move;
    white_to_move.side_to_move = Side::White;

    CHECK(othello::zobrist_hash(black_to_move) != othello::zobrist_hash(white_to_move));

    const Board empty_black{
        .black = 0,
        .white = 0,
        .side_to_move = Side::Black,
    };
    const Board empty_white{
        .black = 0,
        .white = 0,
        .side_to_move = Side::White,
    };

    CHECK(othello::zobrist_hash(empty_black) != othello::zobrist_hash(empty_white));
}

TEST_CASE("Zobrist hash changes after a legal move", "[hash]") {
    const Board board = Board::initial();
    const auto next = othello::apply_move(board, othello::test::square("d3"));

    REQUIRE(next.has_value());
    CHECK(othello::zobrist_hash(board) != othello::zobrist_hash(*next));
}

TEST_CASE("Different boards usually have different Zobrist hashes", "[hash]") {
    const Board initial = Board::initial();
    const Board corner_owned{
        .black = initial.black | othello::test::bit("a1"),
        .white = initial.white,
        .side_to_move = Side::Black,
    };

    CHECK(othello::zobrist_hash(initial) != othello::zobrist_hash(corner_owned));
}

TEST_CASE("Board serialization round-trip preserves Zobrist hash", "[hash]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const auto parsed = othello::board_from_string(othello::to_string(*board));

    REQUIRE(parsed.has_value());
    CHECK(othello::zobrist_hash(*board) == othello::zobrist_hash(*parsed));
}

TEST_CASE("Swapping disc colors changes the Zobrist hash", "[hash]") {
    const Board board = Board::initial();
    const Board swapped{
        .black = board.white,
        .white = board.black,
        .side_to_move = board.side_to_move,
    };

    CHECK(othello::zobrist_hash(board) != othello::zobrist_hash(swapped));
}
