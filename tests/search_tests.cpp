#include "test_helpers.hpp"

#include "common/stats.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

using othello::Bitboard;
using othello::Board;
using othello::Side;

namespace {

constexpr int exact_endgame_score_scale = 1'000;

[[nodiscard]] Board one_empty_forced_board() {
    return othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)");
}

[[nodiscard]] Board one_empty_root_pass_board() {
    return othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBWB.
side=B)");
}

[[nodiscard]] Board exact_stats_board() {
    return othello::test::board_from_text(R"(...B....
WWBWWW.B
WWWWWWWW
WBWWWWW.
WWWBBWBB
WBBBWB.B
WWBWBWBB
WWWWWWWB
side=B)");
}

} // namespace

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

TEST_CASE("Search options depth zero uses configured evaluator", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    othello::EvaluationConfig config;
    config.opening = othello::EvaluationFeatureWeights{.disc_difference = 7};
    config.midgame = othello::EvaluationFeatureWeights{.disc_difference = 7};
    config.late = othello::EvaluationFeatureWeights{.disc_difference = 7};

    const othello::SearchOptions options{
        .max_depth = 0,
        .exact_endgame_empty_threshold = 0,
        .evaluation_config_override = config,
    };

    const othello::SearchResult result = othello::search(*board, options);

    CHECK(result.score == othello::evaluate_with_config(*board, board->side_to_move, config));
    CHECK(result.score != othello::evaluate_basic(*board, board->side_to_move));
    CHECK(result.stats.eval_calls > 0);
}

TEST_CASE("Search options resolve the default evaluator config", "[search]") {
    const othello::SearchOptions options;

    CHECK(othello::resolve_evaluation_config(options) == othello::default_evaluation_config());
}

TEST_CASE("Search options depth zero uses preset-only evaluator selection", "[search]") {
    const Board board = othello::test::black_must_pass_board();
    REQUIRE_FALSE(othello::is_game_over(board));

    const othello::SearchOptions options{
        .max_depth = 0,
        .exact_endgame_empty_threshold = 0,
        .evaluation_preset = othello::EvaluationPreset::MobilityPlusSmoke,
    };
    const othello::EvaluationConfig smoke_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::MobilityPlusSmoke);

    const othello::SearchResult result = othello::search(board, options);

    CHECK(result.score == othello::evaluate_with_config(board, board.side_to_move, smoke_config));
    CHECK(result.score != othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.stats.eval_calls > 0);
}

