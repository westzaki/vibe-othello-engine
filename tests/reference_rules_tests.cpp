#include "reference_rules.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using othello::Bitboard;
using othello::Board;
using othello::Side;
using othello::Square;

namespace {

struct NamedBoard {
    std::string_view name;
    Board board;
};

struct ProductionRules {
    [[nodiscard]] static Bitboard legal_moves(const Board& board) noexcept {
        return othello::legal_moves(board);
    }

    [[nodiscard]] static std::optional<Board> apply_move(const Board& board,
                                                         Square square) noexcept {
        return othello::apply_move(board, square);
    }

    [[nodiscard]] static std::optional<Board> pass_turn(const Board& board) noexcept {
        return othello::pass_turn(board);
    }

    [[nodiscard]] static bool is_game_over(const Board& board) noexcept {
        return othello::is_game_over(board);
    }
};

struct ReferenceRules {
    [[nodiscard]] static Bitboard legal_moves(const Board& board) noexcept {
        return othello::test::reference::legal_moves(board);
    }

    [[nodiscard]] static std::optional<Board> apply_move(const Board& board,
                                                         Square square) noexcept {
        return othello::test::reference::apply_move(board, square);
    }

    [[nodiscard]] static std::optional<Board> pass_turn(const Board& board) noexcept {
        return othello::test::reference::pass_turn(board);
    }

    [[nodiscard]] static bool is_game_over(const Board& board) noexcept {
        return othello::test::reference::is_game_over(board);
    }
};

[[nodiscard]] Square square_from_index_unchecked(int index) {
    const auto square = Square::from_index(index);
    REQUIRE(square.has_value());
    return *square;
}

void check_optional_board_matches(const std::optional<Board>& production,
                                  const std::optional<Board>& reference) {
    CHECK(production.has_value() == reference.has_value());

    if (production.has_value() && reference.has_value()) {
        CHECK(othello::test::same_board(*production, *reference));
        othello::test::require_board_invariants(*production);
    }
}

void check_board_matches_reference(const Board& board) {
    CAPTURE(othello::to_string(board));

    const Bitboard production_moves = othello::legal_moves(board);
    const Bitboard reference_moves = othello::test::reference::legal_moves(board);
    CHECK(production_moves == reference_moves);
    CHECK(othello::has_legal_move(board) == (reference_moves != 0));
    CHECK(othello::is_game_over(board) == othello::test::reference::is_game_over(board));

    for (const Side side : {Side::Black, Side::White}) {
        CHECK(othello::disc_count(board, side) ==
              othello::test::reference::disc_count(board, side));
        CHECK(othello::score(board, side) == othello::test::reference::score(board, side));
    }

    check_optional_board_matches(othello::pass_turn(board),
                                 othello::test::reference::pass_turn(board));

    // Reachable Othello positions can have 33 legal moves, so validation scans every square
    // instead of relying on a <=32 move buffer.
    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Square square = square_from_index_unchecked(index);
        CAPTURE(index, othello::to_string(square));

        CHECK(othello::flips_for_move(board, square) ==
              othello::test::reference::flips_for_move(board, square));
        check_optional_board_matches(othello::apply_move(board, square),
                                     othello::test::reference::apply_move(board, square));
    }
}

