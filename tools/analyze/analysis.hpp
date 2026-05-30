#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::analyze {

enum class AnalysisMode {
    Fixed,
    Iterative,
};

struct AnalysisOptions {
    std::optional<std::string> board_file;
    bool read_stdin = false;
    int depth = 10;
    AnalysisMode mode = AnalysisMode::Fixed;
    bool use_transposition_table = true;
    std::size_t transposition_table_entries = SearchOptions{}.transposition_table_entries;
    int exact_endgame_empty_threshold = SearchOptions{}.exact_endgame_empty_threshold;
    bool use_pvs = SearchOptions{}.use_pvs;
    bool use_aspiration_window = SearchOptions{}.use_aspiration_window;
    int aspiration_window = SearchOptions{}.aspiration_window;
    int aspiration_max_researches = SearchOptions{}.aspiration_max_researches;
    EvaluationPreset evaluation_preset = EvaluationPreset::Default;
    std::optional<EvaluationConfig> evaluation_config_override;
    std::optional<std::string> evaluation_config_path;
    bool root_candidates = false;
};

struct RootCandidateAnalysis {
    std::optional<Square> move;
    bool pass = false;
    Board child_board = Board::initial();
    int depth = 0;
    int score = 0;
    SearchResult child_search;
    std::vector<Square> principal_variation;
    EvaluationBreakdown evaluation_after_move;
    std::chrono::nanoseconds elapsed = std::chrono::nanoseconds{0};
};

[[nodiscard]] std::string_view mode_name(AnalysisMode mode) noexcept;
[[nodiscard]] SearchResult run_search(const Board& board, const AnalysisOptions& options) noexcept;
[[nodiscard]] std::vector<RootCandidateAnalysis>
analyze_root_candidates(const Board& board, const AnalysisOptions& options);
void print_report(const Board& board, const AnalysisOptions& options, const SearchResult& result,
                  std::chrono::nanoseconds elapsed);

} // namespace othello::tools::analyze
