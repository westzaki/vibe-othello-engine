#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <string_view>

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
};

[[nodiscard]] std::string_view mode_name(AnalysisMode mode) noexcept;
[[nodiscard]] SearchResult run_search(const Board& board, const AnalysisOptions& options) noexcept;
void print_report(const Board& board, const AnalysisOptions& options, const SearchResult& result,
                  std::chrono::nanoseconds elapsed);

} // namespace othello::tools::analyze