TEST_CASE("Search options custom evaluator override wins over preset", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    othello::EvaluationConfig config;
    config.opening = othello::EvaluationFeatureWeights{.disc_difference = 7};
    config.midgame = othello::EvaluationFeatureWeights{.disc_difference = 7};
    config.late = othello::EvaluationFeatureWeights{.disc_difference = 7};

    const othello::SearchOptions options{
        .max_depth = 0,
        .exact_endgame_empty_threshold = 0,
        .evaluation_preset = othello::EvaluationPreset::MobilityPlusSmoke,
        .evaluation_config_override = config,
    };

    const othello::SearchResult result = othello::search(*board, options);

    CHECK(othello::resolve_evaluation_config(options) == config);
    CHECK(result.score == othello::evaluate_with_config(*board, board->side_to_move, config));
    CHECK(result.score != othello::evaluate_with_config(
                              *board, board->side_to_move,
                              othello::evaluation_config_for_preset(
                                  othello::EvaluationPreset::MobilityPlusSmoke)));
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

TEST_CASE("Search stats aggregation includes search observability counters", "[search]") {
    othello::SearchStats total{
        .nodes = 10,
        .beta_cutoffs = 2,
        .beta_cutoffs_first_move = 1,
        .searched_moves = 7,
        .legal_move_nodes = 3,
        .eval_calls = 4,
        .pass_nodes = 1,
        .game_over_nodes = 1,
        .aspiration_searches = 2,
        .aspiration_researches = 1,
        .aspiration_fail_lows = 1,
        .aspiration_fail_highs = 0,
        .aspiration_full_window_fallbacks = 1,
    };
    const othello::SearchStats stats{
        .nodes = 5,
        .beta_cutoffs = 3,
        .beta_cutoffs_first_move = 2,
        .searched_moves = 11,
        .legal_move_nodes = 6,
        .eval_calls = 9,
        .pass_nodes = 2,
        .game_over_nodes = 1,
        .aspiration_searches = 3,
        .aspiration_researches = 2,
        .aspiration_fail_lows = 1,
        .aspiration_fail_highs = 1,
        .aspiration_full_window_fallbacks = 0,
    };

    othello::tools::add_search_stats(total, stats);

    CHECK(total.nodes == 15);
    CHECK(total.beta_cutoffs == 5);
    CHECK(total.beta_cutoffs_first_move == 3);
    CHECK(total.searched_moves == 18);
    CHECK(total.legal_move_nodes == 9);
    CHECK(total.eval_calls == 13);
    CHECK(total.pass_nodes == 3);
    CHECK(total.game_over_nodes == 2);
    CHECK(total.aspiration_searches == 5);
    CHECK(total.aspiration_researches == 3);
    CHECK(total.aspiration_fail_lows == 2);
    CHECK(total.aspiration_fail_highs == 1);
    CHECK(total.aspiration_full_window_fallbacks == 1);
    CHECK(othello::tools::beta_cut_first_move_percentage(total) == 60.0);
}

TEST_CASE("Search observability counters are sane on simple searches", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult leaf =
        othello::search(board, othello::SearchOptions{.max_depth = 0});
    CHECK(leaf.stats.eval_calls > 0);
    CHECK(leaf.stats.legal_move_nodes == 0);
    CHECK(leaf.stats.searched_moves == 0);

    const othello::SearchResult depth_search =
        othello::search(board, othello::SearchOptions{.max_depth = 2});
    CHECK(depth_search.stats.legal_move_nodes > 0);
    CHECK(depth_search.stats.searched_moves > 0);
    CHECK(depth_search.stats.eval_calls > 0);
    CHECK(depth_search.stats.beta_cutoffs_first_move <= depth_search.stats.beta_cutoffs);
}

TEST_CASE("Search threshold disabled keeps depth-limited behavior for small endgames", "[search]") {
    const Board board = one_empty_forced_board();
    const othello::SearchOptions options{
        .max_depth = 0,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult result = othello::search(board, options);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("Search uses exact endgame at the root within threshold", "[search]") {
    const Board board = one_empty_forced_board();
    const othello::ExactEndgameResult exact = othello::solve_exact_endgame(board);
    const othello::SearchOptions options{
        .max_depth = 0,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 1,
        .use_pvs = true,
        .use_aspiration_window = true,
        .evaluation_preset = othello::EvaluationPreset::MobilityPlusSmoke,
    };

    const othello::SearchResult result = othello::search(board, options);

    CHECK(result.best_move == exact.best_move);
    CHECK(result.score == exact.disc_margin * exact_endgame_score_scale);
    CHECK(result.depth == exact.empties);
    CHECK(result.nodes == exact.nodes);
    CHECK(result.stats.nodes == result.nodes);
    CHECK(result.principal_variation == exact.principal_variation);
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
    CHECK(result.stats.pvs_scouts == 0);
    CHECK(result.stats.pvs_researches == 0);
    CHECK(result.stats.pvs_scout_cutoffs == 0);
    CHECK(result.stats.aspiration_searches == 0);
    CHECK(result.stats.aspiration_researches == 0);
    CHECK(result.stats.aspiration_fail_lows == 0);
    CHECK(result.stats.aspiration_fail_highs == 0);
    CHECK(result.stats.aspiration_full_window_fallbacks == 0);
    CHECK(result.stats.beta_cutoffs == 0);
    CHECK(result.stats.beta_cutoffs_first_move == 0);
    CHECK(result.stats.searched_moves == 0);
    CHECK(result.stats.legal_move_nodes == 0);
    CHECK(result.stats.eval_calls == 0);
    CHECK(result.stats.pass_nodes == 0);
    CHECK(result.stats.game_over_nodes == 0);
    CHECK(result.stats.dynamic_ordering_nodes == 0);
    CHECK(result.stats.dynamic_ordering_moves == 0);
}

TEST_CASE("Exact root search handles pass positions without fake PV moves", "[search]") {
    const Board board = one_empty_root_pass_board();
    REQUIRE(othello::legal_moves(board) == 0);
    const auto after_pass = othello::pass_turn(board);
    REQUIRE(after_pass.has_value());

    const othello::SearchResult result =
        othello::search(board, othello::SearchOptions{.exact_endgame_empty_threshold = 1});

    CHECK_FALSE(result.best_move.has_value());
    REQUIRE_FALSE(result.principal_variation.empty());
    CHECK((othello::legal_moves(*after_pass) & result.principal_variation.front().bit()) != 0);
    CHECK(result.depth == 1);
    CHECK(result.nodes == result.stats.nodes);
}

TEST_CASE("Exact root search exposes exact solver TT stats", "[search]") {
    const Board board = exact_stats_board();
    const othello::ExactEndgameResult exact = othello::solve_exact_endgame(board);
    REQUIRE(exact.stats.tt_lookups > 0);
    REQUIRE(exact.stats.tt_stores > 0);
    REQUIRE(exact.stats.tt_move_ordering_used > 0);

    const othello::SearchResult result =
        othello::search(board, othello::SearchOptions{.exact_endgame_empty_threshold = 10});

    CHECK(result.best_move == exact.best_move);
    CHECK(result.score == exact.disc_margin * exact_endgame_score_scale);
    CHECK(result.depth == exact.empties);
    CHECK(result.nodes == exact.nodes);
    CHECK(result.stats.nodes == exact.stats.nodes);
    CHECK(result.stats.tt_lookups == exact.stats.tt_lookups);
    CHECK(result.stats.tt_hits == exact.stats.tt_hits);
    CHECK(result.stats.tt_exact_hits == exact.stats.tt_exact_hits);
    CHECK(result.stats.tt_lower_hits == exact.stats.tt_lower_hits);
    CHECK(result.stats.tt_upper_hits == exact.stats.tt_upper_hits);
    CHECK(result.stats.tt_stores == exact.stats.tt_stores);
    CHECK(result.stats.tt_overwrites == exact.stats.tt_overwrites);
    CHECK(result.stats.tt_collisions == exact.stats.tt_collisions);
    CHECK(result.stats.tt_rejected_stores == exact.stats.tt_rejected_stores);
    CHECK(result.stats.tt_move_ordering_probes == exact.stats.tt_move_ordering_probes);
    CHECK(result.stats.tt_move_ordering_hits == exact.stats.tt_move_ordering_hits);
    CHECK(result.stats.tt_move_ordering_used == exact.stats.tt_move_ordering_used);
    CHECK(result.stats.eval_calls == 0);
    CHECK(result.stats.pvs_scouts == 0);
    CHECK(result.stats.aspiration_searches == 0);
}

TEST_CASE("Iterative search returns exact result immediately within threshold", "[search]") {
    const Board board = one_empty_forced_board();
    const othello::ExactEndgameResult exact = othello::solve_exact_endgame(board);
    const othello::SearchOptions options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 1,
        .use_pvs = true,
        .use_aspiration_window = true,
    };

    const othello::SearchResult result = othello::search_iterative(board, options);

    CHECK(result.best_move == exact.best_move);
    CHECK(result.score == exact.disc_margin * exact_endgame_score_scale);
    CHECK(result.depth == exact.empties);
    CHECK(result.nodes == exact.nodes);
    CHECK(result.stats.nodes == result.nodes);
    CHECK(result.stats.tt_lookups == 0);
    CHECK(result.stats.tt_move_ordering_probes == 0);
    CHECK(result.stats.pvs_scouts == 0);
    CHECK(result.stats.pvs_researches == 0);
    CHECK(result.stats.pvs_scout_cutoffs == 0);
    CHECK(result.stats.aspiration_searches == 0);
    CHECK(result.stats.aspiration_researches == 0);
    CHECK(result.stats.aspiration_fail_lows == 0);
    CHECK(result.stats.aspiration_fail_highs == 0);
    CHECK(result.stats.aspiration_full_window_fallbacks == 0);
    CHECK(result.stats.eval_calls == 0);
    CHECK(result.stats.pass_nodes == 0);
    CHECK(result.stats.dynamic_ordering_nodes == 0);
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
    CHECK(result.stats.tt_rejected_stores == 0);
    CHECK(result.stats.tt_move_ordering_probes == 0);
    CHECK(result.stats.tt_move_ordering_hits == 0);
    CHECK(result.stats.tt_move_ordering_used == 0);
    CHECK(result.stats.pvs_scouts == 0);
    CHECK(result.stats.pvs_researches == 0);
    CHECK(result.stats.pvs_scout_cutoffs == 0);
    CHECK(result.stats.searched_moves > 0);
    CHECK(result.stats.legal_move_nodes > 0);
    CHECK(result.stats.eval_calls > 0);
    CHECK(result.stats.beta_cutoffs_first_move <= result.stats.beta_cutoffs);
    CHECK(result.stats.pass_nodes == 0);
    CHECK(result.stats.game_over_nodes == 0);
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

TEST_CASE("Transposition best move ordering preserves fixed-depth search result", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions without_tt{
        .max_depth = 5,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
    };
    const othello::SearchOptions with_tt{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult baseline = othello::search(*board, without_tt);
    const othello::SearchResult ordered = othello::search(*board, with_tt);

    CHECK(ordered.best_move == baseline.best_move);
    CHECK(ordered.score == baseline.score);
    CHECK(ordered.depth == baseline.depth);
}

TEST_CASE("Iterative search counts transposition best move ordering probes", "[search]") {
    const Board board = othello::test::board_from_text(R"(........
.B......
..B.B...
...BB...
.WWWWW..
..B.....
........
........
side=W)");

    const othello::SearchOptions options{
        .max_depth = 7,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult result = othello::search_iterative(board, options);

    CHECK(result.stats.tt_move_ordering_probes > 0);
    CHECK(result.stats.tt_move_ordering_hits > 0);
    CHECK(result.stats.tt_move_ordering_used > 0);
    CHECK(result.stats.tt_move_ordering_hits <= result.stats.tt_move_ordering_probes);
    CHECK(result.stats.tt_move_ordering_used <= result.stats.tt_move_ordering_hits);
}

TEST_CASE("Search stats count rejected transposition table stores", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 5,
        .use_transposition_table = true,
        .transposition_table_entries = 1,
    };
    const othello::SearchResult first = othello::search(*board, options);
    const othello::SearchResult second = othello::search(*board, options);

    CHECK(first.best_move == second.best_move);
    CHECK(first.score == second.score);
    CHECK(first.depth == second.depth);
    CHECK(first.stats.tt_stores > 0);
    CHECK(first.stats.tt_rejected_stores > 0);
    CHECK(first.stats.tt_collisions <= first.stats.tt_overwrites);
    CHECK(first.stats.tt_hits <= first.stats.tt_lookups);
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

TEST_CASE("Small transposition tables keep iterative search deterministic", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 5,
        .use_transposition_table = true,
        .transposition_table_entries = 1,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult first = othello::search_iterative(*board, options);
    const othello::SearchResult second = othello::search_iterative(*board, options);

    CHECK(first.best_move == second.best_move);
    CHECK(first.score == second.score);
    CHECK(first.depth == second.depth);
    CHECK(first.nodes == second.nodes);
    CHECK(first.principal_variation == second.principal_variation);
    CHECK(first.stats.tt_move_ordering_probes == second.stats.tt_move_ordering_probes);
    CHECK(first.stats.tt_move_ordering_hits == second.stats.tt_move_ordering_hits);
    CHECK(first.stats.tt_move_ordering_used == second.stats.tt_move_ordering_used);
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

TEST_CASE("PVS off leaves PVS counters zero", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchResult result =
        othello::search(*board, othello::SearchOptions{.max_depth = 5});

    CHECK(result.stats.pvs_scouts == 0);
    CHECK(result.stats.pvs_researches == 0);
    CHECK(result.stats.pvs_scout_cutoffs == 0);
}

TEST_CASE("PVS preserves fixed-depth search result without transposition table", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions alpha_beta_options{
        .max_depth = 5,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = false,
    };
    const othello::SearchOptions pvs_options{
        .max_depth = 5,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };

    const othello::SearchResult alpha_beta = othello::search(*board, alpha_beta_options);
    const othello::SearchResult pvs = othello::search(*board, pvs_options);

    CHECK(pvs.best_move == alpha_beta.best_move);
    CHECK(pvs.score == alpha_beta.score);
    CHECK(pvs.depth == alpha_beta.depth);
    CHECK(pvs.stats.pvs_scouts > 0);
    CHECK(pvs.stats.pvs_researches <= pvs.stats.pvs_scouts);
    CHECK(pvs.stats.pvs_scout_cutoffs <= pvs.stats.pvs_scouts);
    CHECK(pvs.stats.pvs_researches + pvs.stats.pvs_scout_cutoffs == pvs.stats.pvs_scouts);
}

TEST_CASE("TT and PVS combinations preserve deterministic fixed-depth result", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions baseline_options{
        .max_depth = 4,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = false,
    };
    const othello::SearchResult baseline = othello::search(*board, baseline_options);

    for (const bool use_tt : {false, true}) {
        for (const bool use_pvs : {false, true}) {
            CAPTURE(use_tt);
            CAPTURE(use_pvs);
            const othello::SearchOptions options{
                .max_depth = 4,
                .use_transposition_table = use_tt,
                .exact_endgame_empty_threshold = 0,
                .use_pvs = use_pvs,
            };

            const othello::SearchResult result = othello::search(*board, options);

            CHECK(result.best_move == baseline.best_move);
            CHECK(result.score == baseline.score);
            CHECK(result.depth == baseline.depth);
        }
    }
}

TEST_CASE("PVS preserves fixed-depth search result with transposition table", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions alpha_beta_options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = false,
    };
    const othello::SearchOptions pvs_options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };

    const othello::SearchResult alpha_beta = othello::search(*board, alpha_beta_options);
    const othello::SearchResult pvs = othello::search(*board, pvs_options);

    CHECK(pvs.best_move == alpha_beta.best_move);
    CHECK(pvs.score == alpha_beta.score);
    CHECK(pvs.depth == alpha_beta.depth);
    CHECK(pvs.stats.pvs_scouts > 0);
    CHECK(pvs.stats.pvs_researches + pvs.stats.pvs_scout_cutoffs == pvs.stats.pvs_scouts);
}

TEST_CASE("Iterative PVS final score matches fixed-depth PVS result", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };

    const othello::SearchResult fixed_depth = othello::search(*board, options);
    const othello::SearchResult iterative = othello::search_iterative(*board, options);
    const othello::SearchResult repeated = othello::search_iterative(*board, options);

    CHECK(iterative.score == fixed_depth.score);
    CHECK(iterative.depth == fixed_depth.depth);
    CHECK(iterative.best_move == repeated.best_move);
    CHECK(iterative.score == repeated.score);
    CHECK(iterative.depth == repeated.depth);
    CHECK(iterative.principal_variation == repeated.principal_variation);
    CHECK(iterative.stats.pvs_scouts > 0);
    CHECK(iterative.stats.pvs_researches + iterative.stats.pvs_scout_cutoffs ==
          iterative.stats.pvs_scouts);
}

TEST_CASE("Aspiration disabled leaves iterative search behavior unchanged", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 5,
        .exact_endgame_empty_threshold = 0,
        .use_aspiration_window = false,
    };

    const othello::SearchResult first = othello::search_iterative(*board, options);
    const othello::SearchResult second = othello::search_iterative(*board, options);

    CHECK(first.best_move == second.best_move);
    CHECK(first.score == second.score);
    CHECK(first.depth == second.depth);
    CHECK(first.nodes == second.nodes);
    CHECK(first.principal_variation == second.principal_variation);
    CHECK(first.stats.aspiration_searches == 0);
    CHECK(first.stats.aspiration_researches == 0);
    CHECK(first.stats.aspiration_fail_lows == 0);
    CHECK(first.stats.aspiration_fail_highs == 0);
    CHECK(first.stats.aspiration_full_window_fallbacks == 0);
}

TEST_CASE("Aspiration preserves iterative search result", "[search]") {
    const Board board = othello::test::board_from_text(R"(........
.B......
..B.B...
...BB...
.WWWWW..
..B.....
........
........
side=W)");

    const othello::SearchOptions full_window_options{
        .max_depth = 6,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
    };
    const othello::SearchOptions aspiration_options{
        .max_depth = 6,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .use_aspiration_window = true,
        .aspiration_window = 50,
    };

    const othello::SearchResult full_window =
        othello::search_iterative(board, full_window_options);
    const othello::SearchResult aspirated =
        othello::search_iterative(board, aspiration_options);

    CHECK(aspirated.best_move == full_window.best_move);
    CHECK(aspirated.score == full_window.score);
    CHECK(aspirated.depth == full_window.depth);
    CHECK(aspirated.principal_variation == full_window.principal_variation);
    CHECK(aspirated.stats.aspiration_searches > 0);
    CHECK(aspirated.stats.aspiration_researches ==
          aspirated.stats.aspiration_fail_lows + aspirated.stats.aspiration_fail_highs);
}

TEST_CASE("Aspiration preserves iterative TT and PVS result", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions full_window_options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };
    const othello::SearchOptions aspiration_options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .use_aspiration_window = true,
        .aspiration_window = 50,
    };

    const othello::SearchResult full_window =
        othello::search_iterative(*board, full_window_options);
    const othello::SearchResult aspirated =
        othello::search_iterative(*board, aspiration_options);

    CHECK(aspirated.best_move == full_window.best_move);
    CHECK(aspirated.score == full_window.score);
    CHECK(aspirated.depth == full_window.depth);
    CHECK(aspirated.principal_variation == full_window.principal_variation);
    CHECK(aspirated.stats.aspiration_searches > 0);
    CHECK(aspirated.stats.aspiration_researches ==
          aspirated.stats.aspiration_fail_lows + aspirated.stats.aspiration_fail_highs);
    CHECK(aspirated.stats.aspiration_full_window_fallbacks <=
          aspirated.stats.aspiration_researches);
}

