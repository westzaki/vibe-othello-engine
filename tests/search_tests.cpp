#include "common/stats.hpp"
#include "test_helpers.hpp"

#include "../src/search_ordering.hpp"
#include "../src/search_tt.hpp"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include <limits>
#include <othello/othello.hpp>
#include <vector>

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

[[nodiscard]] Board adaptive_exact_allowed_board() {
    return othello::test::board_from_text(R"(.WB.B.BW
.WWWWWWW
.WBWWWWB
W.WBWWW.
WWBBBW.W
WBBBB.W.
WBBBBB..
WBW...B.
side=B)");
}

[[nodiscard]] Board adaptive_exact_opponent_high_mobility_board() {
    return othello::test::board_from_text(R"(...W.B.W
....BBWB
WB.BBWBB
.BBBWWBB
.BWBWBBB
.BWWWBBB
.BWWW.BB
BBBBW.WB
side=B)");
}

[[nodiscard]] Board adaptive_exact_fifteen_allowed_board() {
    return othello::test::board_from_text(R"(...W.BBW
....BBBB
WB.BBWBB
.BBBWWBB
.BWBWBBB
.BWWWBBB
.BWWW.BB
BBBBW.WB
side=W)");
}

[[nodiscard]] Board adaptive_exact_fifteen_skip_board() {
    return othello::test::board_from_text(R"(BBB..W.B
BWWBB.B.
.W.BBBWW
WWBWBBWW
..WWWBWW
.BBWWWWW
BBBBBBBB
..B.W.W.
side=W)");
}

[[nodiscard]] Board adaptive_exact_root_pass_board() {
    return othello::test::board_from_text(R"(........
BBBBBBBW
BBWWBBB.
BBBBBBBB
.BBBBWBB
..BBBWBB
..BBBWWW
..BBBBBW
side=B)");
}

[[nodiscard]] Board adaptive_exact_high_mobility_board() {
    return othello::test::board_from_text(R"(...W.W..
..WWW.WW
.WWWBWW.
.WBBBBB.
WBWBBBBW
BBBBBWWW
BBBBWWWW
BW.B.WW.
side=B)");
}

void check_heuristic_score_metadata(const othello::SearchResult& result) {
    CHECK(result.score_kind == othello::SearchScoreKind::Heuristic);
    CHECK_FALSE(result.used_exact_endgame);
    CHECK_FALSE(result.exact_disc_margin.has_value());
}

void check_exact_score_metadata(const othello::SearchResult& result,
                                const othello::ExactEndgameResult& exact) {
    CHECK(result.score_kind == othello::SearchScoreKind::ExactDiscMarginScaled);
    CHECK(result.used_exact_endgame);
    REQUIRE(result.exact_disc_margin.has_value());
    CHECK(*result.exact_disc_margin == exact.disc_margin);
}

struct IterativeDepthCapture {
    std::vector<othello::IterativeSearchDepthInfo> rows;
    const std::vector<othello::RootMoveOrderingEntry>* root_ordering_snapshot = nullptr;
    std::vector<std::vector<othello::RootMoveOrderingEntry>> root_ordering_by_depth;
};

[[nodiscard]] othello::search_detail::TranspositionEntry
tt_entry(othello::ZobristHash hash, othello::search_detail::TranspositionScope scope,
         std::uint32_t generation, int depth) {
    return othello::search_detail::TranspositionEntry{
        .hash = hash,
        .eval_identity = scope.eval_identity,
        .generation = generation,
        .depth = depth,
        .score = 0,
        .best_move_index = -1,
        .mode = scope.mode,
        .bound = othello::search_detail::BoundKind::Exact,
        .occupied = true,
    };
}

void capture_iterative_depth(const othello::IterativeSearchDepthInfo& info, void* user_data) {
    auto* capture = static_cast<IterativeDepthCapture*>(user_data);
    REQUIRE(capture != nullptr);
    capture->rows.push_back(info);
    if (capture->root_ordering_snapshot != nullptr) {
        capture->root_ordering_by_depth.push_back(*capture->root_ordering_snapshot);
    }
}

[[nodiscard]] bool root_orderings_equal(
    const std::vector<othello::RootMoveOrderingEntry>& left,
    const std::vector<othello::RootMoveOrderingEntry>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].move != right[index].move ||
            left[index].order_score != right[index].order_score) {
            return false;
        }
    }
    return true;
}

