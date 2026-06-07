#include "../src/endgame_ordering.hpp"
#include "../src/search_common.hpp"
#include "test_helpers.hpp"

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <optional>
#include <othello/othello.hpp>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using othello::Board;
using othello::Side;

namespace {

struct ExactRegressionCase {
    std::string_view benchmark_name;
    std::string_view board_text;
    int empties = 0;
    std::optional<std::string_view> best_move;
    int disc_margin = 0;
};

[[nodiscard]] std::optional<othello::Square>
expected_best_move(std::optional<std::string_view> coordinate) {
    if (!coordinate.has_value()) {
        return std::nullopt;
    }

    return othello::test::square(*coordinate);
}

void check_exact_regression_case(const ExactRegressionCase& test_case) {
    const std::string benchmark_name{test_case.benchmark_name};
    CAPTURE(benchmark_name);

    const Board board = othello::test::board_from_text(test_case.board_text);
    const std::optional<othello::Square> expected_move = expected_best_move(test_case.best_move);

    const othello::ExactEndgameResult first = othello::solve_exact_endgame(board);
    const othello::ExactEndgameResult second = othello::solve_exact_endgame(board);
    const othello::ExactEndgameResult disabled_tt = othello::solve_exact_endgame(
        board, othello::ExactEndgameOptions{.transposition_table_entries = 0});

    CHECK(first.empties == test_case.empties);
    CHECK(first.best_move == expected_move);
    CHECK(first.disc_margin == test_case.disc_margin);
    CHECK(first.nodes > 0);
    CHECK(first.principal_variation.size() <= static_cast<std::size_t>(first.empties));

    CHECK(second.empties == first.empties);
    CHECK(second.best_move == first.best_move);
    CHECK(second.disc_margin == first.disc_margin);
    CHECK(second.principal_variation == first.principal_variation);

    CHECK(disabled_tt.empties == first.empties);
    CHECK(disabled_tt.best_move == first.best_move);
    CHECK(disabled_tt.disc_margin == first.disc_margin);
    CHECK(disabled_tt.stats.tt_lookups == 0);
    CHECK(disabled_tt.stats.tt_stores == 0);

    if (expected_move.has_value()) {
        REQUIRE_FALSE(first.principal_variation.empty());
        CHECK(first.principal_variation.front() == *expected_move);
    }
}

void check_same_exact_result(const othello::ExactEndgameResult& actual,
                             const othello::ExactEndgameResult& expected) {
    CHECK(actual.best_move == expected.best_move);
    CHECK(actual.disc_margin == expected.disc_margin);
    CHECK(actual.empties == expected.empties);
    CHECK(actual.principal_variation == expected.principal_variation);
}

[[nodiscard]] othello::search_detail::NodeResult
solve_generic_exact_reference(const othello::search_detail::SearchPosition& position) {
    using othello::search_detail::flips_for_move;
    using othello::search_detail::is_better_best_move;
    using othello::search_detail::legal_moves;
    using othello::search_detail::NodeResult;
    using othello::search_detail::position_after_move;
    using othello::search_detail::position_after_pass;
    using othello::search_detail::principal_variation_with_move;
    using othello::search_detail::PrincipalVariation;
    using othello::search_detail::score_for_player;

    const othello::Bitboard moves = legal_moves(position);
    if (moves == 0) {
        const auto next = position_after_pass(position);
        if (legal_moves(next) == 0) {
            return NodeResult{.score = score_for_player(position)};
        }

        const NodeResult child = solve_generic_exact_reference(next);
        return NodeResult{
            .score = -child.score,
            .principal_variation = child.principal_variation,
        };
    }

    std::optional<int> best_score;
    std::optional<othello::Square> best_move;
    PrincipalVariation best_principal_variation;

    othello::Bitboard remaining_moves = moves;
    while (remaining_moves != 0) {
        const int index = std::countr_zero(remaining_moves);
        remaining_moves &= remaining_moves - 1;

        const std::optional<othello::Square> square = othello::Square::from_index(index);
        REQUIRE(square.has_value());

        const othello::Bitboard flips = flips_for_move(position, *square);
        if (flips == 0) {
            continue;
        }

        const NodeResult child =
            solve_generic_exact_reference(position_after_move(position, *square, flips));
        const int candidate_score = -child.score;
        if (is_better_best_move(candidate_score, *square, best_score, best_move)) {
            best_score = candidate_score;
            best_move = square;
            best_principal_variation =
                principal_variation_with_move(*square, child.principal_variation);
        }
    }

    return NodeResult{
        .best_move = best_move,
        .score = best_score.value_or(score_for_player(position)),
        .principal_variation = best_principal_variation,
    };
}

[[nodiscard]] std::vector<othello::Square> legal_squares(const Board& board) {
    return othello::test::squares_from_bitboard(othello::legal_moves(board));
}

[[nodiscard]] Board random_near_end_board(int target_empties, std::mt19937& random_engine) {
    Board board = Board::initial();
    for (int step = 0; step < 128 && std::popcount(board.empty()) > target_empties; ++step) {
        const std::vector<othello::Square> moves = legal_squares(board);
        if (!moves.empty()) {
            std::uniform_int_distribution<std::size_t> distribution(0, moves.size() - 1);
            const auto next = othello::apply_move(board, moves[distribution(random_engine)]);
            REQUIRE(next.has_value());
            board = *next;
            continue;
        }

        const auto passed = othello::pass_turn(board);
        if (!passed.has_value()) {
            break;
        }
        board = *passed;
    }
    return board;
}

void check_last_1_or_2_matches_generic_reference(const Board& board) {
    CAPTURE(othello::to_string(board));
    REQUIRE((std::popcount(board.empty()) == 1 || std::popcount(board.empty()) == 2));

    const auto position = othello::search_detail::SearchPosition::from_board(board);
    const othello::ExactEndgameResult specialized = othello::solve_exact_endgame(board);
    const othello::search_detail::NodeResult generic = solve_generic_exact_reference(position);

    CHECK(specialized.best_move == generic.best_move);
    CHECK(specialized.disc_margin == generic.score);
    CHECK(specialized.principal_variation ==
          othello::search_detail::principal_variation_to_vector(generic.principal_variation));
}

} // namespace

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

