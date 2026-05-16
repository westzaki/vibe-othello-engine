#include <othello/othello.hpp>

#include <bit>
#include <iostream>
#include <string_view>

namespace {

using othello::Bitboard;
using othello::Board;
using othello::Side;
using othello::Square;

[[nodiscard]] Bitboard bit(std::string_view coordinate)
{
    const auto square = othello::square_from_string(coordinate);
    if (!square.has_value()) {
        std::cerr << "Invalid test coordinate: " << coordinate << '\n';
        return 0;
    }

    return square->bit();
}

int failures = 0;

void check(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_initial_board()
{
    const Board board = Board::initial();

    check(std::popcount(board.black) == 2, "initial board has 2 black discs");
    check(std::popcount(board.white) == 2, "initial board has 2 white discs");
    check(board.side_to_move == Side::Black, "initial side to move is black");
}

void test_coordinate_conversion()
{
    struct Example {
        std::string_view coordinate;
        int index;
    };

    constexpr Example examples[] = {
        {"a1", 0},
        {"h1", 7},
        {"a8", 56},
        {"h8", 63},
        {"d3", 19},
        {"c4", 26},
    };

    for (const Example example : examples) {
        const auto square = othello::square_from_string(example.coordinate);

        check(square.has_value(), "coordinate parses");
        if (square.has_value()) {
            check(square->index() == example.index, "coordinate maps to expected square index");
            check(othello::to_string(*square) == example.coordinate, "square converts back to coordinate");
        }
    }

    check(!othello::square_from_string(""), "empty coordinate is invalid");
    check(!othello::square_from_string("i1"), "file past h is invalid");
    check(!othello::square_from_string("a9"), "rank past 8 is invalid");
    check(!othello::square_from_string("D3"), "uppercase coordinate is invalid");
}

void test_initial_legal_moves_for_black()
{
    const Board board = Board::initial();
    const Bitboard expected = bit("d3") | bit("c4") | bit("f5") | bit("e6");

    check(othello::legal_moves(board) == expected, "initial black legal moves are d3, c4, f5, e6");
}

} // namespace

int main()
{
    test_initial_board();
    test_coordinate_conversion();
    test_initial_legal_moves_for_black();

    return failures == 0 ? 0 : 1;
}
