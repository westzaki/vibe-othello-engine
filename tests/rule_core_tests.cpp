#include <array>
#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>
#include <random>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] Board black_must_pass_board() {
    return Board{
        .black = bit("d4"),
        .white = bit("e4") | bit("f4") | bit("g4") | bit("h4"),
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] bool same_board(const Board& lhs, const Board& rhs) noexcept {
    return lhs.black == rhs.black && lhs.white == rhs.white && lhs.side_to_move == rhs.side_to_move;
}

[[nodiscard]] std::vector<Square> squares_from_bitboard(Bitboard bits) {
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

void require_board_invariants(const Board& board) {
    CHECK((board.black & board.white) == 0);
    CHECK(othello::disc_count(board, Side::Black) + othello::disc_count(board, Side::White) <= 64);

    const std::string text = othello::to_string(board);
    const auto parsed = othello::board_from_string(text);

    REQUIRE(parsed.has_value());
    CHECK(same_board(*parsed, board));
}

constexpr std::string_view initial_board_text = R"(........
........
........
...BW...
...WB...
........
........
........
side=B)";

} // namespace

TEST_CASE("Initial board has starting discs and black to move", "[rule-core]") {
    const Board board = Board::initial();

    CHECK(othello::disc_count(board, Side::Black) == 2);
    CHECK(othello::disc_count(board, Side::White) == 2);
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

    CHECK(othello::has_legal_move(board));
    CHECK(othello::legal_moves(board) == expected);
}

TEST_CASE("Initial black move d3 places and flips discs", "[rule-core]") {
    const auto next = othello::apply_move(Board::initial(), square("d3"));

    REQUIRE(next.has_value());
    CHECK((next->black & bit("d3")) != 0);
    CHECK((next->black & bit("d4")) != 0);
    CHECK(othello::disc_count(*next, Side::Black) == 4);
    CHECK(othello::disc_count(*next, Side::White) == 1);
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("Each initial black legal move leaves four black discs and one white disc",
          "[rule-core]") {
    constexpr std::array moves{"d3", "c4", "f5", "e6"};

    for (const std::string_view move : moves) {
        CAPTURE(std::string{move});

        const auto next = othello::apply_move(Board::initial(), square(move));

        REQUIRE(next.has_value());
        CHECK(othello::disc_count(*next, Side::Black) == 4);
        CHECK(othello::disc_count(*next, Side::White) == 1);
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
    CHECK(othello::disc_count(*next, Side::Black) == 9);
    CHECK(othello::disc_count(*next, Side::White) == 0);
    CHECK(next->side_to_move == Side::White);
}

TEST_CASE("Passing is rejected while the current side has legal moves", "[rule-core]") {
    const Board board = Board::initial();

    CHECK(othello::has_legal_move(board));
    CHECK_FALSE(othello::pass_turn(board).has_value());
}

TEST_CASE("A side with no legal move can pass to the opponent", "[rule-core]") {
    const Board board = black_must_pass_board();

    REQUIRE_FALSE(othello::has_legal_move(board));

    const auto next = othello::pass_turn(board);

    REQUIRE(next.has_value());
    CHECK(next->black == board.black);
    CHECK(next->white == board.white);
    CHECK(next->side_to_move == Side::White);
    CHECK(othello::has_legal_move(*next));
    CHECK((othello::legal_moves(*next) & bit("c4")) != 0);
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

TEST_CASE("Initial board text parses to the initial board", "[rule-core]") {
    const auto board = othello::board_from_string(initial_board_text);

    REQUIRE(board.has_value());
    CHECK(same_board(*board, Board::initial()));
}

TEST_CASE("Initial board formats to text", "[rule-core]") {
    CHECK(othello::to_string(Board::initial()) == std::string{initial_board_text});
}

TEST_CASE("Board formatting and parsing round-trips a non-initial board", "[rule-core]") {
    const auto board = othello::apply_move(Board::initial(), square("d3"));

    REQUIRE(board.has_value());

    const std::string text = othello::to_string(*board);
    const auto parsed = othello::board_from_string(text);

    REQUIRE(parsed.has_value());
    CHECK(same_board(*parsed, *board));
}

TEST_CASE("Malformed board strings are rejected", "[rule-core]") {
    constexpr std::array malformed{
        R"(........
........
........
...BW...
...WB...
........
........
side=B)",
        R"(........
........
........
...BW...
...WB...
........
........
........
........
side=B)",
        R"(.......
........
........
...BW...
...WB...
........
........
........
side=B)",
        R"(........
........
........
...BX...
...WB...
........
........
........
side=B)",
        R"(........
........
........
...BW...
...WB...
........
........
........
side=Black)",
        R"(........
........
........
...BW...
...WB...
........
........
........)",
    };

    for (const std::string_view text : malformed) {
        CAPTURE(std::string{text});
        CHECK_FALSE(othello::board_from_string(text).has_value());
    }
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
            require_board_invariants(board);

            const Bitboard moves = othello::legal_moves(board);
            if (moves != 0) {
                const std::vector<Square> legal_squares = squares_from_bitboard(moves);
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

            require_board_invariants(board);
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
