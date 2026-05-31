#include "eval_vs_exact/eval_vs_exact.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace {

[[nodiscard]] std::string read_fixture(const std::filesystem::path& relative_path) {
    const std::filesystem::path path = std::filesystem::path{OTHELLO_SOURCE_DIR} / relative_path;
    std::ifstream input{path};
    REQUIRE(input);
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] othello::tools::eval_vs_exact::AnalyzerOptions default_options() {
    return othello::tools::eval_vs_exact::AnalyzerOptions{
        .labels_path =
            std::filesystem::path{OTHELLO_SOURCE_DIR} / "data/labels/exact_label_tiny.jsonl",
        .evaluator = {.preset = othello::EvaluationPreset::Default},
        .top = 10,
        .phase_breakdown = true,
        .include_positions = true,
        .timestamp = "2026-05-31T00:00:00Z",
        .source_sha = "test",
        .command = "othello_eval_vs_exact --labels fixture --eval-preset default",
    };
}

[[nodiscard]] othello::EvaluationConfig zero_config() {
    othello::EvaluationConfig config{};
    config.opening = {};
    config.midgame = {};
    config.late = {};
    return config;
}

[[nodiscard]] othello::EvaluationConfig potential_mobility_only_config() {
    othello::EvaluationConfig config = zero_config();
    config.opening.potential_mobility = 1;
    config.midgame.potential_mobility = 1;
    config.late.potential_mobility = 1;
    return config;
}

[[nodiscard]] othello::tools::eval_vs_exact::AnalyzerOptions
move_rank_options(const othello::EvaluationConfig& config) {
    auto options = default_options();
    options.move_rank_analysis = true;
    options.evaluator.config_override = config;
    options.evaluator.config_path = "test.eval";
    options.command += " --move-rank-analysis --eval-config test.eval";
    return options;
}

} // namespace

TEST_CASE("Eval vs exact analyzer reports tiny exact-label fixture", "[eval-vs-exact]") {
    const std::string labels = read_fixture("data/labels/exact_label_tiny.jsonl");
    std::string error;

    const std::optional<othello::tools::eval_vs_exact::AnalyzerReport> report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, default_options(), error);

    REQUIRE(report.has_value());
    CHECK(report->summary.records_read == 3);
    CHECK(report->summary.analyzed == 3);
    CHECK(report->summary.exact_wins == 2);
    CHECK(report->summary.exact_losses == 1);

    const std::string& markdown = report->markdown;
    CHECK(markdown.contains("# Eval vs Exact Report"));
    CHECK(markdown.contains("No strength claim"));
    CHECK(markdown.contains("heuristic units"));
    CHECK(markdown.contains("high_confidence_threshold: 250"));
    CHECK(markdown.contains("Threshold: `abs(eval_score) >= 250`"));
    CHECK(markdown.contains("v1 fail-fast"));
    CHECK(markdown.contains("sign_agreement_rate"));
    CHECK(markdown.contains("high_confidence_wrong_direction_count"));
    CHECK(markdown.contains("## Worst Wrong-Direction Positions"));
    CHECK(markdown.contains("## High-Confidence Disagreements"));
    CHECK(markdown.contains("### By Evaluator Phase"));
    CHECK_FALSE(markdown.contains("## Move-Rank Analysis"));
    CHECK(report->summary.move_rank_analyzed == 0);
}

