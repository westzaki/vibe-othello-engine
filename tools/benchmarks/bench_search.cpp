#include "bench_common.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class SearchBenchmarkMode {
    Fixed,
    Iterative,
    Both,
};

enum class SearchRunMode {
    Fixed,
    Iterative,
};

struct BenchmarkOptions {
    std::vector<int> depths{1, 2, 3, 4, 5};
    std::uint64_t repetitions = 3;
    SearchBenchmarkMode mode = SearchBenchmarkMode::Fixed;
    std::optional<bool> use_transposition_table;
    std::size_t transposition_table_entries = othello::SearchOptions{}.transposition_table_entries;
};

struct SearchBenchmarkResult {
    std::string_view name;
    SearchRunMode mode;
    bool use_transposition_table;
    std::size_t transposition_table_entries;
    std::uint64_t position_count;
    int depth;
    std::optional<othello::Square> sample_best_move;
    int sample_score;
    std::vector<othello::Square> sample_principal_variation;
    std::uint64_t searches;
    std::chrono::nanoseconds elapsed;
    std::uint64_t total_nodes;
    std::uint64_t result_checksum;
    std::uint64_t work_checksum;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " [--mode fixed|iterative|both] [--depths 1,2,3,4,5]"
                 " [--repetitions N] [--tt on|off] [--tt-entries N]\n"
              << '\n'
              << "Options:\n"
              << "  --depths LIST       comma-separated positive search depths\n"
              << "  --repetitions N     positive repetition count per depth\n"
              << "  --mode MODE         fixed, iterative, or both (default: fixed)\n"
              << "  --tt on|off         override the SearchOptions TT default\n"
              << "  --tt-entries N      requested transposition table entry count\n"
              << "  --help              show this help text\n";
}

[[nodiscard]] std::string_view mode_name(SearchRunMode mode) noexcept {
    switch (mode) {
    case SearchRunMode::Fixed:
        return "fixed";
    case SearchRunMode::Iterative:
        return "iterative";
    }

    return "unknown";
}