TEST_CASE("Exact endgame stats mirror node count and leave TT counters zero when exact TT is not "
          "allocated for tiny tails",
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
    CHECK(result.stats.tt_move_ordering_probes == 0);
    CHECK(result.stats.tt_move_ordering_hits == 0);
    CHECK(result.stats.tt_move_ordering_used == 0);
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
    CHECK(result.stats.tt_move_ordering_probes > 0);
    CHECK(result.stats.tt_move_ordering_hits > 0);
    CHECK(result.stats.tt_move_ordering_used > 0);
    CHECK(result.stats.tt_move_ordering_hits <= result.stats.tt_move_ordering_probes);
    CHECK(result.stats.tt_move_ordering_used <= result.stats.tt_move_ordering_hits);
}

TEST_CASE("Exact endgame stats aggregation covers every counter field", "[endgame]") {
    constexpr std::size_t field_count = sizeof(othello::ExactEndgameStats) / sizeof(std::uint64_t);
    CHECK(othello::exact_endgame_stats_detail::tracked_member_count == field_count);

    othello::ExactEndgameStats total{.nodes = 10, .tt_lookups = 4, .tt_hits = 2};
    const othello::ExactEndgameStats stats{
        .nodes = 5,
        .tt_lookups = 3,
        .tt_hits = 1,
        .tt_move_ordering_used = 6,
    };

    othello::accumulate_exact_endgame_stats(total, stats);

    CHECK(total.nodes == 15);
    CHECK(total.tt_lookups == 7);
    CHECK(total.tt_hits == 3);
    CHECK(total.tt_move_ordering_used == 6);
}

