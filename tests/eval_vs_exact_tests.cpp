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
