#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

using othello::Bitboard;
using othello::Board;
using othello::Side;

TEST_CASE("Fixed-depth search at depth zero returns an evaluation-only result", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result = othello::search_fixed_depth(board, 0);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("Search options depth zero returns an evaluation-only result", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result =
        othello::search(board, othello::SearchOptions{.max_depth = 0});

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("Fixed-depth search treats negative depth as depth zero", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result = othello::search_fixed_depth(board, -1);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("Search options treat negative depth as depth zero", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result =
        othello::search(board, othello::SearchOptions{.max_depth = -1});

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("Iterative search depth zero returns an evaluation-only result", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result =
        othello::search_iterative(board, othello::SearchOptions{.max_depth = 0});

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("Iterative search treats negative depth as depth zero", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result =
        othello::search_iterative(board, othello::SearchOptions{.max_depth = -1});

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("Fixed-depth search returns a legal move from the initial board", "[search]") {
    const Board board = Board::initial();
    const Bitboard legal_moves = othello::legal_moves(board);

    const othello::SearchResult result = othello::search_fixed_depth(board, 1);

    REQUIRE(result.best_move.has_value());
    CHECK((legal_moves & result.best_move->bit()) != 0);
    REQUIRE(result.principal_variation.size() == 1);
    CHECK(result.principal_variation.front() == *result.best_move);
    CHECK(result.depth == 1);
    CHECK(result.nodes > 1);
}

TEST_CASE("Search stats node count mirrors compatibility node field", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result =
        othello::search(board, othello::SearchOptions{.max_depth = 4});

    CHECK(result.nodes > 0);
    CHECK(result.stats.nodes == result.nodes);
}

TEST_CASE("Fixed-depth compatibility delegates to options-based search", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult fixed_depth = othello::search_fixed_depth(board, 3);
    const othello::SearchResult options =
        othello::search(board, othello::SearchOptions{.max_depth = 3});

    CHECK(fixed_depth.best_move == options.best_move);
    CHECK(fixed_depth.score == options.score);
    CHECK(fixed_depth.depth == options.depth);
}

TEST_CASE("Iterative search at depth one matches fixed-depth search", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult fixed_depth =
        othello::search(board, othello::SearchOptions{.max_depth = 1});
    const othello::SearchResult iterative =
        othello::search_iterative(board, othello::SearchOptions{.max_depth = 1});

    CHECK(fixed_depth.best_move == iterative.best_move);
    CHECK(fixed_depth.score == iterative.score);
    CHECK(fixed_depth.depth == iterative.depth);
    CHECK(iterative.nodes > 0);
}

TEST_CASE("Fixed-depth search reports a deterministic principal variation", "[search]") {
    const Board board = Board::initial();
    const othello::SearchOptions options{
        .max_depth = 3,
        .use_transposition_table = false,
    };

    const othello::SearchResult first = othello::search(board, options);
    const othello::SearchResult second = othello::search(board, options);

    REQUIRE(first.best_move.has_value());
    REQUIRE_FALSE(first.principal_variation.empty());
    CHECK(first.principal_variation.front() == *first.best_move);
    CHECK(first.principal_variation.size() <= 3);
    CHECK(first.principal_variation == second.principal_variation);
    CHECK(first.best_move == second.best_move);
    CHECK(first.score == second.score);
}

TEST_CASE("Iterative search with root preference matches fixed-depth search", "[search]") {
    const Board board = Board::initial();
    const othello::SearchOptions options{
        .max_depth = 3,
        .use_transposition_table = false,
    };

    const othello::SearchResult fixed_depth = othello::search(board, options);
    const othello::SearchResult iterative = othello::search_iterative(board, options);

    CHECK(fixed_depth.best_move == iterative.best_move);
    CHECK(fixed_depth.score == iterative.score);
    CHECK(fixed_depth.depth == iterative.depth);
    REQUIRE(iterative.best_move.has_value());
    REQUIRE_FALSE(iterative.principal_variation.empty());
    CHECK(iterative.principal_variation.front() == *iterative.best_move);
    CHECK(iterative.principal_variation.size() <= 3);
    CHECK(iterative.nodes > 0);
}

TEST_CASE("Iterative search with previous principal variation hint matches fixed-depth search",
          "[search]") {
    const Board board = Board::initial();
    const othello::SearchOptions options{
        .max_depth = 4,
        .use_transposition_table = false,
    };

    const othello::SearchResult fixed_depth = othello::search(board, options);
    const othello::SearchResult iterative = othello::search_iterative(board, options);

    CHECK(fixed_depth.best_move == iterative.best_move);
    CHECK(fixed_depth.score == iterative.score);
    CHECK(fixed_depth.depth == iterative.depth);
    REQUIRE(iterative.best_move.has_value());
    REQUIRE_FALSE(iterative.principal_variation.empty());
    CHECK(iterative.principal_variation.front() == *iterative.best_move);
    CHECK(iterative.principal_variation.size() <= 4);
}

TEST_CASE("Fixed-depth search is deterministic", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult first = othello::search_fixed_depth(board, 2);
    const othello::SearchResult second = othello::search_fixed_depth(board, 2);

    CHECK(first.best_move == second.best_move);
    CHECK(first.score == second.score);
    CHECK(first.depth == second.depth);
    CHECK(first.nodes == second.nodes);
    CHECK(first.principal_variation == second.principal_variation);
}

TEST_CASE("Iterative search without transposition table matches fixed-depth score", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 3,
        .use_transposition_table = false,
    };
    const othello::SearchResult fixed_depth = othello::search(*board, options);
    const othello::SearchResult iterative = othello::search_iterative(*board, options);

    CHECK(fixed_depth.best_move == iterative.best_move);
    CHECK(fixed_depth.score == iterative.score);
    CHECK(fixed_depth.depth == iterative.depth);
    CHECK(iterative.nodes > fixed_depth.nodes);
}

TEST_CASE("Iterative search with transposition table is deterministic", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 3,
        .use_transposition_table = true,
        .transposition_table_entries = 16,
    };
    const othello::SearchResult first = othello::search_iterative(*board, options);
    const othello::SearchResult second = othello::search_iterative(*board, options);

    CHECK(first.best_move == second.best_move);
    CHECK(first.score == second.score);
    CHECK(first.depth == second.depth);
    CHECK(first.nodes == second.nodes);
    CHECK(first.principal_variation == second.principal_variation);
    CHECK(first.nodes > 0);
}

TEST_CASE("Search options can enable the transposition table", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult default_without_tt =
        othello::search(*board, othello::SearchOptions{.max_depth = 4});
    const othello::SearchResult with_tt = othello::search(
        *board, othello::SearchOptions{.max_depth = 4, .use_transposition_table = true});

    CHECK(default_without_tt.best_move == with_tt.best_move);
    CHECK(default_without_tt.score == with_tt.score);
    CHECK(default_without_tt.depth == with_tt.depth);
}

TEST_CASE("Search stats leave transposition table counters zero when disabled", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult result =
        othello::search(*board, othello::SearchOptions{.max_depth = 4});

    CHECK(result.stats.nodes == result.nodes);
    CHECK(result.stats.tt_lookups == 0);
    CHECK(result.stats.tt_hits == 0);
    CHECK(result.stats.tt_exact_hits == 0);
    CHECK(result.stats.tt_lower_hits == 0);
    CHECK(result.stats.tt_upper_hits == 0);
    CHECK(result.stats.tt_stores == 0);
    CHECK(result.stats.tt_overwrites == 0);
    CHECK(result.stats.tt_collisions == 0);
}

TEST_CASE("Search stats count transposition table work when enabled", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult result = othello::search(
        *board, othello::SearchOptions{.max_depth = 4, .use_transposition_table = true});

    CHECK(result.stats.nodes == result.nodes);
    CHECK(result.stats.tt_lookups > 0);
    CHECK(result.stats.tt_stores > 0);
    CHECK(result.stats.tt_hits <= result.stats.tt_lookups);
    CHECK(result.stats.tt_exact_hits + result.stats.tt_lower_hits + result.stats.tt_upper_hits ==
          result.stats.tt_hits);
    CHECK(result.stats.tt_collisions <= result.stats.tt_overwrites);
}

TEST_CASE("Search options accept small transposition table sizes", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult default_size = othello::search(
        *board, othello::SearchOptions{.max_depth = 4, .use_transposition_table = true});
    const othello::SearchResult small_size =
        othello::search(*board, othello::SearchOptions{.max_depth = 4,
                                                       .use_transposition_table = true,
                                                       .transposition_table_entries = 16});

    CHECK(default_size.best_move == small_size.best_move);
    CHECK(default_size.score == small_size.score);
    CHECK(default_size.depth == small_size.depth);
}

TEST_CASE("Non-power-of-two transposition table sizes are accepted", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult default_size = othello::search(
        *board, othello::SearchOptions{.max_depth = 4, .use_transposition_table = true});
    const othello::SearchResult non_power_of_two =
        othello::search(*board, othello::SearchOptions{.max_depth = 4,
                                                       .use_transposition_table = true,
                                                       .transposition_table_entries = 17});

    CHECK(default_size.best_move == non_power_of_two.best_move);
    CHECK(default_size.score == non_power_of_two.score);
    CHECK(default_size.depth == non_power_of_two.depth);
}

TEST_CASE("Zero transposition table entries disables the table", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult disabled = othello::search(
        *board, othello::SearchOptions{.max_depth = 4, .use_transposition_table = false});
    const othello::SearchResult zero_entries = othello::search(
        *board, othello::SearchOptions{.max_depth = 4, .transposition_table_entries = 0});

    CHECK(disabled.best_move == zero_entries.best_move);
    CHECK(disabled.score == zero_entries.score);
    CHECK(disabled.depth == zero_entries.depth);
}

TEST_CASE("Fixed-depth search remains deterministic at deeper depths", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult first = othello::search_fixed_depth(*board, 4);
    const othello::SearchResult second = othello::search_fixed_depth(*board, 4);

    CHECK(first.best_move == second.best_move);
    CHECK(first.score == second.score);
    CHECK(first.depth == second.depth);
    CHECK(first.nodes == second.nodes);
}

TEST_CASE("Search stats count dynamic move ordering work", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result =
        othello::search(board, othello::SearchOptions{.max_depth = 5});

    CHECK(result.stats.nodes == result.nodes);
    CHECK(result.stats.dynamic_ordering_nodes > 0);
    CHECK(result.stats.dynamic_ordering_moves >= result.stats.dynamic_ordering_nodes * 5);
}

TEST_CASE("Fixed-depth search handles terminal boards", "[search]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    const othello::SearchResult result = othello::search_fixed_depth(board, 3);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 3);
    CHECK(result.nodes > 0);
}

TEST_CASE("Fixed-depth search handles pass positions", "[search]") {
    const Board board = othello::test::black_must_pass_board();

    REQUIRE(othello::legal_moves(board) == 0);
    REQUIRE_FALSE(othello::is_game_over(board));

    const othello::SearchResult result = othello::search_fixed_depth(board, 2);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.size() <= 2);
    if (!result.principal_variation.empty()) {
        const auto passed = othello::pass_turn(board);
        REQUIRE(passed.has_value());
        CHECK((othello::legal_moves(*passed) & result.principal_variation.front().bit()) != 0);
    }
    CHECK(result.depth == 2);
    CHECK(result.nodes > 0);
}

TEST_CASE("Iterative search handles pass positions without fake principal variation moves",
          "[search]") {
    const Board board = othello::test::black_must_pass_board();

    REQUIRE(othello::legal_moves(board) == 0);
    REQUIRE_FALSE(othello::is_game_over(board));

    const othello::SearchOptions options{
        .max_depth = 3,
        .use_transposition_table = false,
    };
    const othello::SearchResult fixed_depth = othello::search(board, options);
    const othello::SearchResult iterative = othello::search_iterative(board, options);

    CHECK_FALSE(iterative.best_move.has_value());
    CHECK(fixed_depth.score == iterative.score);
    CHECK(iterative.depth == 3);
    CHECK(iterative.principal_variation.size() <= 3);
    if (!iterative.principal_variation.empty()) {
        const auto passed = othello::pass_turn(board);
        REQUIRE(passed.has_value());
        CHECK((othello::legal_moves(*passed) & iterative.principal_variation.front().bit()) != 0);
    }
    CHECK(iterative.nodes > 0);
}