TEST_CASE("Exact endgame TT sizing options preserve exact result", "[endgame]") {
    const Board board = othello::test::board_from_text(R"(...B....
WWBWWW.B
WWWWWWWW
WBWWWWW.
WWWBBWBB
WBBBWB.B
WWBWBWBB
WWWWWWWB
side=B)");

    const othello::ExactEndgameResult default_size = othello::solve_exact_endgame(board);
    const othello::ExactEndgameResult larger_size = othello::solve_exact_endgame(
        board, othello::ExactEndgameOptions{.transposition_table_entries = std::size_t{1} << 16});
    const othello::ExactEndgameResult disabled_tt = othello::solve_exact_endgame(
        board, othello::ExactEndgameOptions{.transposition_table_entries = 0});
    const othello::ExactEndgameResult oversized_size = othello::solve_exact_endgame(
        board, othello::ExactEndgameOptions{.transposition_table_entries =
                                                std::numeric_limits<std::size_t>::max()});

    CHECK(larger_size.best_move == default_size.best_move);
    CHECK(larger_size.disc_margin == default_size.disc_margin);
    CHECK(larger_size.principal_variation == default_size.principal_variation);

    CHECK(disabled_tt.best_move == default_size.best_move);
    CHECK(disabled_tt.disc_margin == default_size.disc_margin);
    CHECK(disabled_tt.principal_variation == default_size.principal_variation);
    CHECK(disabled_tt.stats.tt_lookups == 0);
    CHECK(disabled_tt.stats.tt_stores == 0);

    CHECK(oversized_size.best_move == default_size.best_move);
    CHECK(oversized_size.disc_margin == default_size.disc_margin);
    CHECK(oversized_size.principal_variation == default_size.principal_variation);
    CHECK(oversized_size.nodes == default_size.nodes);
    CHECK(oversized_size.stats.tt_lookups > 0);
    CHECK(oversized_size.stats.tt_stores > 0);
}

TEST_CASE("Exact endgame preserves legal, pass, and terminal outcomes with TT off", "[endgame]") {
    const Board current_side_has_moves = othello::test::board_from_text(R"(...B....
WWBWWW.B
WWWWWWWW
WBWWWWW.
WWWBBWBB
WBBBWB.B
WWBWBWBB
WWWWWWWB
side=B)");
    REQUIRE(othello::legal_moves(current_side_has_moves) != 0);

    const Board root_pass = othello::test::board_from_text(R"(........
BBBBBBBW
BBWWBBB.
BBBBBBBB
.BBBBWBB
..BBBWBB
..BBBWWW
..BBBBBW
side=B)");
    REQUIRE(othello::legal_moves(root_pass) == 0);
    REQUIRE(othello::pass_turn(root_pass).has_value());

    const Board terminal_with_empty_squares = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBB....
side=W)");
    REQUIRE(othello::legal_moves(terminal_with_empty_squares) == 0);
    REQUIRE_FALSE(othello::pass_turn(terminal_with_empty_squares).has_value());
    REQUIRE(othello::is_game_over(terminal_with_empty_squares));

    for (const Board& board : {current_side_has_moves, root_pass, terminal_with_empty_squares}) {
        const othello::ExactEndgameResult default_tt = othello::solve_exact_endgame(board);
        const othello::ExactEndgameResult disabled_tt = othello::solve_exact_endgame(
            board, othello::ExactEndgameOptions{.transposition_table_entries = 0});

        check_same_exact_result(disabled_tt, default_tt);
        CHECK(default_tt.stats.nodes == default_tt.nodes);
        CHECK(disabled_tt.stats.nodes == disabled_tt.nodes);
        CHECK(disabled_tt.stats.tt_lookups == 0);
        CHECK(disabled_tt.stats.tt_stores == 0);
    }

    const othello::ExactEndgameResult terminal =
        othello::solve_exact_endgame(terminal_with_empty_squares);
    CHECK_FALSE(terminal.best_move.has_value());
    CHECK(terminal.principal_variation.empty());
    CHECK(terminal.empties == 4);
    CHECK(terminal.disc_margin ==
          othello::score(terminal_with_empty_squares, terminal_with_empty_squares.side_to_move));
}

