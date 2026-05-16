#include "bench_common.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct SearchOptions {
    std::vector<int> depths{1, 2, 3, 4, 5};
    std::uint64_t repetitions = 3;
};

struct SearchBenchmarkResult {
    std::string_view name;
    std::uint64_t position_count;
    int depth;
    std::uint64_t searches;
    std::chrono::nanoseconds elapsed;
    std::uint64_t total_nodes;
    std::uint64_t result_checksum;
    std::uint64_t work_checksum;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name << " [--depths 1,2,3,4,5] [--repetitions N]\n"
              << '\n'
              << "Options:\n"
              << "  --depths LIST       comma-separated positive search depths\n"
              << "  --repetitions N     positive repetition count per depth\n"
              << "  --help              show this help text\n";
}

[[nodiscard]] std::optional<int> parse_positive_depth(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value <= 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::vector<int>> parse_depths(std::string_view text) {
    std::vector<int> depths;

    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto token = text.substr(begin, end - begin);
        const auto depth = parse_positive_depth(token);
        if (!depth.has_value()) {
            return std::nullopt;
        }

        depths.push_back(*depth);
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }

    if (depths.empty()) {
        return std::nullopt;
    }

    return depths;
}

[[nodiscard]] std::optional<SearchOptions> parse_options(std::span<char* const> args) {
    SearchOptions options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option = args[index];

        if (option == "--help") {
            return std::nullopt;
        }

        if (option == "--depths") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--depths requires a comma-separated positive integer list\n";
                return std::nullopt;
            }

            const auto depths = parse_depths(args[index]);
            if (!depths.has_value()) {
                std::cerr << "--depths must be a comma-separated positive integer list\n";
                return std::nullopt;
            }
            options.depths = *depths;
            continue;
        }

        if (option == "--repetitions") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--repetitions requires a positive integer\n";
                return std::nullopt;
            }

            const auto repetitions = othello::benchmarks::parse_positive_count(args[index]);
            if (!repetitions.has_value()) {
                std::cerr << "--repetitions must be a positive integer\n";
                return std::nullopt;
            }
            options.repetitions = *repetitions;
            continue;
        }

        std::cerr << "unknown option: " << option << '\n';
        return std::nullopt;
    }

    return options;
}

[[nodiscard]] SearchBenchmarkResult
benchmark_search_fixed_depth(const std::vector<othello::benchmarks::Position>& positions, int depth,
                             std::uint64_t repetitions) {
    std::uint64_t result_checksum = 0;
    std::uint64_t work_checksum = 0;
    std::uint64_t searches = 0;
    std::uint64_t total_nodes = 0;

    const auto start = Clock::now();
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& position : positions) {
            const auto result = othello::search_fixed_depth(position.board, depth);
            const auto stable_result_checksum = othello::benchmarks::mix_checksum(
                othello::benchmarks::search_result_checksum(result),
                othello::benchmarks::board_checksum(position.board));

            result_checksum =
                othello::benchmarks::mix_checksum(result_checksum, stable_result_checksum);
            work_checksum =
                othello::benchmarks::mix_checksum(work_checksum, stable_result_checksum);
            work_checksum = othello::benchmarks::mix_checksum(work_checksum, result.nodes);

            total_nodes += result.nodes;
            ++searches;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return SearchBenchmarkResult{
        .name = "search_fixed_depth",
        .position_count = positions.size(),
        .depth = depth,
        .searches = searches,
        .elapsed = elapsed,
        .total_nodes = total_nodes,
        .result_checksum = result_checksum,
        .work_checksum = work_checksum,
    };
}

void print_search_result(const SearchBenchmarkResult& result) {
    const auto elapsed_count = result.elapsed.count();
    const auto elapsed_ms = static_cast<double>(elapsed_count) / 1'000'000.0;
    const auto searches_per_second =
        elapsed_count == 0 ? 0.0
                           : (static_cast<double>(result.searches) * 1'000'000'000.0) /
                                 static_cast<double>(elapsed_count);
    const auto nodes_per_second =
        elapsed_count == 0 ? 0.0
                           : (static_cast<double>(result.total_nodes) * 1'000'000'000.0) /
                                 static_cast<double>(elapsed_count);
    const auto nodes_per_search = result.searches == 0 ? 0.0
                                                       : static_cast<double>(result.total_nodes) /
                                                             static_cast<double>(result.searches);

    std::cout << std::left << std::setw(32) << result.name << "  positions=" << std::right
              << std::setw(2) << result.position_count << "  depth=" << std::setw(2) << result.depth
              << "  searches=" << std::setw(8) << result.searches << "  " << std::fixed
              << std::setprecision(3) << std::setw(10) << elapsed_ms << " ms  " << std::setw(14)
              << searches_per_second << " searches/s  nodes=" << result.total_nodes << "  "
              << std::setw(14) << nodes_per_second << " nodes/s  " << std::setw(10)
              << nodes_per_search << " nodes/search  result_checksum=" << result.result_checksum
              << "  work_checksum=" << result.work_checksum << '\n';
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

    const auto positions = othello::benchmarks::make_fixed_positions();
    if (!positions.has_value()) {
        return 1;
    }

    std::cout << "Othello search benchmark\n";
    std::cout << "fixed positions: " << positions->size() << '\n';
    std::cout << "repetitions: " << options.repetitions << '\n';
    std::cout << "depths:";
    for (const int depth : options.depths) {
        std::cout << ' ' << depth;
    }
    std::cout << "\n\n";

    for (const int depth : options.depths) {
        print_search_result(benchmark_search_fixed_depth(*positions, depth, options.repetitions));
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