void check_same_result(const othello::SearchResult& left, const othello::SearchResult& right) {
    CHECK(left.best_move == right.best_move);
    CHECK(left.score == right.score);
    CHECK(left.depth == right.depth);
    CHECK(left.principal_variation == right.principal_variation);
    CHECK(left.score_kind == right.score_kind);
    CHECK(left.used_exact_endgame == right.used_exact_endgame);
    CHECK(left.exact_disc_margin == right.exact_disc_margin);
}

} // namespace

TEST_CASE("Fixed-depth search at depth zero returns an evaluation-only result", "[search]") {
    const Board board = Board::initial();

    const othello::SearchResult result = othello::search_fixed_depth(board, 0);

    CHECK_FALSE(result.best_move.has_value());
    CHECK(result.principal_variation.empty());
    CHECK(result.score == othello::evaluate_basic(board, board.side_to_move));
    check_heuristic_score_metadata(result);
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
    check_heuristic_score_metadata(result);
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

TEST_CASE("Search options custom evaluator override resolves over the default", "[search]") {
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

    CHECK(othello::resolve_evaluation_config(options) == config);
    CHECK(result.score == othello::evaluate_with_config(*board, board->side_to_move, config));
    CHECK(result.score != othello::evaluate_with_config(*board, board->side_to_move,
                                                        othello::default_evaluation_config()));
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
    check_heuristic_score_metadata(result);
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
        .aspiration_fail_low_distance_sum = 7,
        .aspiration_fail_high_distance_sum = 0,
        .aspiration_fail_low_distance_max = 7,
        .aspiration_fail_high_distance_max = 0,
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
        .aspiration_fail_low_distance_sum = 5,
        .aspiration_fail_high_distance_sum = 3,
        .aspiration_fail_low_distance_max = 5,
        .aspiration_fail_high_distance_max = 3,
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
    CHECK(total.aspiration_fail_low_distance_sum == 12);
    CHECK(total.aspiration_fail_high_distance_sum == 3);
    CHECK(total.aspiration_fail_low_distance_max == 7);
    CHECK(total.aspiration_fail_high_distance_max == 3);
    CHECK(othello::tools::beta_cut_first_move_percentage(total) == 60.0);
}

TEST_CASE("Search stats add and delta cover every counter field", "[search]") {
    constexpr std::size_t field_count =
        sizeof(othello::SearchStats) / sizeof(std::uint64_t);
    CHECK(othello::search_stats_detail::tracked_member_count == field_count);

    const othello::SearchStats before{
        .nodes = 10,
        .aspiration_fail_low_distance_max = 7,
        .aspiration_fail_high_distance_max = 11,
    };
    const othello::SearchStats after{
        .nodes = 15,
        .aspiration_fail_low_distance_max = 7,
        .aspiration_fail_high_distance_max = 13,
    };

    const othello::SearchStats delta = othello::search_stats_delta(after, before);

    CHECK(delta.nodes == 5);
    CHECK(delta.aspiration_fail_low_distance_max == 0);
    CHECK(delta.aspiration_fail_high_distance_max == 13);
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
    check_heuristic_score_metadata(result);
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
    };

    const othello::SearchResult result = othello::search(board, options);

    CHECK(result.best_move == exact.best_move);
    CHECK(result.score == exact.disc_margin * exact_endgame_score_scale);
    check_exact_score_metadata(result, exact);
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
    check_exact_score_metadata(result, exact);
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

TEST_CASE("Adaptive exact root policy solves conservative sixteen-empty roots", "[search]") {
    const Board board = adaptive_exact_allowed_board();
    const othello::ExactEndgameResult exact = othello::solve_exact_endgame(board);
    const othello::SearchOptions options{
        .max_depth = 1,
        .exact_endgame_root_policy = othello::ExactEndgameRootPolicy::Adaptive16,
    };

    const othello::ExactEndgameRootDecision decision =
        othello::decide_exact_endgame_root(board, options);
    const othello::SearchResult result = othello::search(board, options);

    CHECK(decision.solve_exact);
    CHECK(decision.empty_count == 16);
    CHECK(decision.legal_moves_current == 9);
    CHECK(decision.legal_moves_opponent == 8);
    CHECK(decision.skip_reason == othello::ExactEndgameRootSkipReason::None);
    CHECK(result.best_move == exact.best_move);
    CHECK(result.score == exact.disc_margin * exact_endgame_score_scale);
    check_exact_score_metadata(result, exact);
    CHECK(result.depth == exact.empties);
    CHECK(result.nodes == exact.nodes);
    CHECK(result.stats.eval_calls == 0);
}

TEST_CASE("Adaptive exact root policy still honors disabled exact threshold", "[search]") {
    const othello::SearchOptions options{
        .max_depth = 1,
        .exact_endgame_empty_threshold = 0,
        .exact_endgame_root_policy = othello::ExactEndgameRootPolicy::Adaptive16,
    };

    const othello::ExactEndgameRootDecision decision =
        othello::decide_exact_endgame_root(adaptive_exact_allowed_board(), options);
    CHECK_FALSE(decision.solve_exact);
    CHECK(decision.empty_count == 16);
    CHECK(decision.skip_reason == othello::ExactEndgameRootSkipReason::Disabled);
}

TEST_CASE("Adaptive exact root policy gates fifteen-empty roots by mobility", "[search]") {
    const othello::SearchOptions options{
        .max_depth = 1,
        .exact_endgame_root_policy = othello::ExactEndgameRootPolicy::Adaptive16,
    };

    const othello::ExactEndgameRootDecision allowed_decision =
        othello::decide_exact_endgame_root(adaptive_exact_fifteen_allowed_board(), options);
    CHECK(allowed_decision.solve_exact);
    CHECK(allowed_decision.empty_count == 15);
    CHECK(allowed_decision.legal_moves_current == 10);
    CHECK(allowed_decision.skip_reason == othello::ExactEndgameRootSkipReason::None);

    const othello::ExactEndgameRootDecision skipped_decision =
        othello::decide_exact_endgame_root(adaptive_exact_fifteen_skip_board(), options);
    CHECK_FALSE(skipped_decision.solve_exact);
    CHECK(skipped_decision.empty_count == 15);
    CHECK(skipped_decision.legal_moves_current == 12);
    CHECK(skipped_decision.skip_reason ==
          othello::ExactEndgameRootSkipReason::AdaptiveTooManyLegalMoves);

    const othello::ExactEndgameRootDecision opponent_decision =
        othello::decide_exact_endgame_root(adaptive_exact_opponent_high_mobility_board(), options);
    CHECK_FALSE(opponent_decision.solve_exact);
    CHECK(opponent_decision.empty_count == 16);
    CHECK(opponent_decision.legal_moves_current == 3);
    CHECK(opponent_decision.legal_moves_opponent == 11);
    CHECK(opponent_decision.skip_reason ==
          othello::ExactEndgameRootSkipReason::AdaptiveOpponentTooManyLegalMoves);
}

TEST_CASE("Adaptive exact root policy skips heavy sixteen-empty roots", "[search]") {
    const othello::SearchOptions options{
        .max_depth = 1,
        .exact_endgame_root_policy = othello::ExactEndgameRootPolicy::Adaptive16,
    };

    const Board root_pass = adaptive_exact_root_pass_board();
    const othello::ExactEndgameRootDecision pass_decision =
        othello::decide_exact_endgame_root(root_pass, options);
    const othello::SearchResult pass_result = othello::search(root_pass, options);

    CHECK_FALSE(pass_decision.solve_exact);
    CHECK(pass_decision.empty_count == 16);
    CHECK(pass_decision.legal_moves_current == 0);
    CHECK(pass_decision.skip_reason == othello::ExactEndgameRootSkipReason::AdaptiveRootPass);
    CHECK(pass_result.depth == 1);
    check_heuristic_score_metadata(pass_result);
    CHECK(pass_result.stats.eval_calls > 0);

    const Board high_mobility = adaptive_exact_high_mobility_board();
    const othello::ExactEndgameRootDecision mobility_decision =
        othello::decide_exact_endgame_root(high_mobility, options);
    const othello::SearchResult mobility_result = othello::search(high_mobility, options);

    CHECK_FALSE(mobility_decision.solve_exact);
    CHECK(mobility_decision.empty_count == 16);
    CHECK(mobility_decision.legal_moves_current == 14);
    CHECK(mobility_decision.skip_reason ==
          othello::ExactEndgameRootSkipReason::AdaptiveTooManyLegalMoves);
    CHECK(mobility_result.depth == 1);
    check_heuristic_score_metadata(mobility_result);
    CHECK(mobility_result.stats.eval_calls > 0);
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
    check_exact_score_metadata(result, exact);
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

TEST_CASE("Iterative depth instrumentation preserves search result", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions baseline_options{
        .max_depth = 4,
        .use_transposition_table = true,
        .transposition_table_entries = 64,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .use_aspiration_window = true,
    };

    const othello::SearchResult baseline = othello::search_iterative(*board, baseline_options);

    IterativeDepthCapture capture;
    std::vector<othello::RootMoveOrderingEntry> root_ordering;
    capture.root_ordering_snapshot = &root_ordering;
    othello::SearchOptions instrumented_options = baseline_options;
    instrumented_options.iterative_depth_observer = capture_iterative_depth;
    instrumented_options.iterative_depth_observer_user_data = &capture;
    instrumented_options.root_move_ordering_snapshot = &root_ordering;

    const othello::SearchResult instrumented =
        othello::search_iterative(*board, instrumented_options);

    CHECK(instrumented.best_move == baseline.best_move);
    CHECK(instrumented.score == baseline.score);
    CHECK(instrumented.depth == baseline.depth);
    CHECK(instrumented.nodes == baseline.nodes);
    CHECK(instrumented.principal_variation == baseline.principal_variation);
    CHECK(instrumented.stats.pvs_scouts == baseline.stats.pvs_scouts);
    CHECK(instrumented.stats.pvs_researches == baseline.stats.pvs_researches);
    CHECK(instrumented.stats.aspiration_searches == baseline.stats.aspiration_searches);
    CHECK(instrumented.stats.aspiration_researches == baseline.stats.aspiration_researches);

    REQUIRE(capture.rows.size() == 4);
    for (std::size_t index = 0; index < capture.rows.size(); ++index) {
        const auto& row = capture.rows[index];
        CHECK(row.requested_depth == 4);
        CHECK(row.completed_depth == static_cast<int>(index) + 1);
        CHECK(row.elapsed_ns > 0);
        CHECK(row.stats.nodes > 0);
        if (index == 0) {
            CHECK_FALSE(row.previous_score.has_value());
            CHECK(row.previous_score_delta == 0);
        } else {
            REQUIRE(row.previous_score.has_value());
            CHECK(row.previous_score_delta == row.score - *row.previous_score);
        }
    }

    REQUIRE_FALSE(root_ordering.empty());
    REQUIRE(capture.root_ordering_by_depth.size() == capture.rows.size());
    CHECK(root_orderings_equal(root_ordering, capture.root_ordering_by_depth.back()));
    CHECK_FALSE(root_orderings_equal(capture.root_ordering_by_depth.front(),
                                     capture.root_ordering_by_depth.back()));
    REQUIRE(instrumented.best_move.has_value());
    bool best_move_was_ordered = false;
    for (const auto& entry : root_ordering) {
        if (entry.move == *instrumented.best_move) {
            best_move_was_ordered = true;
        }
    }
    CHECK(best_move_was_ordered);
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

TEST_CASE("Search session preserves one-shot fixed-depth search results", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 4,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };

    othello::SearchSession session{options};
    const othello::SearchResult session_result = othello::search(session, *board, options);
    const othello::SearchResult temporary_result = othello::search(*board, options);

    check_same_result(session_result, temporary_result);
    CHECK(session.generation() == 1);
    CHECK(session.mode() == othello::SearchMode::FixedDepth);
    CHECK(session.previous_best_move() == session_result.best_move);
    CHECK(session.root_principal_variation() == session_result.principal_variation);
}

TEST_CASE("Search session keeps a persistent midgame transposition table", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 4,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
    };

    othello::SearchSession session{options};
    const othello::SearchResult first = othello::search(session, *board, options);
    const othello::SearchResult second = othello::search(session, *board, options);

    check_same_result(first, second);
    CHECK(session.generation() == 2);
    CHECK(second.stats.tt_hits > 0);
    CHECK(second.nodes <= first.nodes);
}

TEST_CASE("Search session scopes transposition entries by evaluation config", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    othello::EvaluationConfig first_config;
    first_config.opening = othello::EvaluationFeatureWeights{.disc_difference = 1};
    first_config.midgame = othello::EvaluationFeatureWeights{.disc_difference = 1};
    first_config.late = othello::EvaluationFeatureWeights{.disc_difference = 1};

    othello::EvaluationConfig second_config;
    second_config.opening = othello::EvaluationFeatureWeights{.disc_difference = 9};
    second_config.midgame = othello::EvaluationFeatureWeights{.disc_difference = 9};
    second_config.late = othello::EvaluationFeatureWeights{.disc_difference = 9};

    const othello::SearchOptions first_options{
        .max_depth = 0,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .evaluation_config_override = first_config,
    };
    const othello::SearchOptions second_options{
        .max_depth = 0,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .evaluation_config_override = second_config,
    };

    othello::SearchSession session{first_options};
    static_cast<void>(othello::search(session, *board, first_options));
    const othello::SearchResult scoped = othello::search(session, *board, second_options);
    const othello::SearchResult fresh = othello::search(*board, second_options);

    check_same_result(scoped, fresh);
    CHECK(scoped.stats.tt_lookups == 1);
    CHECK(scoped.stats.tt_hits == 0);
}

TEST_CASE("Search session scopes transposition entries by search mode", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions options{
        .max_depth = 3,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
    };

    othello::SearchSession session{options};
    static_cast<void>(othello::search(session, *board, options));
    const othello::SearchResult session_iterative =
        othello::search_iterative(session, *board, options);
    const othello::SearchResult fresh_iterative = othello::search_iterative(*board, options);

    check_same_result(session_iterative, fresh_iterative);
    CHECK(session.mode() == othello::SearchMode::Iterative);
    CHECK(session.previous_best_move() == session_iterative.best_move);
}

TEST_CASE("Midgame transposition table lookup keeps mode and evaluation scopes separate",
          "[search]") {
    othello::search_detail::TranspositionTable table{
        othello::search_detail::SearchEngineOptions{
            .use_transposition_table = true,
            .transposition_table_entries = 4,
        }};
    othello::SearchStats stats;
    constexpr othello::ZobristHash hash = 0x1234;
    constexpr othello::search_detail::TranspositionScope fixed_scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 17,
    };
    constexpr othello::search_detail::TranspositionScope iterative_scope{
        .mode = othello::SearchMode::Iterative,
        .eval_identity = 17,
    };
    constexpr othello::search_detail::TranspositionScope other_eval_scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 99,
    };

    REQUIRE(table.store(hash, fixed_scope, 4, 3, 42, -100, 100, std::nullopt, stats));

    CHECK_FALSE(table.lookup(hash, iterative_scope, 3, -100, 100, false, stats)
                    .cutoff.has_value());
    CHECK_FALSE(table.lookup(hash, other_eval_scope, 3, -100, 100, false, stats)
                    .cutoff.has_value());
    const auto scoped = table.lookup(hash, fixed_scope, 3, -100, 100, false, stats);
    REQUIRE(scoped.cutoff.has_value());
    CHECK(scoped.cutoff->score == 42);
}