TEST_CASE("Eval vs exact analyzer reports move-rank analysis when enabled",
          "[eval-vs-exact]") {
    const std::string labels = read_fixture("data/labels/exact_label_tiny.jsonl");
    auto options = default_options();
    options.move_rank_analysis = true;
    options.command += " --move-rank-analysis";
    std::string error;

    const std::optional<othello::tools::eval_vs_exact::AnalyzerReport> report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    REQUIRE(report.has_value());
    CHECK(report->summary.move_rank_records_with_scores == 1);
    CHECK(report->summary.move_rank_records_missing_scores == 2);
    CHECK(report->summary.move_rank_analyzed == 1);
    CHECK(report->summary.move_rank_top_exact_best == 1);

    const std::string& markdown = report->markdown;
    CHECK(markdown.contains("## Move-Rank Analysis"));
    CHECK(markdown.contains("move_rank_analysis: true"));
    CHECK(markdown.contains("evaluator_top_score_group_exact_best_rate"));
    CHECK(markdown.contains("exact_best_move_rank_under_evaluator"));
    CHECK(markdown.contains("Worst Evaluator Top-Move Misses"));
    CHECK(markdown.contains("records did not include `move_scores`"));
}

TEST_CASE("Eval vs exact move-rank analysis handles missing move scores",
          "[eval-vs-exact]") {
    constexpr std::string_view labels =
        "{\"schema\":\"exact_label.v1\",\"position_id\":\"pos-000001\","
        "\"board\":\"BBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\n"
        "BBBBBBBB\\nBBBBBBBB\\nside=W\",\"side_to_move\":\"W\",\"empties\":0,"
        "\"legal_moves\":[],\"exact_score_side_to_move\":-64,\"best_moves\":[],"
        "\"best_move\":null}\n";
    auto options = default_options();
    options.move_rank_analysis = true;
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    REQUIRE(report.has_value());
    CHECK(report->summary.move_rank_records_missing_scores == 1);
    CHECK(report->summary.move_rank_analyzed == 0);
    CHECK(report->markdown.contains("no records with usable `move_scores` were available"));
}

TEST_CASE("Eval vs exact move-rank analysis treats evaluator top ties as a group",
          "[eval-vs-exact]") {
    constexpr std::string_view labels = R"json({"schema":"exact_label.v1","position_id":"pos-tie-group","board":"BBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\n.WBBBBW.\nside=B","side_to_move":"B","empties":2,"legal_moves":["A1","H1"],"exact_score_side_to_move":64,"best_moves":["H1"],"best_move":"H1","move_scores":[{"move":"A1","exact_score_side_to_move":0},{"move":"H1","exact_score_side_to_move":64}]}
)json";
    const auto options = move_rank_options(zero_config());
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    REQUIRE(report.has_value());
    CHECK(report->summary.move_rank_analyzed == 1);
    CHECK(report->summary.move_rank_top_exact_best == 1);
    CHECK(report->summary.move_rank_top_non_best == 0);
    CHECK(report->summary.move_rank_exact_best_rank_sum == 1);
    CHECK(report->summary.move_rank_eval_score_gap_sum == 0);
    CHECK(report->summary.move_rank_exact_score_gap_sum == 0);

    const std::string& markdown = report->markdown;
    CHECK(markdown.contains("evaluator_selected_top_move: `A1`"));
    CHECK(markdown.contains("evaluator_top_score_group: `A1` `H1`"));
    CHECK(markdown.contains("exact_best_in_evaluator_top_score_group: true"));
    CHECK(markdown.contains("None."));
}