TEST_CASE("Narrow aspiration falls back without changing iterative result", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions full_window_options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };
    const othello::SearchOptions narrow_options{
        .max_depth = 5,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .use_aspiration_window = true,
        .aspiration_window = 1,
        .aspiration_max_researches = 0,
    };

    const othello::SearchResult full_window =
        othello::search_iterative(*board, full_window_options);
    const othello::SearchResult narrow = othello::search_iterative(*board, narrow_options);

    CHECK(narrow.best_move == full_window.best_move);
    CHECK(narrow.score == full_window.score);
    CHECK(narrow.depth == full_window.depth);
    CHECK(narrow.principal_variation == full_window.principal_variation);
    CHECK(narrow.stats.aspiration_searches > 0);
    CHECK(narrow.stats.aspiration_researches ==
          narrow.stats.aspiration_fail_lows + narrow.stats.aspiration_fail_highs);
    CHECK(narrow.stats.aspiration_full_window_fallbacks <=
          narrow.stats.aspiration_researches);
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
    CHECK(result.depth == 0);
    CHECK(result.nodes > 0);
}

TEST_CASE("PVS does not scout terminal boards", "[search]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };
    const othello::SearchOptions options{
        .max_depth = 3,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };

    const othello::SearchResult result = othello::search(board, options);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.stats.pvs_scouts == 0);
    CHECK(result.stats.pvs_researches == 0);
    CHECK(result.stats.pvs_scout_cutoffs == 0);
    CHECK(result.stats.game_over_nodes > 0);
    CHECK(result.stats.eval_calls > 0);
}

