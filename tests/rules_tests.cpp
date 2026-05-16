#include "test_helpers.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using othello::Bitboard;
using othello::Board;
using othello::Side;
using othello::Square;

TEST_CASE("Initial board has starting discs and black to move", "[rule-core]") {
    const Board board = Board::initial();

    CHECK(othello::disc_count(board, Side::Black) == 2);
    CHECK(othello::disc_count(board, Side::White) == 2);
    CHECK(board.side_to_move == Side::Black);
}

TEST_CASE("Initial black legal moves are d3 c4 f5 e6", "[rule-core]") {
    const Board board = Board::initial();
    const Bitboard expected = othello::test::bit("d3") | othello::test::bit("c4") |
                              othello::test::bit("f5") | othello::test::bit("e6");

    CHECK(othello::has_legal_move(board));
    CHECK(othello::legal_moves(board) == expected);
}

TEST_CASE("Initial black move d3 places and flips discs", "[rule-core]") {
    const auto next = othello::apply_move(Board::initial(), othello::test::square("d3"));

    REQUIRE(next.has_value());
    CHECK((next->black & othello::test::bit("d3")) != 0);
    CHECK((next->black & othello::test::bit("d4")) != 0);
    CHECK(othello::disc_count(*next, Side::Black) == 4);
    CHECK(othello::disc_count(*next, Side::White) == 1);
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("Each initial black legal move leaves four black discs and one white disc",
          "[rule-core]") {
    constexpr std::array moves{"d3", "c4", "f5", "e6"};

    for (const std::string_view move : moves) {
        CAPTURE(std::string{move});

        const auto next = othello::apply_move(Board::initial(), othello::test::square(move));

        REQUIRE(next.has_value());
        CHECK(othello::disc_count(*next, Side::Black) == 4);
        CHECK(othello::disc_count(*next, Side::White) == 1);
        CHECK(next->side_to_move == Side::White);
    }
}

TEST_CASE("Illegal moves do not produce a changed board", "[rule-core]") {
    const Board board = Board::initial();

    CHECK(othello::flips_for_move(board, othello::test::square("a1")) == 0);
    CHECK_FALSE(othello::apply_move(board, othello::test::square("a1")).has_value());
}

TEST_CASE("Move application flips discs in multiple directions", "[rule-core]") {
    const Board board{
        .black = othello::test::bit("b4") | othello::test::bit("f4") | othello::test::bit("d6") |
                 othello::test::bit("f6"),
        .white = othello::test::bit("c4") | othello::test::bit("e4") | othello::test::bit("d5") |
                 othello::test::bit("e5"),
        .side_to_move = Side::Black,
    };
    const Square move = othello::test::square("d4");
    const Bitboard expected_flips = othello::test::bit("c4") | othello::test::bit("e4") |
                                    othello::test::bit("d5") | othello::test::bit("e5");

    CHECK(othello::flips_for_move(board, move) == expected_flips);

    const auto next = othello::apply_move(board, move);

    REQUIRE(next.has_value());
    CHECK((next->black & othello::test::bit("d4")) != 0);
    CHECK((next->black & expected_flips) == expected_flips);
    CHECK(othello::disc_count(*next, Side::Black) == 9);
    CHECK(othello::disc_count(*next, Side::White) == 0);
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("White can apply a legal move after the initial black move", "[rule-core]") {
    const auto after_black = othello::apply_move(Board::initial(), othello::test::square("d3"));

    REQUIRE(after_black.has_value());
    REQUIRE(after_black->side_to_move == Side::White);
    CHECK(othello::legal_moves(*after_black) != 0);
    CHECK((othello::legal_moves(*after_black) & othello::test::bit("c3")) != 0);

    const auto after_white = othello::apply_move(*after_black, othello::test::square("c3"));

    REQUIRE(after_white.has_value());
    CHECK(after_white->side_to_move == Side::Black);
    CHECK((after_white->black & after_white->white) == 0);
}

TEST_CASE("Edge horizontal move flips discs along an edge row", "[rule-core]") {
    const Board board = othello::test::board_from_text(R"(........
........
........
........
........
........
........
.WWWWWWB
side=B)");
    const Square move = othello::test::square("a1");
    const Bitboard expected_flips = othello::test::bit("b1") | othello::test::bit("c1") |
                                    othello::test::bit("d1") | othello::test::bit("e1") |
                                    othello::test::bit("f1") | othello::test::bit("g1");

    CAPTURE(othello::to_string(board), othello::to_string(move));

    CHECK(othello::flips_for_move(board, move) == expected_flips);

    const auto next = othello::apply_move(board, move);

    REQUIRE(next.has_value());
    CHECK(othello::disc_count(*next, Side::Black) == 8);
    CHECK(othello::disc_count(*next, Side::White) == 0);
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("Edge vertical move flips discs along an edge file", "[rule-core]") {
    const Board board = othello::test::board_from_text(R"(B.......
W.......
W.......
W.......
W.......
W.......
W.......
........
side=B)");
    const Square move = othello::test::square("a1");
    const Bitboard expected_flips = othello::test::bit("a2") | othello::test::bit("a3") |
                                    othello::test::bit("a4") | othello::test::bit("a5") |
                                    othello::test::bit("a6") | othello::test::bit("a7");

    CAPTURE(othello::to_string(board), othello::to_string(move));

    CHECK(othello::flips_for_move(board, move) == expected_flips);

    const auto next = othello::apply_move(board, move);

    REQUIRE(next.has_value());
    CHECK(othello::disc_count(*next, Side::Black) == 8);
    CHECK(othello::disc_count(*next, Side::White) == 0);
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("Corner move flips diagonally into the board", "[rule-core]") {
    const Board board = othello::test::board_from_text(R"(........
......W.
.....B..
........
........
........
........
........
side=B)");
    const Square move = othello::test::square("h8");
    const Bitboard expected_flips = othello::test::bit("g7");

    CAPTURE(othello::to_string(board), othello::to_string(move));

    CHECK((othello::legal_moves(board) & move.bit()) != 0);
    CHECK(othello::flips_for_move(board, move) == expected_flips);

    const auto next = othello::apply_move(board, move);

    REQUIRE(next.has_value());
    CHECK((next->black & othello::test::bit("h8")) != 0);
    CHECK((next->black & expected_flips) == expected_flips);
    CHECK(othello::disc_count(*next, Side::Black) == 3);
    CHECK(othello::disc_count(*next, Side::White) == 0);
}

TEST_CASE("Legal moves do not wrap around between h-file and a-file", "[rule-core]") {
    const Board h_to_a_board = othello::test::board_from_text(R"(........
........
........
........
........
........
........
WB......
side=B)");
    const Board a_to_h_board = othello::test::board_from_text(R"(........
........
........
........
........
........
........
......BW
side=B)");
    constexpr Bitboard expected_moves = 0;

    CAPTURE(othello::to_string(h_to_a_board));

    CHECK(othello::legal_moves(h_to_a_board) == expected_moves);
    CHECK(othello::flips_for_move(h_to_a_board, othello::test::square("h1")) == 0);

    CAPTURE(othello::to_string(a_to_h_board));

    CHECK(othello::legal_moves(a_to_h_board) == expected_moves);
    CHECK(othello::flips_for_move(a_to_h_board, othello::test::square("a1")) == 0);
}

TEST_CASE("Passing is rejected while the current side has legal moves", "[rule-core]") {
    const Board board = Board::initial();

    CHECK(othello::has_legal_move(board));
    CHECK_FALSE(othello::pass_turn(board).has_value());
}

TEST_CASE("A side with no legal move can pass to the opponent", "[rule-core]") {
    const Board board = othello::test::black_must_pass_board();

    REQUIRE_FALSE(othello::has_legal_move(board));

    const auto next = othello::pass_turn(board);

    REQUIRE(next.has_value());
    CHECK(next->black == board.black);
    CHECK(next->white == board.white);
    CHECK(next->side_to_move == Side::White);
    CHECK(othello::has_legal_move(*next));
    CHECK((othello::legal_moves(*next) & othello::test::bit("c4")) != 0);
}

TEST_CASE("Game over requires neither side to have a legal move", "[rule-core]") {
    const Board initial = Board::initial();
    const Board terminal{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    CHECK_FALSE(othello::is_game_over(initial));
    CHECK(othello::is_game_over(terminal));
    CHECK_FALSE(othello::pass_turn(terminal).has_value());
    CHECK(othello::disc_count(terminal, Side::Black) == 64);
    CHECK(othello::disc_count(terminal, Side::White) == 0);
}

TEST_CASE("Score is disc difference from the requested side", "[rule-core]") {
    const Board initial = Board::initial();
    const Board terminal{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    CHECK(othello::score(initial, Side::Black) == 0);
    CHECK(othello::score(initial, Side::White) == 0);
    CHECK(othello::score(terminal, Side::Black) == 64);
    CHECK(othello::score(terminal, Side::White) == -64);
}

TEST_CASE("Random legal playouts preserve rule-core invariants", "[rule-core]") {
    constexpr int playout_count = 10;
    constexpr int max_steps = 200;

    // Fixed seed keeps the smoke test deterministic in CI.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937 random_engine{20260516};

    for (int playout = 0; playout < playout_count; ++playout) {
        Board board = Board::initial();

        for (int step = 0; !othello::is_game_over(board); ++step) {
            const std::string board_text = othello::to_string(board);
            CAPTURE(playout, step, board_text);

            REQUIRE(step < max_steps);
            othello::test::require_board_invariants(board);

            const Bitboard moves = othello::legal_moves(board);
            if (moves != 0) {
                const std::vector<Square> legal_squares =
                    othello::test::squares_from_bitboard(moves);
                REQUIRE_FALSE(legal_squares.empty());

                std::uniform_int_distribution<std::size_t> distribution{0,
                                                                        legal_squares.size() - 1};
                const Square move = legal_squares[distribution(random_engine)];
                const auto next = othello::apply_move(board, move);

                REQUIRE(next.has_value());
                board = *next;
            } else {
                const auto next = othello::pass_turn(board);

                REQUIRE(next.has_value());
                CHECK(next->black == board.black);
                CHECK(next->white == board.white);
                CHECK(next->side_to_move == othello::opponent(board.side_to_move));
                board = *next;
            }

            othello::test::require_board_invariants(board);
        }

        const std::string final_board_text = othello::to_string(board);
        CAPTURE(playout, final_board_text);

        Board opponent_board = board;
        opponent_board.side_to_move = othello::opponent(board.side_to_move);

        CHECK(othello::is_game_over(board));
        CHECK(othello::legal_moves(board) == 0);
        CHECK(othello::legal_moves(opponent_board) == 0);
        CHECK(othello::disc_count(board, Side::Black) + othello::disc_count(board, Side::White) <=
              64);
        CHECK(othello::score(board, Side::Black) == -othello::score(board, Side::White));
    }
}
