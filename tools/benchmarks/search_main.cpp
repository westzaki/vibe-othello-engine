#include "benchmarks/search_bench_options.hpp"
#include "benchmarks/search_bench_reporter.hpp"
#include "benchmarks/search_bench_runner.hpp"
#include "common/search_cli_options.hpp"
#include "positions/fixtures.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using othello::benchmarks::search_bench::benchmark_position;
using othello::benchmarks::search_bench::benchmark_search;
using othello::benchmarks::search_bench::BenchmarkOptions;
using othello::benchmarks::search_bench::describe_positions;
using othello::benchmarks::search_bench::exact_root_profile_list_text;
using othello::benchmarks::search_bench::ExactRootProfile;
using othello::benchmarks::search_bench::make_positions;
using othello::benchmarks::search_bench::mode_name;
using othello::benchmarks::search_bench::parse_options;
using othello::benchmarks::search_bench::position_set_name;
using othello::benchmarks::search_bench::PositionBenchmarkResult;
using othello::benchmarks::search_bench::print_position_result;
using othello::benchmarks::search_bench::print_position_result_header;
using othello::benchmarks::search_bench::print_position_summary;
using othello::benchmarks::search_bench::print_position_summary_header;
using othello::benchmarks::search_bench::print_search_result;
using othello::benchmarks::search_bench::print_search_result_header;
using othello::benchmarks::search_bench::print_usage;
using othello::benchmarks::search_bench::SearchBenchmarkMode;
using othello::benchmarks::search_bench::SearchRunMode;
using othello::benchmarks::search_bench::write_iterative_depth_jsonl;
using othello::benchmarks::search_bench::write_position_jsonl;
using othello::benchmarks::search_bench::write_search_jsonl;
using othello::tools::OutputFormat;

void run_requested_benchmarks(const std::vector<othello::benchmarks::Position>& positions,
                              const BenchmarkOptions& options, int depth,
                              const ExactRootProfile& exact_root_profile) {
    const auto run_mode = [&](SearchRunMode mode) {
        auto result = benchmark_search(positions, depth, options.repetitions, options, mode,
                                       exact_root_profile);
        if (options.output_format == OutputFormat::Jsonl) {
            if (options.emit_iterative_depth_rows) {
                for (const auto& row : result.iterative_depth_rows) {
                    write_iterative_depth_jsonl(row, options.position_set, options.repetitions);
                }
            }
            write_search_jsonl(result, options.position_set, options.repetitions);
        } else {
            print_search_result(result);
        }
    };

    if (options.mode == SearchBenchmarkMode::Fixed || options.mode == SearchBenchmarkMode::Both) {
        run_mode(SearchRunMode::Fixed);
    }

    if (options.mode == SearchBenchmarkMode::Iterative ||
        options.mode == SearchBenchmarkMode::Both) {
        run_mode(SearchRunMode::Iterative);
    }
}

void run_requested_position_benchmarks(const std::vector<othello::benchmarks::Position>& positions,
                                       const BenchmarkOptions& options, int depth,
                                       const ExactRootProfile& exact_root_profile,
                                       std::vector<PositionBenchmarkResult>& results) {
    const auto run_mode = [&](SearchRunMode mode) {
        for (const auto& position : positions) {
            auto result = benchmark_position(position, depth, options.repetitions, options, mode,
                                             exact_root_profile);
            if (options.output_format == OutputFormat::Jsonl) {
                if (options.emit_iterative_depth_rows) {
                    for (const auto& row : result.iterative_depth_rows) {
                        write_iterative_depth_jsonl(row, options.position_set, options.repetitions);
                    }
                }
                write_position_jsonl(result, options.position_set, options.repetitions,
                                     options.emit_iterative_depth_rows);
            } else {
                print_position_result(result);
            }
            results.push_back(std::move(result));
        }
    };

    if (options.mode == SearchBenchmarkMode::Fixed || options.mode == SearchBenchmarkMode::Both) {
        run_mode(SearchRunMode::Fixed);
    }

    if (options.mode == SearchBenchmarkMode::Iterative ||
        options.mode == SearchBenchmarkMode::Both) {
        run_mode(SearchRunMode::Iterative);
    }
}

void print_requested_position_summaries(const std::vector<PositionBenchmarkResult>& results,
                                        const BenchmarkOptions& options) {
    std::cout << "\nPer-position summary\n";
    std::cout << "elapsed and node percentiles use per-position totals across all repetitions\n";
    print_position_summary_header();

    for (const auto& profile : options.exact_root_profiles) {
        for (const int depth : options.depths) {
            const auto print_mode = [&](SearchRunMode mode) {
                std::vector<PositionBenchmarkResult> group;
                for (const auto& result : results) {
                    if (result.exact_root_profile == profile.label && result.depth == depth &&
                        result.mode == mode) {
                        group.push_back(result);
                    }
                }
                if (!group.empty()) {
                    print_position_summary(group, mode, depth, profile.label);
                }
            };

            if (options.mode == SearchBenchmarkMode::Fixed ||
                options.mode == SearchBenchmarkMode::Both) {
                print_mode(SearchRunMode::Fixed);
            }

            if (options.mode == SearchBenchmarkMode::Iterative ||
                options.mode == SearchBenchmarkMode::Both) {
                print_mode(SearchRunMode::Iterative);
            }
        }
    }
}