TEST_CASE("Midgame transposition table updates same scoped hash regardless of generation",
          "[search]") {
    othello::search_detail::TranspositionTable table{
        othello::search_detail::SearchEngineOptions{
            .use_transposition_table = true,
            .transposition_table_entries = 4,
        }};
    othello::SearchStats stats;
    constexpr othello::ZobristHash hash = 0x1234;
    constexpr othello::search_detail::TranspositionScope scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 17,
    };

    REQUIRE(table.store(hash, scope, 20, 4, 42, -100, 100, std::nullopt, stats));
    REQUIRE(table.store(hash, scope, 1, 1, 7, -100, 100, std::nullopt, stats));

    const auto scoped = table.lookup(hash, scope, 1, -100, 100, false, stats);
    REQUIRE(scoped.cutoff.has_value());
    CHECK(scoped.cutoff->score == 7);
    CHECK(stats.tt_overwrites == 1);
    CHECK(stats.tt_collisions == 0);
}

TEST_CASE("Midgame transposition replacement updates same scoped hash first", "[search]") {
    using othello::search_detail::select_replacement_entry;
    constexpr othello::search_detail::TranspositionScope scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 7,
    };
    std::array<othello::search_detail::TranspositionEntry, 4> entries{
        tt_entry(11, scope, 1, 1),
        tt_entry(12, scope, 20, 5),
        tt_entry(13, scope, 20, 5),
        tt_entry(14, scope, 20, 5),
    };

    CHECK(select_replacement_entry(entries, 11, scope, 20, 0) == &entries[0]);
}