[[nodiscard]] std::uint64_t mode_checksum(SearchRunMode mode) noexcept {
    switch (mode) {
    case SearchRunMode::Fixed:
        return 1;
    case SearchRunMode::Iterative:
        return 2;
    }

    return 0;
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

[[nodiscard]] std::optional<SearchBenchmarkMode> parse_benchmark_mode(std::string_view text) {
    if (text == "fixed") {
        return SearchBenchmarkMode::Fixed;
    }
    if (text == "iterative") {
        return SearchBenchmarkMode::Iterative;
    }
    if (text == "both") {
        return SearchBenchmarkMode::Both;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<bool> parse_tt_setting(std::string_view text) {
    if (text == "on") {
        return true;
    }
    if (text == "off") {
        return false;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> parse_entry_count(std::string_view text) noexcept {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end ||
        value > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::optional<BenchmarkOptions> parse_options(std::span<char* const> args) {
    BenchmarkOptions options;

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

        if (option == "--mode") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--mode requires fixed, iterative, or both\n";
                return std::nullopt;
            }

            const auto mode = parse_benchmark_mode(args[index]);
            if (!mode.has_value()) {
                std::cerr << "--mode must be fixed, iterative, or both\n";
                return std::nullopt;
            }
            options.mode = *mode;
            continue;
        }

        if (option == "--tt") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--tt requires on or off\n";
                return std::nullopt;
            }

            const auto tt_setting = parse_tt_setting(args[index]);
            if (!tt_setting.has_value()) {
                std::cerr << "--tt must be on or off\n";
                return std::nullopt;
            }
            options.use_transposition_table = tt_setting;
            continue;
        }

        if (option == "--tt-entries") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--tt-entries requires a non-negative integer\n";
                return std::nullopt;
            }

            const auto entry_count = parse_entry_count(args[index]);
            if (!entry_count.has_value()) {
                std::cerr << "--tt-entries must be a non-negative integer\n";
                return std::nullopt;
            }
            options.transposition_table_entries = *entry_count;
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

[[nodiscard]] othello::SearchOptions make_search_options(const BenchmarkOptions& options,
                                                         int depth) noexcept {
    auto search_options = othello::SearchOptions{.max_depth = depth};
    if (options.use_transposition_table.has_value()) {
        search_options.use_transposition_table = *options.use_transposition_table;
    }
    search_options.transposition_table_entries = options.transposition_table_entries;
    return search_options;
}

[[nodiscard]] othello::SearchResult run_search(const othello::Board& board,
                                               const othello::SearchOptions& options,
                                               SearchRunMode mode) noexcept {
    switch (mode) {
    case SearchRunMode::Fixed:
        return othello::search(board, options);
    case SearchRunMode::Iterative:
        return othello::search_iterative(board, options);
    }

    return othello::SearchResult{};
}

[[nodiscard]] std::string
format_principal_variation(const std::vector<othello::Square>& principal_variation) {
    if (principal_variation.empty()) {
        return "-";
    }

    std::string text;
    for (const othello::Square square : principal_variation) {
        if (!text.empty()) {
            text += "->";
        }
        text += othello::to_string(square);
    }
    return text;
}

[[nodiscard]] SearchBenchmarkResult
benchmark_search(const std::vector<othello::benchmarks::Position>& positions, int depth,
                 std::uint64_t repetitions, const BenchmarkOptions& benchmark_options,
                 SearchRunMode mode) {
    const auto search_options = make_search_options(benchmark_options, depth);
    std::uint64_t result_checksum = 0;
    std::uint64_t work_checksum = 0;
    std::uint64_t searches = 0;
    std::uint64_t total_nodes = 0;
    std::optional<othello::Square> sample_best_move;
    int sample_score = 0;
    std::vector<othello::Square> sample_principal_variation;

    const auto start = Clock::now();
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& position : positions) {
            const auto result = run_search(position.board, search_options, mode);
            auto stable_result_checksum = othello::benchmarks::mix_checksum(
                othello::benchmarks::search_result_checksum(result),
                othello::benchmarks::board_checksum(position.board));
            stable_result_checksum =
                othello::benchmarks::mix_checksum(stable_result_checksum, mode_checksum(mode));

            result_checksum =
                othello::benchmarks::mix_checksum(result_checksum, stable_result_checksum);
            work_checksum =
                othello::benchmarks::mix_checksum(work_checksum, stable_result_checksum);
            work_checksum = othello::benchmarks::mix_checksum(work_checksum, result.nodes);

            if (searches == 0) {
                sample_best_move = result.best_move;
                sample_score = result.score;
                sample_principal_variation = result.principal_variation;
            }
            total_nodes += result.nodes;
            ++searches;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return SearchBenchmarkResult{
        .name = "search",
        .mode = mode,
        .use_transposition_table = search_options.use_transposition_table,
        .transposition_table_entries = search_options.transposition_table_entries,
        .position_count = positions.size(),
        .depth = depth,
        .sample_best_move = sample_best_move,
        .sample_score = sample_score,
        .sample_principal_variation = sample_principal_variation,
        .searches = searches,
        .elapsed = elapsed,
        .total_nodes = total_nodes,
        .result_checksum = result_checksum,
        .work_checksum = work_checksum,
    };
}

void print_search_result_header() {
    std::cout << std::left << std::setw(12) << "benchmark" << "  " << std::setw(10) << "mode"
              << "  " << std::setw(3) << "tt" << "  " << std::setw(10) << "tt_entries"
              << "  positions  depth  best_move  score  " << std::setw(28) << "pv"
              << "  searches  elapsed_ms      searches/s  total_nodes         nodes/s"
                 "  nodes/search  result_checksum  work_checksum\n";
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
    const std::string principal_variation_text =
        format_principal_variation(result.sample_principal_variation);

    std::cout << std::left << std::setw(12) << result.name << "  " << std::setw(10)
              << mode_name(result.mode) << "  " << std::setw(3)
              << (result.use_transposition_table ? "on" : "off") << "  " << std::right
              << std::setw(10) << result.transposition_table_entries << "  " << std::setw(9)
              << result.position_count << "  " << std::setw(5) << result.depth << "  " << std::left
              << std::setw(9)
              << (result.sample_best_move.has_value() ? othello::to_string(*result.sample_best_move)
                                                      : "-")
              << "  " << std::right << std::setw(5) << result.sample_score << "  " << std::left
              << std::setw(28) << principal_variation_text << "  " << std::right << std::setw(8)
              << result.searches << "  " << std::fixed << std::setprecision(3) << std::setw(10)
              << elapsed_ms << "  " << std::setw(14) << searches_per_second << "  " << std::setw(11)
              << result.total_nodes << "  " << std::setw(14) << nodes_per_second << "  "
              << std::setw(12) << nodes_per_search << "  " << result.result_checksum << "  "
              << result.work_checksum << '\n';
}

void run_requested_benchmarks(const std::vector<othello::benchmarks::Position>& positions,
                              const BenchmarkOptions& options, int depth) {
    if (options.mode == SearchBenchmarkMode::Fixed || options.mode == SearchBenchmarkMode::Both) {
        print_search_result(
            benchmark_search(positions, depth, options.repetitions, options, SearchRunMode::Fixed));
    }

    if (options.mode == SearchBenchmarkMode::Iterative ||
        options.mode == SearchBenchmarkMode::Both) {
        print_search_result(benchmark_search(positions, depth, options.repetitions, options,
                                             SearchRunMode::Iterative));
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

    const auto positions = othello::benchmarks::make_fixed_positions();
    if (!positions.has_value()) {
        return 1;
    }

    std::cout << "Othello search benchmark\n";
    std::cout << "fixed positions: " << positions->size() << '\n';
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
              << (options.use_transposition_table.value_or(
                      default_search_options.use_transposition_table)
                      ? "on"
                      : "off")
              << '\n';
    std::cout << "tt entries: " << options.transposition_table_entries << '\n';
    std::cout << "best_move/score/pv: first sampled result\n";
    std::cout << "depths:";
    for (const int depth : options.depths) {
        std::cout << ' ' << depth;
    }
    std::cout << "\n\n";

    print_search_result_header();
    for (const int depth : options.depths) {
        run_requested_benchmarks(*positions, options, depth);
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