TEST_CASE("Exact endgame four-empty tail preserves legal pass and double-pass outcomes",
          "[endgame]") {
    const Board legal_move = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBW....
side=B)");
    REQUIRE(othello::legal_moves(legal_move) != 0);

    const Board root_pass = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBW....
side=W)");
    REQUIRE(othello::legal_moves(root_pass) == 0);
    REQUIRE(othello::pass_turn(root_pass).has_value());

    const Board double_pass = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBB....
side=W)");
    REQUIRE(othello::legal_moves(double_pass) == 0);
    REQUIRE_FALSE(othello::pass_turn(double_pass).has_value());
    REQUIRE(othello::is_game_over(double_pass));

    for (const Board& board : {legal_move, root_pass, double_pass}) {
        const othello::ExactEndgameResult default_tt = othello::solve_exact_endgame(board);
        const othello::ExactEndgameResult disabled_tt = othello::solve_exact_endgame(
            board, othello::ExactEndgameOptions{.transposition_table_entries = 0});

        CHECK(default_tt.empties == 4);
        CHECK(disabled_tt.best_move == default_tt.best_move);
        CHECK(disabled_tt.disc_margin == default_tt.disc_margin);
        CHECK(disabled_tt.stats.tt_lookups == 0);
        CHECK(disabled_tt.stats.tt_stores == 0);
    }
}

TEST_CASE("Exact endgame last-1 and last-2 tails match generic reference cases", "[endgame]") {
    const std::array boards{
        othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)"),
        othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBWB.
side=B)"),
        othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBB.
side=B)"),
        othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
.WBBBBW.
side=B)"),
        othello::test::board_from_text(R"(WWBBBBBB
.BBWWBBW
B.BWWBWW
BBBWBWBB
BBBBWBBB
BWBBBBWB
BBBBBBBB
BWBBBBBB
side=B)"),
        othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBB..
side=W)"),
        othello::test::board_from_text(R"(BWWWWWWW
BWWBWWWW
BWWWWWWW
.WWWBBWB
WWWWWWBB
WWBBBBBB
WWWWBWBB
.WWWWWWB
side=B)"),
    };

    for (const Board& board : boards) {
        check_last_1_or_2_matches_generic_reference(board);
    }
}

TEST_CASE("Exact endgame last-1 and last-2 tails match generic reference random near-end boards",
          "[endgame]") {
    std::mt19937 random_engine{20260607};

    for (int target_empties : {1, 2}) {
        int checked = 0;
        for (int attempt = 0; checked < 32 && attempt < 4096; ++attempt) {
            Board board = random_near_end_board(target_empties, random_engine);
            if (std::popcount(board.empty()) != target_empties) {
                continue;
            }

            check_last_1_or_2_matches_generic_reference(board);
            ++checked;
        }
        CHECK(checked == 32);
    }
}

