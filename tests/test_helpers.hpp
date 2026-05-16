#pragma once

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace othello::test {

[[nodiscard]] inline Bitboard bit(std::string_view coordinate) {
    const auto square = square_from_string(coordinate);
    REQUIRE(square.has_value());
    return square->bit();
}

[[nodiscard]] inline Square square(std::string_view coordinate) {
    const auto parsed = square_from_string(coordinate);
    REQUIRE(parsed.has_value());
    return *parsed;
}

[[nodiscard]] inline Board board_from_text(std::string_view text) {
    const auto board = board_from_string(text);
    REQUIRE(board.has_value());
    return *board;
}

[[nodiscard]] inline Board black_must_pass_board() {
    return Board{
        .black = bit("d4"),
        .white = bit("e4") | bit("f4") | bit("g4") | bit("h4"),
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] inline bool same_board(const Board& lhs, const Board& rhs) noexcept {
    return lhs.black == rhs.black && lhs.white == rhs.white && lhs.side_to_move == rhs.side_to_move;
}

[[nodiscard]] inline std::vector<Square> squares_from_bitboard(Bitboard bits) {
    std::vector<Square> squares;

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const auto square = Square::from_index(index);
        REQUIRE(square.has_value());

        if ((bits & square->bit()) != 0) {
            squares.push_back(*square);
        }
    }

    return squares;
}

inline void require_board_invariants(const Board& board) {
    CHECK((board.black & board.white) == 0);
    CHECK(disc_count(board, Side::Black) + disc_count(board, Side::White) <= 64);

    const std::string text = to_string(board);
    const auto parsed = board_from_string(text);

    REQUIRE(parsed.has_value());
    CHECK(same_board(*parsed, board));
}

inline constexpr std::string_view initial_board_text = R"(........
........
........
...BW...
...WB...
........
........
........
side=B)";

} // namespace othello::test