TEST_CASE("Midgame transposition replacement uses an empty slot before overwriting",
          "[search]") {
    using othello::search_detail::select_replacement_entry;
    constexpr othello::search_detail::TranspositionScope scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 7,
    };
    std::array<othello::search_detail::TranspositionEntry, 4> entries{
        tt_entry(11, scope, 10, 2),
        othello::search_detail::TranspositionEntry{},
        tt_entry(13, scope, 10, 1),
        tt_entry(14, scope, 10, 1),
    };

    CHECK(select_replacement_entry(entries, 99, scope, 10, 0) == &entries[1]);
}

TEST_CASE("Midgame transposition replacement prefers old shallow entries under pressure",
          "[search]") {
    using othello::search_detail::select_replacement_entry;
    constexpr othello::search_detail::TranspositionScope scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 7,
    };
    std::array<othello::search_detail::TranspositionEntry, 4> entries{
        tt_entry(11, scope, 20, 4),
        tt_entry(12, scope, 8, 1),
        tt_entry(13, scope, 20, 2),
        tt_entry(14, scope, 20, 3),
    };

    CHECK(select_replacement_entry(entries, 99, scope, 20, 1) == &entries[1]);
}

TEST_CASE("Midgame transposition replacement rejects weak incoming stores", "[search]") {
    using othello::search_detail::select_replacement_entry;
    constexpr othello::search_detail::TranspositionScope scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 7,
    };
    std::array<othello::search_detail::TranspositionEntry, 4> entries{
        tt_entry(11, scope, 20, 4),
        tt_entry(12, scope, 20, 5),
        tt_entry(13, scope, 20, 6),
        tt_entry(14, scope, 20, 7),
    };

    CHECK(select_replacement_entry(entries, 99, scope, 20, 2) == nullptr);
}

