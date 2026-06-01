#include "analyze/analysis.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

#include <bit>
#include <iostream>
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
    options.evaluator.config_override =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::CornerPattern2x3V1);
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
    options.evaluator.config_override =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::EdgePattern8V1);
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