[[nodiscard]] std::vector<NamedBoard> fixed_reference_boards() {
    return {
        {.name = "initial", .board = Board::initial()},
        {.name = "edge-horizontal", .board = othello::test::board_from_text(R"(........
........
........
........
........
........
........
.WWWWWWB
side=B)")},
        {.name = "edge-vertical", .board = othello::test::board_from_text(R"(B.......
W.......
W.......
W.......
W.......
W.......
W.......
........
side=B)")},
        {.name = "corner-diagonal", .board = othello::test::board_from_text(R"(........
......W.
.....B..
........
........
........
........
........
side=B)")},
        {.name = "h-file-a-file-wrap-guard", .board = othello::test::board_from_text(R"(........
........
........
........
........
........
........
WB......
side=B)")},
        {.name = "a-file-h-file-wrap-guard", .board = othello::test::board_from_text(R"(........
........
........
........
........
........
........
......BW
side=B)")},
        {.name = "multi-direction",
         .board =
             Board{
                 .black = othello::test::bit("b4") | othello::test::bit("f4") |
                          othello::test::bit("d6") | othello::test::bit("f6"),
                 .white = othello::test::bit("c4") | othello::test::bit("e4") |
                          othello::test::bit("d5") | othello::test::bit("e5"),
                 .side_to_move = Side::Black,
             }},
        {.name = "x-square-adjacent-empty-corners",
         .board = othello::test::board_from_text(R"(........
.W.B....
..W.....
...B....
....W...
.....B..
......W.
........
side=B)")},
        {.name = "root-pass", .board = othello::test::black_must_pass_board()},
        {.name = "terminal-all-black",
         .board = Board{.black = ~Bitboard{0}, .white = 0, .side_to_move = Side::Black}},
        {.name = "14-empty-high-mobility", .board = othello::test::board_from_text(R"(.WWWW.W.
BWBWWWWW
BBWWWBW.
BBBBBWWW
BWBWBBWW
BWWBBWWW
.W..WBW.
...W..W.
side=B)")},
        {.name = "18-empty-corner-race", .board = othello::test::board_from_text(R"(.B...WWW
.WBWWWBW
..BBWBWB
.WWWWWBB
.WWWWBBB
WWBWB..B
WWWBWW..
.W.BB...
side=B)")},
        {.name = "20-empty-high-mobility-lite", .board = othello::test::board_from_text(R"(WWW.BBB.
.B.WWWWW
BBWBBBBW
.WWWWBW.
.BBWBWWW
..BB.WWW
..BBBW.W
..B.....
side=B)")},
        {.name = "20-empty-root-pass", .board = othello::test::board_from_text(R"(........
W....B..
WWW.BB..
WWWWBB..
WWWWWBBB
WWWWWWBB
WWWWWWW.
WWWWWWWW
side=B)")},
    };
}

[[nodiscard]] std::vector<NamedBoard> perft_boards() {
    return {
        {.name = "initial", .board = Board::initial()},
        {.name = "edge-horizontal", .board = othello::test::board_from_text(R"(........
........
........
........
........
........
........
.WWWWWWB
side=B)")},
        {.name = "root-pass", .board = othello::test::black_must_pass_board()},
        {.name = "14-empty-high-mobility", .board = othello::test::board_from_text(R"(.WWWW.W.
BWBWWWWW
BBWWWBW.
BBBBBWWW
BWBWBBWW
BWWBBWWW
.W..WBW.
...W..W.
side=B)")},
        {.name = "18-empty-corner-race", .board = othello::test::board_from_text(R"(.B...WWW
.WBWWWBW
..BBWBWB
.WWWWWBB
.WWWWBBB
WWBWB..B
WWWBWW..
.W.BB...
side=B)")},
        {.name = "20-empty-high-mobility-lite", .board = othello::test::board_from_text(R"(WWW.BBB.
.B.WWWWW
BBWBBBBW
.WWWWBW.
.BBWBWWW
..BB.WWW
..BBBW.W
..B.....
side=B)")},
    };
}

template <typename Rules>
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] std::uint64_t perft(const Board& board, int depth) {
    if (depth == 0 || Rules::is_game_over(board)) {
        return 1;
    }

    const Bitboard moves = Rules::legal_moves(board);
    if (moves == 0) {
        const auto passed = Rules::pass_turn(board);
        REQUIRE(passed.has_value());
        return perft<Rules>(*passed, depth - 1);
    }

    std::uint64_t total = 0;
    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const Square square = square_from_index_unchecked(index);
        if ((moves & square.bit()) == 0) {
            continue;
        }

        const auto next = Rules::apply_move(board, square);
        REQUIRE(next.has_value());
        total += perft<Rules>(*next, depth - 1);
    }

    return total;
}

void check_perft_matches(std::string_view name, const Board& board, int max_depth) {
    for (int depth = 0; depth <= max_depth; ++depth) {
        CAPTURE(std::string{name}, depth, othello::to_string(board));
        CHECK(perft<ProductionRules>(board, depth) == perft<ReferenceRules>(board, depth));
    }
}

} // namespace

TEST_CASE("Reference rules match production on fixed edge, pass, terminal, and endgame boards",
          "[rule-core][reference]") {
    for (const NamedBoard& board : fixed_reference_boards()) {
        CAPTURE(std::string{board.name});
        check_board_matches_reference(board.board);
    }
}

TEST_CASE("Seeded random legal playout positions match reference rules", "[rule-core][reference]") {
    constexpr int target_positions = 1000;
    constexpr int max_steps = 200;
    int checked_positions = 0;

    // Fixed seed keeps the broad reference check deterministic in CI.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937 random_engine{20260523};

    for (int playout = 0; checked_positions < target_positions; ++playout) {
        Board board = Board::initial();

        for (int step = 0; step < max_steps && checked_positions < target_positions; ++step) {
            CAPTURE(playout, step, checked_positions);

            if (step % 3 == 0 || othello::is_game_over(board)) {
                check_board_matches_reference(board);
                ++checked_positions;
            }

            if (othello::is_game_over(board)) {
                break;
            }

            const Bitboard moves = othello::legal_moves(board);
            if (moves != 0) {
                const std::vector<Square> legal_squares =
                    othello::test::squares_from_bitboard(moves);
                REQUIRE_FALSE(legal_squares.empty());

                std::uniform_int_distribution<std::size_t> distribution{0,
                                                                        legal_squares.size() - 1};
                const auto next =
                    othello::apply_move(board, legal_squares[distribution(random_engine)]);

                REQUIRE(next.has_value());
                board = *next;
            } else {
                const auto next = othello::pass_turn(board);

                REQUIRE(next.has_value());
                board = *next;
            }
        }
    }

    CHECK(checked_positions == target_positions);
}

TEST_CASE("Production and reference perft agree on selected positions",
          "[rule-core][reference][perft]") {
    // Perft counts plies. A forced pass is counted as the only legal action for that ply;
    // terminal positions before depth zero return one leaf.
    check_perft_matches("initial", Board::initial(), 5);

    for (const NamedBoard& board : perft_boards()) {
        CAPTURE(std::string{board.name});
        check_perft_matches(board.name, board.board, 3);
    }
}