TEST_CASE("Midgame transposition replacement handles generation wraparound", "[search]") {
    using othello::search_detail::generation_age;
    using othello::search_detail::select_replacement_entry;
    constexpr std::uint32_t max_generation = std::numeric_limits<std::uint32_t>::max();
    constexpr othello::search_detail::TranspositionScope scope{
        .mode = othello::SearchMode::FixedDepth,
        .eval_identity = 7,
    };

    CHECK(generation_age(1, max_generation) == 1);
    CHECK(generation_age(2, max_generation) == 2);
    CHECK(generation_age(1, max_generation - 1) == 2);
    CHECK(generation_age(max_generation, max_generation - 1) == 1);

    std::array<othello::search_detail::TranspositionEntry, 4> entries{
        tt_entry(11, scope, 1, 1),
        tt_entry(12, scope, max_generation, 1),
        tt_entry(13, scope, 1, 2),
        tt_entry(14, scope, 1, 2),
    };

    CHECK(select_replacement_entry(entries, 99, scope, 1, 1) == &entries[1]);
}

TEST_CASE("Search options can skip depth-zero transposition table stores", "[search]") {
    const Board board = Board::initial();

    const othello::SearchOptions with_leaf_store{
        .max_depth = 0,
        .use_transposition_table = true,
        .store_leaf_tt_entries = true,
        .exact_endgame_empty_threshold = 0,
    };
    const othello::SearchOptions without_leaf_store{
        .max_depth = 0,
        .use_transposition_table = true,
        .store_leaf_tt_entries = false,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult with_leaf = othello::search(board, with_leaf_store);
    const othello::SearchResult without_leaf = othello::search(board, without_leaf_store);

    CHECK(with_leaf.score == without_leaf.score);
    CHECK(with_leaf.best_move == without_leaf.best_move);
    CHECK(with_leaf.stats.tt_stores == 1);
    CHECK(with_leaf.stats.tt_leaf_stores == 1);
    CHECK(without_leaf.stats.tt_stores == 0);
    CHECK(without_leaf.stats.tt_leaf_stores == 0);
    CHECK(without_leaf.stats.tt_lookups == 1);
}

TEST_CASE("Skipping leaf transposition table stores preserves fixed-depth result", "[search]") {
    const auto board = othello::apply_move(Board::initial(), othello::test::square("d3"));
    REQUIRE(board.has_value());

    const othello::SearchOptions with_leaf_store{
        .max_depth = 1,
        .use_transposition_table = true,
        .store_leaf_tt_entries = true,
        .exact_endgame_empty_threshold = 0,
    };
    const othello::SearchOptions without_leaf_store{
        .max_depth = 1,
        .use_transposition_table = true,
        .store_leaf_tt_entries = false,
        .exact_endgame_empty_threshold = 0,
    };

    const othello::SearchResult with_leaf = othello::search(*board, with_leaf_store);
    const othello::SearchResult without_leaf = othello::search(*board, without_leaf_store);

    CHECK(with_leaf.best_move == without_leaf.best_move);
    CHECK(with_leaf.score == without_leaf.score);
    CHECK(with_leaf.depth == without_leaf.depth);
    CHECK(with_leaf.stats.tt_leaf_stores > 0);
    CHECK(without_leaf.stats.tt_leaf_stores == 0);
    CHECK(with_leaf.stats.tt_stores > without_leaf.stats.tt_stores);
    CHECK(without_leaf.stats.tt_stores > 0);
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
    CHECK(result.stats.tt_leaf_stores == 0);
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
    CHECK(result.stats.tt_leaf_stores > 0);
    CHECK(result.stats.tt_leaf_stores <= result.stats.tt_stores);
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

TEST_CASE("Midgame history bonus grows on beta cutoffs and stays capped", "[search]") {
    othello::search_detail::HistoryKillerState state;
    constexpr othello::search_detail::HistoryKillerOrderingParams params{
        .history_cap = 40,
        .history_max_bonus = 12,
        .killer_first_bonus = 0,
        .killer_second_bonus = 0,
    };

    othello::search_detail::record_history_cutoff(state, 18, 3, params);
    CHECK(state.history[18] == 9);
    CHECK(othello::search_detail::history_killer_bonus(state, 18, 3, params) == 9);

    for (int count = 0; count < 12; ++count) {
        othello::search_detail::record_history_cutoff(state, 18, 7, params);
    }

    CHECK(state.history[18] <= params.history_cap);
    CHECK(othello::search_detail::history_killer_bonus(state, 18, 7, params) <=
          params.history_max_bonus);
}

TEST_CASE("Midgame killer moves are depth-bucketed, unique, and deterministic", "[search]") {
    othello::search_detail::HistoryKillerState state;

    othello::search_detail::record_killer_cutoff(state, 10, 4);
    othello::search_detail::record_killer_cutoff(state, 20, 5);
    CHECK(state.killer_moves[4][0] == 10);
    CHECK(state.killer_moves[4][1] == -1);
    CHECK(state.killer_moves[5][0] == 20);
    CHECK(state.killer_moves[5][1] == -1);

    othello::search_detail::record_killer_cutoff(state, 11, 4);
    CHECK(state.killer_moves[4][0] == 11);
    CHECK(state.killer_moves[4][1] == 10);

    othello::search_detail::record_killer_cutoff(state, 10, 4);
    CHECK(state.killer_moves[4][0] == 10);
    CHECK(state.killer_moves[4][1] == 11);

    othello::search_detail::record_killer_cutoff(state, 10, 4);
    CHECK(state.killer_moves[4][0] == 10);
    CHECK(state.killer_moves[4][1] == 11);
}

TEST_CASE("Midgame history and killer signals reset with their session state", "[search]") {
    othello::search_detail::HistoryKillerState state;
    othello::search_detail::record_history_killer_cutoff(state, 42, 6);
    REQUIRE(state.history[42] > 0);
    REQUIRE(state.killer_moves[6][0] == 42);

    state.reset();

    CHECK(state.history[42] == 0);
    CHECK(state.killer_moves[6][0] == -1);
    CHECK(state.killer_moves[6][1] == -1);
}

TEST_CASE("Midgame ordering keeps PV promotion above history and killers", "[search]") {
    const Board board = Board::initial();

    const othello::SearchOptions warmup_options{
        .max_depth = 4,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };
    othello::SearchSession session{warmup_options};
    const othello::SearchResult warmup = othello::search_iterative(session, board, warmup_options);
    REQUIRE(warmup.best_move.has_value());

    std::vector<othello::RootMoveOrderingEntry> root_ordering;
    othello::SearchOptions ordered_options = warmup_options;
    ordered_options.use_transposition_table = false;
    ordered_options.root_move_ordering_snapshot = &root_ordering;
    const othello::SearchResult ordered =
        othello::search_iterative(session, board, ordered_options);

    REQUIRE(ordered.best_move.has_value());
    REQUIRE_FALSE(root_ordering.empty());
    CHECK(root_ordering.front().move == *ordered.best_move);
}

TEST_CASE("Midgame ordering uses square index as deterministic same-score tie-break", "[search]") {
    std::vector<othello::RootMoveOrderingEntry> root_ordering;
    const othello::SearchOptions options{
        .max_depth = 1,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .root_move_ordering_snapshot = &root_ordering,
    };

    static_cast<void>(othello::search(Board::initial(), options));

    REQUIRE(root_ordering.size() == 4);
    CHECK(root_ordering[0].move == othello::test::square("d3"));
    CHECK(root_ordering[1].move == othello::test::square("c4"));
    CHECK(root_ordering[2].move == othello::test::square("f5"));
    CHECK(root_ordering[3].move == othello::test::square("e6"));
    CHECK(root_ordering[0].order_score == root_ordering[1].order_score);
    CHECK(root_ordering[1].order_score == root_ordering[2].order_score);
    CHECK(root_ordering[2].order_score == root_ordering[3].order_score);
}

TEST_CASE("Fixed-depth search preserves representative midgame ordering snapshots", "[search]") {
    struct Case {
        Board board;
        othello::Square best_move;
        int score = 0;
        std::uint64_t nodes = 0;
        std::vector<othello::Square> principal_variation;
    };

    const std::vector<Case> cases{
        Case{
            .board = othello::test::board_from_text(R"(........
.B......
..B.B...
...BB...
.WWWWW..
..B.....
........
........
side=W)"),
            .best_move = othello::test::square("d6"),
            .score = 69,
            .nodes = 2535,
            .principal_variation = {othello::test::square("d6"), othello::test::square("a5"),
                                    othello::test::square("b6"), othello::test::square("b5"),
                                    othello::test::square("a4")},
        },
        Case{
            .board = othello::test::board_from_text(R"(.....W..
...WWW..
...WWW..
..WWWWBB
..WWWWBW
..WWWWWW
...WB...
...W....
side=B)"),
            .best_move = othello::test::square("e8"),
            .score = 195,
            .nodes = 3506,
            .principal_variation = {othello::test::square("e8"), othello::test::square("d8"),
                                    othello::test::square("g6"), othello::test::square("e1"),
                                    othello::test::square("c2")},
        },
    };

    const othello::SearchOptions options{
        .max_depth = 5,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = false,
    };

    for (const Case& test_case : cases) {
        const othello::SearchResult result = othello::search(test_case.board, options);

        REQUIRE(result.best_move.has_value());
        CHECK(*result.best_move == test_case.best_move);
        CHECK(result.score == test_case.score);
        CHECK(result.nodes == test_case.nodes);
        CHECK(result.principal_variation == test_case.principal_variation);
        CHECK(result.stats.dynamic_ordering_nodes > 0);
        CHECK(result.stats.dynamic_ordering_moves >= result.stats.dynamic_ordering_nodes * 5);
    }
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

    const othello::SearchResult full_window = othello::search_iterative(board, full_window_options);
    const othello::SearchResult aspirated = othello::search_iterative(board, aspiration_options);

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
    const othello::SearchResult aspirated = othello::search_iterative(*board, aspiration_options);

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

TEST_CASE("Score-delta-aware aspiration is opt-in and preserves iterative result", "[search]") {
    CHECK(othello::SearchOptions{}.aspiration_profile == othello::AspirationProfile::Fixed);

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
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };
    const othello::SearchOptions fixed_options{
        .max_depth = 6,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .use_aspiration_window = true,
        .aspiration_window = 50,
        .aspiration_profile = othello::AspirationProfile::Fixed,
    };
    const othello::SearchOptions score_delta_aware_options{
        .max_depth = 6,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .use_aspiration_window = true,
        .aspiration_window = 50,
        .aspiration_profile = othello::AspirationProfile::ScoreDeltaAware,
    };

    const othello::SearchResult full_window =
        othello::search_iterative(board, full_window_options);
    const othello::SearchResult fixed = othello::search_iterative(board, fixed_options);
    const othello::SearchResult score_delta_aware =
        othello::search_iterative(board, score_delta_aware_options);

    CHECK(fixed.best_move == full_window.best_move);
    CHECK(fixed.score == full_window.score);
    CHECK(fixed.depth == full_window.depth);
    CHECK(fixed.principal_variation == full_window.principal_variation);
    CHECK(score_delta_aware.best_move == full_window.best_move);
    CHECK(score_delta_aware.score == full_window.score);
    CHECK(score_delta_aware.depth == full_window.depth);
    CHECK(score_delta_aware.principal_variation == full_window.principal_variation);
    CHECK(score_delta_aware.stats.aspiration_searches > 0);
    CHECK(score_delta_aware.stats.aspiration_researches <= fixed.stats.aspiration_researches);
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
    CHECK(narrow.stats.aspiration_full_window_fallbacks <= narrow.stats.aspiration_researches);
    CHECK(narrow.stats.aspiration_fail_low_distance_sum +
              narrow.stats.aspiration_fail_high_distance_sum >
          0);
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