TEST_CASE("Eval vs exact move-rank analysis reports non-best top-group misses",
          "[eval-vs-exact]") {
    constexpr std::string_view labels = R"json({"schema":"exact_label.v1","position_id":"pos-non-best-top","board":"........\n........\n..WWW...\n..WBW...\n..WWW...\n........\n........\n........\nside=B","side_to_move":"B","empties":55,"legal_moves":["B3","D3","F3","B5","F5","B7","D7","F7"],"exact_score_side_to_move":10,"best_moves":["B7"],"best_move":"B7","move_scores":[{"move":"B3","exact_score_side_to_move":3},{"move":"D3","exact_score_side_to_move":0},{"move":"F3","exact_score_side_to_move":3},{"move":"B5","exact_score_side_to_move":0},{"move":"F5","exact_score_side_to_move":0},{"move":"B7","exact_score_side_to_move":10},{"move":"D7","exact_score_side_to_move":0},{"move":"F7","exact_score_side_to_move":3}]}
)json";
    const auto options = move_rank_options(potential_mobility_only_config());
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    REQUIRE(report.has_value());
    CHECK(report->summary.move_rank_analyzed == 1);
    CHECK(report->summary.move_rank_top_exact_best == 0);
    CHECK(report->summary.move_rank_top_non_best == 1);
    CHECK(report->summary.move_rank_exact_best_rank_sum == 5);
    CHECK(report->summary.move_rank_eval_score_gap_sum == 4);
    CHECK(report->summary.move_rank_exact_score_gap_sum == 10);

    const std::string& markdown = report->markdown;
    CHECK(markdown.contains("### 1. pos-non-best-top"));
    CHECK(markdown.contains("evaluator_top_score_group: `B5` `D3` `D7` `F5`"));
    CHECK(markdown.contains("exact_best_in_evaluator_top_score_group: false"));
    CHECK(markdown.contains("exact_best_move_rank_under_evaluator: 5"));
    CHECK(markdown.contains("evaluator_score_gap_top_minus_exact_best: 4 heuristic units"));
    CHECK(markdown.contains("exact_score_gap_exact_best_minus_top_group: 10 discs"));
    CHECK(markdown.contains("`1:B5 eval=10 exact=0`"));
    CHECK(markdown.contains("`6:B7 eval=6 exact=10 exact-best`"));
}

TEST_CASE("Eval vs exact move-rank analysis supports PASS root scores",
          "[eval-vs-exact]") {
    constexpr std::string_view labels = R"json({"schema":"exact_label.v1","position_id":"pos-pass-root","board":"BBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBBBB\nBBBBBWB.\nside=B","side_to_move":"B","empties":1,"legal_moves":["PASS"],"exact_score_side_to_move":58,"best_moves":["PASS"],"best_move":"PASS","move_scores":[{"move":"PASS","exact_score_side_to_move":58}]}
)json";
    const auto options = move_rank_options(zero_config());
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    REQUIRE(report.has_value());
    CHECK(report->summary.move_rank_records_with_scores == 1);
    CHECK(report->summary.move_rank_records_no_legal_moves == 0);
    CHECK(report->summary.move_rank_analyzed == 1);
    CHECK(report->summary.move_rank_top_exact_best == 1);
    CHECK(report->markdown.contains("`1:PASS eval=0 exact=58 exact-best`"));
}

TEST_CASE("Eval vs exact analyzer rejects unsupported schemas", "[eval-vs-exact]") {
    const std::string labels = read_fixture("data/labels/exact_label_unsupported_schema.jsonl");
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, default_options(), error);

    CHECK_FALSE(report.has_value());
    CHECK(error.contains("unsupported schema"));
}

TEST_CASE("Eval vs exact analyzer rejects missing required fields", "[eval-vs-exact]") {
    const std::string labels = read_fixture("data/labels/exact_label_missing_required.jsonl");
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, default_options(), error);

    CHECK_FALSE(report.has_value());
    CHECK(error.contains("missing required field: best_move"));
}

TEST_CASE("Eval vs exact analyzer rejects malformed move scores", "[eval-vs-exact]") {
    constexpr std::string_view labels =
        "{\"schema\":\"exact_label.v1\",\"position_id\":\"pos-000001\","
        "\"board\":\"BBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\n"
        "BBBBBBBB\\nBBBBBBW.\\nside=B\",\"side_to_move\":\"B\",\"empties\":1,"
        "\"legal_moves\":[\"H1\"],\"exact_score_side_to_move\":64,"
        "\"best_moves\":[\"H1\"],\"best_move\":\"H1\","
        "\"move_scores\":[{\"move\":\"H1\"}]}\n";
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, default_options(), error);

    CHECK_FALSE(report.has_value());
    CHECK(error.contains("missing required move_scores field: exact_score_side_to_move"));
}

