#include "analyze/analysis.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

#include <algorithm>
#include <bit>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

using othello::Board;
using othello::Side;

namespace {

[[nodiscard]] othello::tools::analyze::AnalysisOptions candidate_options(int depth = 3) {
    return othello::tools::analyze::AnalysisOptions{
        .depth = depth,
        .mode = othello::tools::analyze::AnalysisMode::Fixed,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .root_candidates = true,
    };
}

[[nodiscard]] othello::EvaluationConfig corner_pattern_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
        .midgame = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
        .late = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
    };
}

[[nodiscard]] othello::EvaluationConfig edge_pattern_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
        .midgame = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
        .late = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
    };
}

[[nodiscard]] Board exact_analysis_board() {
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

[[nodiscard]] Board opening_a1_access_board() {
    return othello::test::board_from_text(R"(........
.B......
..B.B...
...BB...
.WWWWW..
..B.....
........
........
side=W)");
}

[[nodiscard]] Board early_corner_race_board() {
    return othello::test::board_from_text(R"(........
...W...W
....WBBB
...BBW..
...WW...
....W...
........
........
side=B)");
}

[[nodiscard]] Board midgame_normal_mobility_board() {
    return othello::test::board_from_text(R"(....WBBB
...WWWBW
....WBWW
...WWW.W
..WWW...
..B.W...
....W...
....W...
side=B)");
}

[[nodiscard]] Board midgame_white_wall_board() {
    return othello::test::board_from_text(R"(WWWWW.W.
WWWWWW..
BWWWWW..
..WBW...
.WWWWW..
W.W.WBBB
..W....B
..W....B
side=B)");
}

[[nodiscard]] bool sorted_by_score_then_move(
    const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates) noexcept {
    for (std::size_t index = 1; index < candidates.size(); ++index) {
        const auto& previous = candidates[index - 1];
        const auto& current = candidates[index];
        if (previous.score < current.score) {
            return false;
        }
        if (previous.score == current.score && previous.move.has_value() &&
            current.move.has_value() && previous.move->index() > current.move->index()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const othello::tools::analyze::RootCandidateAnalysis*
candidate_for_move(const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates,
                   std::optional<othello::Square> move) {
    if (!move.has_value()) {
        return nullptr;
    }
    const auto candidate = std::find_if(
        candidates.begin(), candidates.end(),
        [move](const othello::tools::analyze::RootCandidateAnalysis& current) {
            return current.move == move;
        });
    return candidate == candidates.end() ? nullptr : &*candidate;
}

void check_top_candidate_matches_result(const Board& board,
                                        const othello::tools::analyze::AnalysisOptions& options) {
    const othello::SearchResult result = othello::tools::analyze::run_search(board, options);
    const auto candidates = othello::tools::analyze::analyze_root_candidates(board, options);

    REQUIRE_FALSE(candidates.empty());
    CHECK(candidates.front().move == result.best_move);
    CHECK(candidates.front().score == result.score);
}

void check_candidate_score_semantics(const Board& board,
                                     const othello::tools::analyze::AnalysisOptions& options) {
    const auto candidates = othello::tools::analyze::analyze_root_candidates(board, options);

    REQUIRE_FALSE(candidates.empty());
    CHECK(sorted_by_score_then_move(candidates));
    for (const auto& candidate : candidates) {
        REQUIRE(candidate.move.has_value());
        CHECK((othello::legal_moves(board) & candidate.move->bit()) != 0);
        CHECK(candidate.score == -candidate.child_search.score);
    }
}

} // namespace

TEST_CASE("Root candidate analysis reports one row per legal move", "[analyze]") {
    const Board board = Board::initial();
    const Board before = board;

    const auto candidates =
        othello::tools::analyze::analyze_root_candidates(board, candidate_options());

    CHECK(othello::test::same_board(board, before));
    CHECK(candidates.size() == static_cast<std::size_t>(std::popcount(othello::legal_moves(board))));
    CHECK(sorted_by_score_then_move(candidates));
    for (const auto& candidate : candidates) {
        REQUIRE(candidate.move.has_value());
        CHECK((othello::legal_moves(board) & candidate.move->bit()) != 0);
        CHECK_FALSE(candidate.pass);
        CHECK(candidate.depth == 2);
        CHECK_FALSE(candidate.principal_variation.empty());
        CHECK(candidate.principal_variation.front() == *candidate.move);
        CHECK(candidate.score == -candidate.child_search.score);
        CHECK(candidate.child_search.nodes > 0);
        CHECK(candidate.evaluation_after_move.total ==
              othello::evaluate_basic(candidate.child_board, board.side_to_move));
    }
}

TEST_CASE("Root candidate analysis is deterministic for move order and scores", "[analyze]") {
    const Board board = Board::initial();
    const auto options = candidate_options();

    const auto first = othello::tools::analyze::analyze_root_candidates(board, options);
    const auto second = othello::tools::analyze::analyze_root_candidates(board, options);

    REQUIRE(first.size() == second.size());
    for (std::size_t index = 0; index < first.size(); ++index) {
        CHECK(first[index].move == second[index].move);
        CHECK(first[index].pass == second[index].pass);
        CHECK(first[index].score == second[index].score);
        CHECK(first[index].principal_variation == second[index].principal_variation);
        CHECK(first[index].evaluation_after_move.total == second[index].evaluation_after_move.total);
    }
}

TEST_CASE("Root candidate top can agree with the fixed-depth search result", "[analyze]") {
    othello::tools::analyze::AnalysisOptions options{
        .depth = 5,
        .mode = othello::tools::analyze::AnalysisMode::Fixed,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = false,
        .root_candidates = true,
    };

    check_top_candidate_matches_result(early_corner_race_board(), options);
    check_top_candidate_matches_result(midgame_white_wall_board(), options);
}

TEST_CASE("Root candidate scores are root-perspective independent child searches",
          "[analyze]") {
    othello::tools::analyze::AnalysisOptions options{
        .depth = 5,
        .mode = othello::tools::analyze::AnalysisMode::Fixed,
        .use_transposition_table = false,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = false,
        .root_candidates = true,
    };

    check_candidate_score_semantics(opening_a1_access_board(), options);

    const othello::SearchResult result =
        othello::tools::analyze::run_search(opening_a1_access_board(), options);
    const auto candidates =
        othello::tools::analyze::analyze_root_candidates(opening_a1_access_board(), options);
    REQUIRE_FALSE(candidates.empty());
    REQUIRE(result.best_move.has_value());
    REQUIRE(candidates.front().move.has_value());

    const auto* result_best_candidate = candidate_for_move(candidates, result.best_move);
    REQUIRE(result_best_candidate != nullptr);
    CHECK((othello::legal_moves(opening_a1_access_board()) & result.best_move->bit()) != 0);
}

TEST_CASE("Root candidate score signs survive PVS on and off", "[analyze]") {
    auto without_pvs = candidate_options(6);
    without_pvs.mode = othello::tools::analyze::AnalysisMode::Fixed;
    without_pvs.use_transposition_table = false;
    without_pvs.use_pvs = false;

    auto with_pvs = without_pvs;
    with_pvs.use_pvs = true;

    check_candidate_score_semantics(early_corner_race_board(), without_pvs);
    check_candidate_score_semantics(early_corner_race_board(), with_pvs);
}

TEST_CASE("Root candidate scores keep the same evaluation identity with TT on and off",
          "[analyze]") {
    auto without_tt = candidate_options(4);
    without_tt.mode = othello::tools::analyze::AnalysisMode::Fixed;
    without_tt.use_transposition_table = false;
    without_tt.use_pvs = false;
    without_tt.evaluator.config_override = edge_pattern_only_config(3);

    auto with_tt = without_tt;
    with_tt.use_transposition_table = true;

    const auto tt_off =
        othello::tools::analyze::analyze_root_candidates(midgame_normal_mobility_board(),
                                                         without_tt);
    const auto tt_on =
        othello::tools::analyze::analyze_root_candidates(midgame_normal_mobility_board(), with_tt);

    REQUIRE(tt_off.size() == tt_on.size());
    for (std::size_t index = 0; index < tt_off.size(); ++index) {
        CHECK(tt_off[index].move == tt_on[index].move);
        CHECK(tt_off[index].score == tt_on[index].score);
        CHECK(tt_off[index].evaluation_after_move.total ==
              tt_on[index].evaluation_after_move.total);
    }
}

TEST_CASE("Root candidate scores remain a legal table with shallow hints on and off",
          "[analyze]") {
    auto hint_off = candidate_options(5);
    hint_off.mode = othello::tools::analyze::AnalysisMode::Iterative;
    hint_off.use_transposition_table = true;
    hint_off.tt_min_probe_depth = 1;
    hint_off.tt_min_store_depth = 1;
    hint_off.use_lazy_first_move_ordering = true;
    hint_off.use_shallow_tt_move_ordering_hint = false;
    hint_off.use_pvs = true;
    hint_off.use_aspiration_window = true;
    hint_off.aspiration_profile = othello::AspirationProfile::ScoreDeltaAware;

    auto hint_on = hint_off;
    hint_on.use_shallow_tt_move_ordering_hint = true;

    check_candidate_score_semantics(opening_a1_access_board(), hint_off);
    check_candidate_score_semantics(opening_a1_access_board(), hint_on);
    check_top_candidate_matches_result(opening_a1_access_board(), hint_on);
}

TEST_CASE("Root candidate analysis reports pass candidate when root must pass", "[analyze]") {
    const Board board = othello::test::black_must_pass_board();
    REQUIRE(othello::legal_moves(board) == 0);
    REQUIRE(othello::pass_turn(board).has_value());

    const auto candidates =
        othello::tools::analyze::analyze_root_candidates(board, candidate_options());

    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().pass);
    CHECK_FALSE(candidates.front().move.has_value());
    CHECK(candidates.front().child_board.side_to_move == Side::White);
    CHECK(candidates.front().score == -candidates.front().child_search.score);
}

TEST_CASE("Root candidate analysis uses evaluation config override for breakdowns",
          "[analyze]") {
    const Board board = Board::initial();
    auto options = candidate_options(1);
    othello::EvaluationConfig config = othello::default_evaluation_config();
    config.opening.mobility = 123;
    options.evaluator.config_override = config;

    const auto candidates =
        othello::tools::analyze::analyze_root_candidates(board, options);

    REQUIRE_FALSE(candidates.empty());
    for (const auto& candidate : candidates) {
        CHECK(candidate.evaluation_after_move.mobility_weight == 123);
    }
}

TEST_CASE("Root candidate analysis reports no candidates for terminal root", "[analyze]") {
    const Board board{
        .black = ~othello::Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };
    REQUIRE(othello::is_game_over(board));

    const auto candidates =
        othello::tools::analyze::analyze_root_candidates(board, candidate_options());

    CHECK(candidates.empty());
}

TEST_CASE("Position analysis print includes corner pattern breakdown fields", "[analyze]") {
    const Board board = Board::initial();
    othello::tools::analyze::AnalysisOptions options{
        .depth = 1,
        .mode = othello::tools::analyze::AnalysisMode::Fixed,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .root_candidates = true,
    };
    options.evaluator.config_override = corner_pattern_only_config(4);
    const othello::SearchResult result = othello::tools::analyze::run_search(board, options);

    std::ostringstream captured;
    std::streambuf* const previous_buffer = std::cout.rdbuf(captured.rdbuf());
    othello::tools::analyze::print_report(board, options, result, std::chrono::nanoseconds{0});
    std::cout.rdbuf(previous_buffer);

    const std::string output = captured.str();
    CHECK(output.find("child_board:") != std::string::npos);
    CHECK(output.find("corner_2x3_pattern:") != std::string::npos);
    CHECK(output.find("corner_2x3_pattern_weight:") != std::string::npos);
    CHECK(output.find("corner_2x3_pattern_score:") != std::string::npos);
}

TEST_CASE("Position analysis print includes edge 8 pattern breakdown fields", "[analyze]") {
    const Board board = Board::initial();
    othello::tools::analyze::AnalysisOptions options{
        .depth = 1,
        .mode = othello::tools::analyze::AnalysisMode::Fixed,
        .use_transposition_table = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
    };
    options.evaluator.config_override = edge_pattern_only_config(4);
    const othello::SearchResult result = othello::tools::analyze::run_search(board, options);

    std::ostringstream captured;
    std::streambuf* const previous_buffer = std::cout.rdbuf(captured.rdbuf());
    othello::tools::analyze::print_report(board, options, result, std::chrono::nanoseconds{0});
    std::cout.rdbuf(previous_buffer);

    const std::string output = captured.str();
    CHECK(output.find("edge_8_pattern:") != std::string::npos);
    CHECK(output.find("edge_8_pattern_weight:") != std::string::npos);
    CHECK(output.find("edge_8_pattern_score:") != std::string::npos);
}

TEST_CASE("Position analysis print includes search score semantics", "[analyze]") {
    const Board board = Board::initial();
    othello::tools::analyze::AnalysisOptions options{
        .depth = 1,
        .mode = othello::tools::analyze::AnalysisMode::Fixed,
        .exact_endgame_empty_threshold = 0,
    };
    const othello::SearchResult result = othello::tools::analyze::run_search(board, options);

    std::ostringstream captured;
    std::streambuf* const previous_buffer = std::cout.rdbuf(captured.rdbuf());
    othello::tools::analyze::print_report(board, options, result, std::chrono::nanoseconds{0});
    std::cout.rdbuf(previous_buffer);

    const std::string output = captured.str();
    CHECK(output.find("score_kind: heuristic") != std::string::npos);
    CHECK(output.find("used_exact_endgame: no") != std::string::npos);
    CHECK(output.find("exact_disc_margin: none") != std::string::npos);
}

TEST_CASE("Position analysis print includes diagnostic search options", "[analyze]") {
    const Board board = Board::initial();
    othello::tools::analyze::AnalysisOptions options{
        .depth = 1,
        .mode = othello::tools::analyze::AnalysisMode::Iterative,
        .use_transposition_table = true,
        .use_lazy_first_move_ordering = true,
        .use_shallow_tt_move_ordering_hint = true,
        .exact_endgame_empty_threshold = 0,
        .use_pvs = true,
        .use_aspiration_window = true,
        .aspiration_profile = othello::AspirationProfile::ScoreDeltaAware,
    };
    const othello::SearchResult result = othello::tools::analyze::run_search(board, options);

    std::ostringstream captured;
    std::streambuf* const previous_buffer = std::cout.rdbuf(captured.rdbuf());
    othello::tools::analyze::print_report(board, options, result, std::chrono::nanoseconds{0});
    std::cout.rdbuf(previous_buffer);

    const std::string output = captured.str();
    CHECK(output.find("lazy_first_move_ordering: on") != std::string::npos);
    CHECK(output.find("shallow_tt_move_ordering_hint: on") != std::string::npos);
    CHECK(output.find("aspiration: on") != std::string::npos);
    CHECK(output.find("aspiration_profile: score-delta-aware") != std::string::npos);
}

TEST_CASE("Position analysis print includes exact root search score semantics", "[analyze]") {
    const Board board = exact_analysis_board();
    const othello::ExactEndgameResult exact = othello::solve_exact_endgame(board);
    othello::tools::analyze::AnalysisOptions options{
        .depth = 1,
        .mode = othello::tools::analyze::AnalysisMode::Fixed,
        .exact_endgame_empty_threshold = 1,
    };
    const othello::SearchResult result = othello::tools::analyze::run_search(board, options);

    std::ostringstream captured;
    std::streambuf* const previous_buffer = std::cout.rdbuf(captured.rdbuf());
    othello::tools::analyze::print_report(board, options, result, std::chrono::nanoseconds{0});
    std::cout.rdbuf(previous_buffer);

    const std::string output = captured.str();
    CHECK(output.find("score_kind: exact_disc_margin_scaled") != std::string::npos);
    CHECK(output.find("used_exact_endgame: yes") != std::string::npos);
    CHECK(output.find("exact_disc_margin: " + std::to_string(exact.disc_margin)) !=
          std::string::npos);
}
