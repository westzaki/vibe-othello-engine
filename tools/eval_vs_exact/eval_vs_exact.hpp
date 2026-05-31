#pragma once

#include "common/evaluator_selection.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace othello::tools::eval_vs_exact {

struct AnalyzerOptions {
    std::filesystem::path labels_path;
    EvaluatorSelection evaluator;
    std::size_t top = 10;
    int high_confidence_threshold = 250;
    bool phase_breakdown = false;
    bool include_positions = false;
    bool move_rank_analysis = false;
    std::string timestamp;
    std::string source_sha = "unknown";
    std::string command;
};

struct AnalyzerSummary {
    std::size_t records_read = 0;
    std::size_t analyzed = 0;
    std::size_t skipped = 0;
    std::size_t exact_wins = 0;
    std::size_t exact_losses = 0;
    std::size_t exact_draws = 0;
    std::size_t eval_positive = 0;
    std::size_t eval_negative = 0;
    std::size_t eval_zero = 0;
    std::size_t sign_agreements = 0;
    std::size_t sign_disagreements = 0;
    std::size_t wrong_direction = 0;
    std::size_t high_confidence_wrong_direction = 0;
    std::size_t exact_draw_handling = 0;
    std::size_t move_rank_records_with_scores = 0;
    std::size_t move_rank_records_missing_scores = 0;
    std::size_t move_rank_records_no_legal_moves = 0;
    std::size_t move_rank_analyzed = 0;
    std::size_t move_rank_top_exact_best = 0;
    std::size_t move_rank_top_non_best = 0;
    std::size_t move_rank_exact_best_rank_sum = 0;
    long long move_rank_eval_score_gap_sum = 0;
    long long move_rank_exact_score_gap_sum = 0;
};

struct AnalyzerReport {
    AnalyzerSummary summary;
    std::string markdown;
};

[[nodiscard]] std::optional<AnalyzerReport>
analyze_exact_label_jsonl(std::string_view text, const AnalyzerOptions& options,
                          std::string& error);

[[nodiscard]] std::string_view phase_name(EvaluationPhase phase) noexcept;

} // namespace othello::tools::eval_vs_exact