TEST_CASE("Eval vs exact move-rank analysis rejects missing legal move scores",
          "[eval-vs-exact]") {
    constexpr std::string_view labels = R"json({"schema":"exact_label.v1","position_id":"pos-missing-score","board":"........\n........\n..WWW...\n..WBW...\n..WWW...\n........\n........\n........\nside=B","side_to_move":"B","empties":55,"legal_moves":["B3","D3","F3","B5","F5","B7","D7","F7"],"exact_score_side_to_move":10,"best_moves":["B7"],"best_move":"B7","move_scores":[{"move":"B3","exact_score_side_to_move":3},{"move":"D3","exact_score_side_to_move":0},{"move":"F3","exact_score_side_to_move":3},{"move":"B5","exact_score_side_to_move":0},{"move":"F5","exact_score_side_to_move":0},{"move":"D7","exact_score_side_to_move":0},{"move":"F7","exact_score_side_to_move":3}]}
)json";
    const auto options = move_rank_options(potential_mobility_only_config());
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    CHECK_FALSE(report.has_value());
    CHECK(error.contains("move_scores missing legal move: B7"));
}

TEST_CASE("Eval vs exact move-rank analysis rejects illegal move scores",
          "[eval-vs-exact]") {
    constexpr std::string_view labels =
        "{\"schema\":\"exact_label.v1\",\"position_id\":\"pos-illegal-score\","
        "\"board\":\"BBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\n"
        "BBBBBBBB\\nBBBBBBW.\\nside=B\",\"side_to_move\":\"B\",\"empties\":1,"
        "\"legal_moves\":[\"H1\"],\"exact_score_side_to_move\":64,"
        "\"best_moves\":[\"H1\"],\"best_move\":\"H1\","
        "\"move_scores\":[{\"move\":\"H1\",\"exact_score_side_to_move\":64},"
        "{\"move\":\"A1\",\"exact_score_side_to_move\":0}]}\n";
    const auto options = move_rank_options(zero_config());
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    CHECK_FALSE(report.has_value());
    CHECK(error.contains("move_scores contains illegal move: A1"));
}

TEST_CASE("Eval vs exact move-rank analysis rejects duplicate move scores",
          "[eval-vs-exact]") {
    constexpr std::string_view labels =
        "{\"schema\":\"exact_label.v1\",\"position_id\":\"pos-duplicate-score\","
        "\"board\":\"BBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\n"
        "BBBBBBBB\\nBBBBBBW.\\nside=B\",\"side_to_move\":\"B\",\"empties\":1,"
        "\"legal_moves\":[\"H1\"],\"exact_score_side_to_move\":64,"
        "\"best_moves\":[\"H1\"],\"best_move\":\"H1\","
        "\"move_scores\":[{\"move\":\"H1\",\"exact_score_side_to_move\":64},"
        "{\"move\":\"H1\",\"exact_score_side_to_move\":63}]}\n";
    const auto options = move_rank_options(zero_config());
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, options, error);

    CHECK_FALSE(report.has_value());
    CHECK(error.contains("duplicate move in move_scores: H1"));
}

TEST_CASE("Eval vs exact analyzer rejects boards that do not match label side", "[eval-vs-exact]") {
    constexpr std::string_view labels =
        "{\"schema\":\"exact_label.v1\",\"position_id\":\"pos-000001\","
        "\"board\":\"BBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\nBBBBBBBB\\n"
        "BBBBBBBB\\nBBBBBBBB\\nside=W\",\"side_to_move\":\"B\",\"empties\":0,"
        "\"legal_moves\":[],\"exact_score_side_to_move\":-64,\"best_moves\":[],"
        "\"best_move\":null}\n";
    std::string error;

    const auto report =
        othello::tools::eval_vs_exact::analyze_exact_label_jsonl(labels, default_options(), error);

    CHECK_FALSE(report.has_value());
    CHECK(error.contains("side_to_move mismatch"));
}