int run_benchmark(std::span<char* const> args) {
    const auto parsed_options = parse_options(args);
    if (!parsed_options.has_value()) {
        for (std::size_t index = 1; index < args.size(); ++index) {
            if (std::string_view{args[index]} == "--help") {
                print_usage(args.front());
                return 0;
            }
        }
        return 2;
    }
    const auto& options = *parsed_options;

    const auto positions = make_positions(options.position_set);
    if (!positions.has_value()) {
        return 1;
    }

    if (options.describe_positions) {
        return describe_positions(*positions) ? 0 : 1;
    }

    if (options.output_format == OutputFormat::Text) {
        std::cout << "Othello search benchmark\n";
        std::cout << "position set: " << position_set_name(options.position_set) << '\n';
        std::cout << "positions: " << positions->size() << '\n';
        std::cout << "repetitions: " << options.repetitions << '\n';
        std::cout << "mode: ";
        switch (options.mode) {
        case SearchBenchmarkMode::Fixed:
            std::cout << "fixed";
            break;
        case SearchBenchmarkMode::Iterative:
            std::cout << "iterative";
            break;
        case SearchBenchmarkMode::Both:
            std::cout << "both";
            break;
        }
        std::cout << '\n';
        const auto default_search_options = othello::SearchOptions{};
        std::cout << "tt: "
                  << (options.search_cli.use_transposition_table.value_or(
                          default_search_options.use_transposition_table)
                          ? "on"
                          : "off")
                  << '\n';
        std::cout << "tt store leaf: "
                  << (options.search_cli.store_leaf_tt_entries.value_or(
                          default_search_options.store_leaf_tt_entries)
                          ? "on"
                          : "off")
                  << '\n';
        std::cout << "pvs: "
                  << (options.search_cli.use_pvs.value_or(default_search_options.use_pvs) ? "on"
                                                                                          : "off")
                  << '\n';
        std::cout << "aspiration: "
                  << (options.search_cli.use_aspiration_window.value_or(
                          default_search_options.use_aspiration_window)
                          ? "on"
                          : "off")
                  << '\n';
        std::cout << "aspiration window: " << options.search_cli.aspiration_window << '\n';
        std::cout << "aspiration max researches: " << options.search_cli.aspiration_max_researches
                  << '\n';
        std::cout << "aspiration profile: "
                  << othello::tools::aspiration_profile_name(options.search_cli.aspiration_profile)
                  << '\n';
        std::cout << "tt entries: " << options.search_cli.transposition_table_entries << '\n';
        std::cout << "exact endgame profiles: "
                  << exact_root_profile_list_text(options.exact_root_profiles) << '\n';
        std::cout << "eval config: "
                  << (options.evaluator.config_path.has_value() ? *options.evaluator.config_path
                                                                : "built-in default")
                  << '\n';
        if (options.by_position) {
            std::cout << "best_move/score/pv: first sampled result per position\n";
        } else {
            std::cout << "best_move/score/pv: first sampled result\n";
        }
        std::cout << "depths:";
        for (const int depth : options.depths) {
            std::cout << ' ' << depth;
        }
        std::cout << "\n\n";
    }

    if (options.by_position) {
        if (options.output_format == OutputFormat::Text) {
            std::cout << "by-position: on\n";
            std::cout << "per-position elapsed_ms and nodes are totals across repetitions\n\n";
        }

        std::vector<PositionBenchmarkResult> position_results;
        position_results.reserve(positions->size() * options.depths.size() *
                                 options.exact_root_profiles.size() * 2);
        if (options.output_format == OutputFormat::Text) {
            print_position_result_header();
        }
        for (const auto& profile : options.exact_root_profiles) {
            for (const int depth : options.depths) {
                run_requested_position_benchmarks(*positions, options, depth, profile,
                                                  position_results);
            }
        }
        if (options.output_format == OutputFormat::Text) {
            print_requested_position_summaries(position_results, options);
        }
        return 0;
    }

    if (options.output_format == OutputFormat::Text) {
        print_search_result_header();
    }
    for (const auto& profile : options.exact_root_profiles) {
        for (const int depth : options.depths) {
            run_requested_benchmarks(*positions, options, depth, profile);
        }
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        return run_benchmark(std::span<char* const>{argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& exception) {
        std::cerr << "benchmark failed: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "benchmark failed with an unknown exception\n";
    }

    return 1;
}
