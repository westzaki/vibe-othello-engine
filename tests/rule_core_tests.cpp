#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>
#include <string>
#include <string_view>

namespace {

using othello::Bitboard;
using othello::Board;
using othello::Side;
using othello::Square;

[[nodiscard]] Bitboard bit(std::string_view coordinate) {
    const auto square = othello::square_from_string(coordinate);
    REQUIRE(square.has_value());
    return square->bit();
}

[[nodiscard]] Square square(std::string_view coordinate) {
    const auto parsed = othello::square_from_string(coordinate);
    REQUIRE(parsed.has_value());
    return *parsed;
}

} // namespace

TEST_CASE("Initial board has starting discs and black to move", "[rule-core]") {
    const Board board = Board::initial();

    CHECK(std::popcount(board.black) == 2);
    CHECK(std::popcount(board.white) == 2);
    CHECK(board.side_to_move == Side::Black);
}

TEST_CASE("Coordinates convert to and from square indexes", "[rule-core]") {
    struct Example {
        std::string_view coordinate;
        int index;
    };

    constexpr std::array examples{
        Example{.coordinate = "a1", .index = 0},  Example{.coordinate = "h1", .index = 7},
        Example{.coordinate = "a8", .index = 56}, Example{.coordinate = "h8", .index = 63},
        Example{.coordinate = "d3", .index = 19}, Example{.coordinate = "c4", .index = 26},
    };

    for (const Example example : examples) {
        CAPTURE(std::string{example.coordinate});

        const auto square = othello::square_from_string(example.coordinate);

        REQUIRE(square.has_value());
        CHECK(square->index() == example.index);
        CHECK(othello::to_string(*square) == std::string{example.coordinate});
    }
}

TEST_CASE("Invalid coordinates are rejected", "[rule-core]") {
    CHECK_FALSE(othello::square_from_string("").has_value());
    CHECK_FALSE(othello::square_from_string("i1").has_value());
    CHECK_FALSE(othello::square_from_string("a9").has_value());
    CHECK_FALSE(othello::square_from_string("D3").has_value());
}

TEST_CASE("Initial black legal moves are d3 c4 f5 e6", "[rule-core]") {
    const Board board = Board::initial();
    const Bitboard expected = bit("d3") | bit("c4") | bit("f5") | bit("e6");

    CHECK(othello::legal_moves(board) == expected);
}

TEST_CASE("Initial black move d3 places and flips discs", "[rule-core]") {
    const auto next = othello::apply_move(Board::initial(), square("d3"));

    REQUIRE(next.has_value());
    CHECK((next->black & bit("d3")) != 0);
    CHECK((next->black & bit("d4")) != 0);
    CHECK(std::popcount(next->black) == 4);
    CHECK(std::popcount(next->white) == 1);
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("Each initial black legal move leaves four black discs and one white disc",
          "[rule-core]") {
    constexpr std::array moves{"d3", "c4", "f5", "e6"};

    for (const std::string_view move : moves) {
        CAPTURE(std::string{move});

        const auto next = othello::apply_move(Board::initial(), square(move));

        REQUIRE(next.has_value());
        CHECK(std::popcount(next->black) == 4);
        CHECK(std::popcount(next->white) == 1);
        CHECK(next->side_to_move == Side::White);
    }
}

TEST_CASE("Illegal moves do not produce a changed board", "[rule-core]") {
    const Board board = Board::initial();

    CHECK(othello::flips_for_move(board, square("a1")) == 0);
    CHECK_FALSE(othello::apply_move(board, square("a1")).has_value());
}

TEST_CASE("Move application flips discs in multiple directions", "[rule-core]") {
    const Board board{
        .black = bit("b4") | bit("f4") | bit("d6") | bit("f6"),
        .white = bit("c4") | bit("e4") | bit("d5") | bit("e5"),
        .side_to_move = Side::Black,
    };
    const Square move = square("d4");
    const Bitboard expected_flips = bit("c4") | bit("e4") | bit("d5") | bit("e5");

    CHECK(othello::flips_for_move(board, move) == expected_flips);

    const auto next = othello::apply_move(board, move);

    REQUIRE(next.has_value());
    CHECK((next->black & bit("d4")) != 0);
    CHECK((next->black & expected_flips) == expected_flips);
    CHECK(std::popcount(next->black) == 9);
    CHECK(std::popcount(next->white) == 0);
    CHECK(next->side_to_move == Side::White);
}
