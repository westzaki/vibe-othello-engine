#pragma once

#include "benchmarks/search_bench_options.hpp"
#include "benchmarks/search_bench_runner.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace othello::benchmarks {
struct Position;
}

namespace othello::benchmarks::search_bench {

[[nodiscard]] bool describe_positions(const std::vector<othello::benchmarks::Position>& positions);

void print_search_result_header();
void print_search_result(const SearchBenchmarkResult& result);
void print_position_result_header();
void print_position_result(const PositionBenchmarkResult& result);
void write_search_jsonl(const SearchBenchmarkResult& result, PositionSet position_set,
                        std::uint64_t repetitions);
void write_iterative_depth_jsonl(const IterativeDepthBenchmarkResult& result,
                                 PositionSet position_set, std::uint64_t repetitions);
void write_position_jsonl(const PositionBenchmarkResult& result, PositionSet position_set,
                          std::uint64_t repetitions, bool include_instrumentation);
void print_position_summary_header();
void print_position_summary(std::span<const PositionBenchmarkResult> results, SearchRunMode mode,
                            int depth, std::string_view exact_root_profile);

} // namespace othello::benchmarks::search_bench
