#pragma once

#include "benchmarks/search_bench_options.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace othello::benchmarks {
struct Position;
}

namespace othello::benchmarks::search_bench {

struct RootMoveOrderingDiagnostic {
    std::vector<othello::RootMoveOrderingEntry> moves;
    std::optional<int> best_move_initial_order_rank;
};

struct IterativeDepthBenchmarkResult {
    std::string_view position_name;
    std::string_view phase;
    std::string_view tags;
    SearchRunMode mode;
    bool use_transposition_table;
    bool store_leaf_tt_entries;
    bool use_pvs;
    bool use_aspiration_window;
    int aspiration_window;
    int aspiration_max_researches;
    othello::AspirationProfile aspiration_profile;
    std::size_t transposition_table_entries;
    std::string exact_root_profile;
    int empty_count;
    bool exact_root;
    std::string exact_skip_reason;
    int requested_depth;
    int completed_depth;
    std::optional<int> previous_score;
    int score = 0;
    int previous_score_delta = 0;
    std::optional<othello::Square> best_move;
    std::vector<othello::Square> principal_variation;
    std::chrono::nanoseconds elapsed;
    std::uint64_t nodes = 0;
    othello::SearchStats stats;
    std::uint64_t result_checksum = 0;
    std::uint64_t work_checksum = 0;
};

struct SearchBenchmarkResult {
    std::string_view name;
    SearchRunMode mode;
    bool use_transposition_table;
    bool store_leaf_tt_entries;
    bool use_pvs;
    bool use_aspiration_window;
    int aspiration_window;
    int aspiration_max_researches;
    othello::AspirationProfile aspiration_profile;
    std::size_t transposition_table_entries;
    std::string exact_root_profile;
    std::uint64_t exact_root_positions;
    std::uint64_t exact_root_searches;
    std::uint64_t position_count;
    int depth;
    std::optional<othello::Square> sample_best_move;
    int sample_score;
    othello::SearchScoreKind sample_score_kind = othello::SearchScoreKind::Heuristic;
    bool sample_used_exact_endgame = false;
    std::optional<int> sample_exact_disc_margin;
    std::vector<othello::Square> sample_principal_variation;
    std::uint64_t searches;
    std::chrono::nanoseconds elapsed;
    std::uint64_t total_nodes;
    othello::SearchStats total_stats;
    std::uint64_t result_checksum;
    std::uint64_t work_checksum;
    std::vector<IterativeDepthBenchmarkResult> iterative_depth_rows;
};

struct PositionBenchmarkResult {
    std::string_view position_name;
    std::string_view phase;
    std::string_view tags;
    SearchRunMode mode;
    bool use_transposition_table;
    bool store_leaf_tt_entries;
    bool use_pvs;
    bool use_aspiration_window;
    int aspiration_window;
    int aspiration_max_researches;
    othello::AspirationProfile aspiration_profile;
    std::size_t transposition_table_entries;
    std::string exact_root_profile;
    int empty_count;
    bool exact_root;
    std::string exact_skip_reason;
    int depth;
    std::optional<othello::Square> sample_best_move;
    int sample_score;
    othello::SearchScoreKind sample_score_kind = othello::SearchScoreKind::Heuristic;
    bool sample_used_exact_endgame = false;
    std::optional<int> sample_exact_disc_margin;
    std::vector<othello::Square> sample_principal_variation;
    std::uint64_t searches;
    std::chrono::nanoseconds elapsed;
    std::uint64_t total_nodes;
    othello::SearchStats total_stats;
    std::uint64_t result_checksum;
    std::uint64_t work_checksum;
    std::vector<IterativeDepthBenchmarkResult> iterative_depth_rows;
    RootMoveOrderingDiagnostic root_move_ordering;
};

struct BenchmarkExactRootDecision {
    bool solve_exact = false;
    int empty_count = 0;
    int legal_moves_current = 0;
    int legal_moves_opponent = 0;
    std::string_view skip_reason = "-";
};

[[nodiscard]] std::optional<std::vector<othello::benchmarks::Position>>
make_positions(PositionSet position_set);
[[nodiscard]] int root_empty_count(const othello::Board& board) noexcept;
[[nodiscard]] bool profile_uses_engine_gate(const ExactRootProfile& profile) noexcept;
[[nodiscard]] std::string_view
exact_root_skip_reason_name(othello::ExactEndgameRootSkipReason reason) noexcept;
[[nodiscard]] BenchmarkExactRootDecision
benchmark_exact_root_decision(const othello::Board& board, const othello::SearchOptions& options,
                              const ExactRootProfile& profile) noexcept;
[[nodiscard]] othello::SearchOptions
make_search_options(const BenchmarkOptions& options, int depth,
                    const ExactRootProfile& exact_root_profile) noexcept;
[[nodiscard]] othello::SearchResult run_search(const othello::Board& board,
                                               const othello::SearchOptions& options,
                                               SearchRunMode mode,
                                               const ExactRootProfile& exact_root_profile);
[[nodiscard]] SearchBenchmarkResult
benchmark_search(const std::vector<othello::benchmarks::Position>& positions, int depth,
                 std::uint64_t repetitions, const BenchmarkOptions& benchmark_options,
                 SearchRunMode mode, const ExactRootProfile& exact_root_profile);
[[nodiscard]] PositionBenchmarkResult
benchmark_position(const othello::benchmarks::Position& position, int depth,
                   std::uint64_t repetitions, const BenchmarkOptions& benchmark_options,
                   SearchRunMode mode, const ExactRootProfile& exact_root_profile);

} // namespace othello::benchmarks::search_bench