TEST_CASE("Transposition move ordering does not affect terminal boards", "[search]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };
    const othello::SearchOptions options{
        .max_depth = 3,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult result = othello::search(board, options);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    CHECK(result.depth == 3);
    CHECK(result.stats.tt_move_ordering_probes == 0);
    CHECK(result.stats.tt_move_ordering_hits == 0);
    CHECK(result.stats.tt_move_ordering_used == 0);
    CHECK(result.stats.pvs_scouts == 0);
    CHECK(result.stats.pvs_researches == 0);
    CHECK(result.stats.pvs_scout_cutoffs == 0);
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
    CHECK(result.stats.pass_nodes > 0);
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

TEST_CASE("Transposition move ordering preserves pass position result", "[search]") {
    const Board board = othello::test::black_must_pass_board();

    REQUIRE(othello::legal_moves(board) == 0);
    REQUIRE_FALSE(othello::is_game_over(board));

    const othello::SearchOptions without_tt{
        .max_depth = 3,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
    };
    const othello::SearchOptions with_tt{
        .max_depth = 3,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult baseline = othello::search_iterative(board, without_tt);
    const othello::SearchResult ordered = othello::search_iterative(board, with_tt);

    CHECK(ordered.best_move == baseline.best_move);
    CHECK(ordered.score == baseline.score);
    CHECK(ordered.depth == baseline.depth);
    CHECK(ordered.principal_variation == baseline.principal_variation);
}

TEST_CASE("PVS preserves pass position result without fake principal variation moves", "[search]") {
    const Board board = othello::test::black_must_pass_board();

    REQUIRE(othello::legal_moves(board) == 0);
    REQUIRE_FALSE(othello::is_game_over(board));

    const othello::SearchOptions alpha_beta_options{
        .max_depth = 4,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = false,
    };
    const othello::SearchOptions pvs_options{
        .max_depth = 4,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };

    const othello::SearchResult alpha_beta = othello::search_iterative(board, alpha_beta_options);
    const othello::SearchResult pvs = othello::search_iterative(board, pvs_options);

    CHECK_FALSE(pvs.best_move.has_value());
    CHECK(pvs.score == alpha_beta.score);
    CHECK(pvs.depth == alpha_beta.depth);
    CHECK(pvs.principal_variation.size() <= 4);
    if (!pvs.principal_variation.empty()) {
        const auto passed = othello::pass_turn(board);
        REQUIRE(passed.has_value());
        CHECK((othello::legal_moves(*passed) & pvs.principal_variation.front().bit()) != 0);
    }
    CHECK(pvs.stats.pvs_researches + pvs.stats.pvs_scout_cutoffs == pvs.stats.pvs_scouts);
}