TEST_CASE("Endgame empty-region parity ordering scores candidate regions", "[endgame]") {
    using othello::endgame_detail::default_move_ordering_params;
    using othello::endgame_detail::empty_region_parity_order_score;
    using othello::endgame_detail::empty_region_size_containing;
    using othello::endgame_detail::empty_region_sizes;

    // a1-b1 is even, d1-e1-f1 is odd, and h1 is a singleton.
    constexpr othello::Bitboard empty = (othello::Bitboard{1} << 0) | (othello::Bitboard{1} << 1) |
                                        (othello::Bitboard{1} << 3) | (othello::Bitboard{1} << 4) |
                                        (othello::Bitboard{1} << 5) | (othello::Bitboard{1} << 7);
    const auto region_sizes = empty_region_sizes(empty);

    CHECK(empty_region_size_containing(empty, 0) == 2);
    CHECK(empty_region_size_containing(empty, 4) == 3);
    CHECK(empty_region_size_containing(empty, 7) == 1);
    CHECK(empty_region_size_containing(empty, 2) == 0);
    CHECK(region_sizes.by_square[0] == 2);
    CHECK(region_sizes.by_square[4] == 3);
    CHECK(region_sizes.by_square[7] == 1);
    CHECK(region_sizes.by_square[2] == 0);

    CHECK(empty_region_parity_order_score(region_sizes.by_square[0],
                                          default_move_ordering_params) == -1'000);
    CHECK(empty_region_parity_order_score(region_sizes.by_square[4],
                                          default_move_ordering_params) == 4'000);
    CHECK(empty_region_parity_order_score(region_sizes.by_square[7],
                                          default_move_ordering_params) == 12'000);
    CHECK(empty_region_parity_order_score(region_sizes.by_square[2],
                                          default_move_ordering_params) == 0);
}

TEST_CASE("Exact endgame solver preserves selected PVS regression results", "[endgame]") {
    // Current exact solver expected results; do not update casually.
    constexpr std::array cases{
        ExactRegressionCase{
            // tools/positions/endgame_fixtures.hpp: 14-empty-corner-choice
            .benchmark_name = "14-empty-corner-choice",
            .board_text = R"(.BW.W...
WWWWWWWB
W.WBBBBB
WWBWWBB.
W.BWBWWB
.WWBBBW.
WWWWWWWW
.BB..WB.
side=B)",
            .empties = 14,
            .best_move = "a8",
            .disc_margin = 24,
        },
        ExactRegressionCase{
            // tools/positions/endgame_fixtures.hpp: 16-empty-corner-choice
            .benchmark_name = "16-empty-corner-choice",
            .board_text = R"(.WB.B.BW
.WWWWWWW
.WBWWWWB
W.WBWWW.
WWBBBW.W
WBBBB.W.
WBBBBB..
WBW...B.
side=B)",
            .empties = 16,
            .best_move = "d1",
            .disc_margin = -14,
        },
        ExactRegressionCase{
            // tools/positions/endgame_fixtures.hpp: 18-empty-normal-mobility
            .benchmark_name = "18-empty-normal-mobility",
            .board_text = R"(.B.W.BB.
..BBBBBB
B..WBWB.
BBBWWWW.
BBBWBWWB
..BBWBW.
.WBBBBWW
W...BBW.
side=B)",
            .empties = 18,
            .best_move = "h1",
            .disc_margin = -30,
        },
    };

    for (const ExactRegressionCase& test_case : cases) {
        check_exact_regression_case(test_case);
    }
}

TEST_CASE("Exact endgame solver preserves selected root-pass regression result", "[endgame]") {
    // Current exact solver expected result; do not update casually.
    const ExactRegressionCase test_case{
        // tools/positions/endgame_fixtures.hpp: 16-empty-root-pass
        .benchmark_name = "16-empty-root-pass",
        .board_text = R"(........
BBBBBBBW
BBWWBBB.
BBBBBBBB
.BBBBWBB
..BBBWBB
..BBBWWW
..BBBBBW
side=B)",
        .empties = 16,
        .best_move = std::nullopt,
        .disc_margin = -48,
    };

    const Board board = othello::test::board_from_text(test_case.board_text);
    REQUIRE(othello::legal_moves(board) == 0);
    const std::optional<Board> after_pass = othello::pass_turn(board);
    REQUIRE(after_pass.has_value());

    const othello::ExactEndgameResult first = othello::solve_exact_endgame(board);
    const othello::ExactEndgameResult second = othello::solve_exact_endgame(board);

    CHECK(first.empties == test_case.empties);
    CHECK_FALSE(first.best_move.has_value());
    CHECK(first.disc_margin == test_case.disc_margin);
    CHECK(first.nodes > 0);
    REQUIRE_FALSE(first.principal_variation.empty());
    CHECK(first.principal_variation.front() == othello::test::square("a3"));
    CHECK(first.principal_variation.size() <= static_cast<std::size_t>(first.empties));
    CHECK((othello::legal_moves(*after_pass) & first.principal_variation.front().bit()) != 0);

    CHECK(second.empties == first.empties);
    CHECK(second.best_move == first.best_move);
    CHECK(second.disc_margin == first.disc_margin);
    CHECK(second.principal_variation == first.principal_variation);
}
