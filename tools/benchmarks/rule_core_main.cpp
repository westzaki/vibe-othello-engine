#include "positions/fixtures.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class PositionSet {
    Smoke,
    Suite,
};

struct BenchmarkOptions {
    PositionSet position_set = PositionSet::Suite;
    std::uint64_t iterations = 100'000;
    bool show_help = false;
};

struct RulePosition {
    std::string_view name;
    std::string_view tags;
    std::string_view board_text;
    othello::Board board;
};

struct BenchmarkResult {
    std::string_view position_name;
    std::string_view operation;
    std::uint64_t calls = 0;
    std::chrono::nanoseconds elapsed{};
    std::uint64_t checksum = 0;
};

struct SummaryResult {
    std::string_view operation;
    std::uint64_t calls = 0;
    std::chrono::nanoseconds elapsed{};
    std::uint64_t checksum = 0;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " [--positions smoke|suite] [--iterations N] [--help]\n\n"
              << "Options:\n"
              << "  --positions SET   smoke or suite (default: suite)\n"
              << "  --iterations N    positive iteration count per position/operation"
                 " (default: 100000)\n"
              << "  --help            show this help text\n\n"
              << "The flips_for_move and apply_move rows call all 64 squares per iteration.\n";
}

[[nodiscard]] std::string_view position_set_name(PositionSet position_set) noexcept {
    switch (position_set) {
    case PositionSet::Smoke:
        return "smoke";
    case PositionSet::Suite:
        return "suite";
    }

    return "unknown";
}

[[nodiscard]] std::optional<PositionSet> parse_position_set(std::string_view text) noexcept {
    if (text == "smoke") {
        return PositionSet::Smoke;
    }
    if (text == "suite") {
        return PositionSet::Suite;
    }

    return std::nullopt;
}

[[nodiscard]] bool add_position(std::vector<RulePosition>& positions, std::string_view name,
                                std::string_view tags, std::string_view board_text) {
    const auto board = othello::board_from_string(board_text);
    if (!board.has_value()) {
        std::cerr << "failed to parse rule benchmark position: " << name << '\n';
        return false;
    }

    positions.push_back(RulePosition{
        .name = name,
        .tags = tags,
        .board_text = board_text,
        .board = *board,
    });
    return true;
}

[[nodiscard]] std::optional<std::vector<RulePosition>> make_smoke_positions() {
    std::vector<RulePosition> positions;
    positions.reserve(5);

    if (!add_position(positions, "initial", "initial",
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BW...\n"
                      "...WB...\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B")) {
        return std::nullopt;
    }

    if (!add_position(positions, "opening-after-d3", "opening",
                      "........\n"
                      "........\n"
                      "...B....\n"
                      "...BB...\n"
                      "...WB...\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=W")) {
        return std::nullopt;
    }

    if (!add_position(positions, "midgame-normal", "midgame",
                      "....WBBB\n"
                      "...WWWBW\n"
                      "....WBWW\n"
                      "...WWW.W\n"
                      "..WWW...\n"
                      "..B.W...\n"
                      "....W...\n"
                      "....W...\n"
                      "side=B")) {
        return std::nullopt;
    }

    if (!add_position(positions, "root-pass", "pass",
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BWWWW\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B")) {
        return std::nullopt;
    }

    if (!add_position(positions, "terminal-all-black", "terminal",
                      "BBBBBBBB\n"
                      "BBBBBBBB\n"
                      "BBBBBBBB\n"
                      "BBBBBBBB\n"
                      "BBBBBBBB\n"
                      "BBBBBBBB\n"
                      "BBBBBBBB\n"
                      "BBBBBBBB\n"
                      "side=B")) {
        return std::nullopt;
    }

    return positions;
}

[[nodiscard]] std::optional<std::vector<RulePosition>> make_suite_positions() {
    auto positions = make_smoke_positions();
    if (!positions.has_value()) {
        return std::nullopt;
    }

    positions->reserve(12);

    if (!add_position(*positions, "opening-wide-mobility",
                      "opening,high_mobility,corner_available,x_square_risk",
                      "........\n"
                      "........\n"
                      "..B..B..\n"
                      "..WWWW..\n"
                      "..WBB...\n"
                      "..B.....\n"
                      ".B......\n"
                      "........\n"
                      "side=W")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "midgame-lopsided-edge",
                      "midgame,high_mobility,edge_heavy,score_lopsided",
                      "...B.W.B\n"
                      ".B.B.WW.\n"
                      ".B.BBB.W\n"
                      "BBWBBB..\n"
                      "BBWBBB..\n"
                      "BBWBBB..\n"
                      "BWWB....\n"
                      "BBBBB...\n"
                      "side=W")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "midgame-pass-edge", "midgame,pass,edge_heavy",
                      "WWWWBBBB\n"
                      "WWWBBB..\n"
                      "WWWBBBBB\n"
                      "WW.BBB..\n"
                      "W.BBB.B.\n"
                      "..B....B\n"
                      "........\n"
                      "........\n"
                      "side=B")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "14-empty-high-mobility",
                      "endgame_14,high_mobility,corner_available,edge_heavy",
                      ".WWWW.W.\n"
                      "BWBWWWWW\n"
                      "BBWWWBW.\n"
                      "BBBBBWWW\n"
                      "BWBWBBWW\n"
                      "BWWBBWWW\n"
                      ".W..WBW.\n"
                      "...W..W.\n"
                      "side=B")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "18-empty-corner-race",
                      "endgame_18,corner_race,corner_available,edge_heavy",
                      ".B...WWW\n"
                      ".WBWWWBW\n"
                      "..BBWBWB\n"
                      ".WWWWWBB\n"
                      ".WWWWBBB\n"
                      "WWBWB..B\n"
                      "WWWBWW..\n"
                      ".W.BB...\n"
                      "side=B")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "20-empty-high-mobility-lite",
                      "endgame_20,high_mobility,corner_available,edge_heavy,x_square_risk",
                      "WWW.BBB.\n"
                      ".B.WWWWW\n"
                      "BBWBBBBW\n"
                      ".WWWWBW.\n"
                      ".BBWBWWW\n"
                      "..BB.WWW\n"
                      "..BBBW.W\n"
                      "..B.....\n"
                      "side=B")) {
        return std::nullopt;
    }

    if (!add_position(*positions, "20-empty-edge-heavy-stress-lite",
                      "endgame_20,edge_heavy,normal_mobility",
                      "..W.....\n"
                      "WBWBBB..\n"
                      ".WWWBB..\n"
                      ".WWBBB..\n"
                      ".WWBWB.B\n"
                      "WBBWBBBB\n"
                      ".BWWWBWB\n"
                      "B.BBB.BB\n"
                      "side=B")) {
        return std::nullopt;
    }

    return positions;
}

[[nodiscard]] std::optional<BenchmarkOptions> parse_options(std::span<char* const> args) {
    BenchmarkOptions options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option = args[index];

        if (option == "--help") {
            options.show_help = true;
            return options;
        }

        if (option == "--positions") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--positions requires smoke or suite\n";
                return std::nullopt;
            }

            const auto parsed = parse_position_set(args[index]);
            if (!parsed.has_value()) {
                std::cerr << "--positions requires smoke or suite\n";
                return std::nullopt;
            }
            options.position_set = *parsed;
        } else if (option == "--iterations") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--iterations requires a positive integer\n";
                return std::nullopt;
            }

            const auto parsed = othello::benchmarks::parse_positive_count(args[index]);
            if (!parsed.has_value()) {
                std::cerr << "--iterations requires a positive integer\n";
                return std::nullopt;
            }
            options.iterations = *parsed;
        } else {
            std::cerr << "unknown option: " << option << '\n';
            print_usage(args.front());
            return std::nullopt;
        }
    }

    return options;
}

[[nodiscard]] std::optional<std::vector<RulePosition>> make_positions(PositionSet position_set) {
    switch (position_set) {
    case PositionSet::Smoke:
        return make_smoke_positions();
    case PositionSet::Suite:
        return make_suite_positions();
    }

    return std::nullopt;
}

[[nodiscard]] std::uint64_t square_checksum(othello::Square square) noexcept {
    return static_cast<std::uint64_t>(square.index()) + 1;
}

[[nodiscard]] std::uint64_t optional_board_checksum(const std::optional<othello::Board>& board,
                                                    othello::Square square) noexcept {
    if (!board.has_value()) {
        return 0xA5A5A5A5A5A5A5A5ULL ^ square_checksum(square);
    }
    return othello::benchmarks::board_checksum(*board);
}

[[nodiscard]] BenchmarkResult benchmark_legal_moves(const RulePosition& position,
                                                    std::uint64_t iterations) {
    std::uint64_t checksum = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        checksum =
            othello::benchmarks::mix_checksum(checksum, othello::legal_moves(position.board));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.position_name = position.name,
                           .operation = "legal_moves",
                           .calls = iterations,
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] BenchmarkResult benchmark_flips_for_move(const RulePosition& position,
                                                       std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (int index = othello::Square::min_index; index <= othello::Square::max_index; ++index) {
            const auto square = *othello::Square::from_index(index);
            checksum = othello::benchmarks::mix_checksum(
                checksum, othello::flips_for_move(position.board, square));
            ++calls;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.position_name = position.name,
                           .operation = "flips_for_move",
                           .calls = calls,
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] BenchmarkResult benchmark_apply_move(const RulePosition& position,
                                                   std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (int index = othello::Square::min_index; index <= othello::Square::max_index; ++index) {
            const auto square = *othello::Square::from_index(index);
            checksum = othello::benchmarks::mix_checksum(
                checksum,
                optional_board_checksum(othello::apply_move(position.board, square), square));
            ++calls;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.position_name = position.name,
                           .operation = "apply_move",
                           .calls = calls,
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] BenchmarkResult benchmark_pass_turn(const RulePosition& position,
                                                  std::uint64_t iterations) {
    std::uint64_t checksum = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        const auto passed = othello::pass_turn(position.board);
        checksum = othello::benchmarks::mix_checksum(
            checksum, passed.has_value() ? othello::benchmarks::board_checksum(*passed) : 0);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.position_name = position.name,
                           .operation = "pass_turn",
                           .calls = iterations,
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] BenchmarkResult benchmark_is_game_over(const RulePosition& position,
                                                     std::uint64_t iterations) {
    std::uint64_t checksum = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        checksum = othello::benchmarks::mix_checksum(checksum,
                                                     othello::is_game_over(position.board) ? 1 : 0);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.position_name = position.name,
                           .operation = "is_game_over",
                           .calls = iterations,
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] BenchmarkResult benchmark_disc_count(const RulePosition& position,
                                                   std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (const auto side : {othello::Side::Black, othello::Side::White}) {
            checksum = othello::benchmarks::mix_checksum(
                checksum, static_cast<std::uint64_t>(othello::disc_count(position.board, side)));
            ++calls;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.position_name = position.name,
                           .operation = "disc_count",
                           .calls = calls,
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] BenchmarkResult benchmark_score(const RulePosition& position,
                                              std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (const auto side : {othello::Side::Black, othello::Side::White}) {
            const int normalized_score = othello::score(position.board, side) + 64;
            checksum = othello::benchmarks::mix_checksum(
                checksum, static_cast<std::uint64_t>(normalized_score));
            ++calls;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.position_name = position.name,
                           .operation = "score",
                           .calls = calls,
                           .elapsed = elapsed,
                           .checksum = checksum};
}

void add_to_summary(std::vector<SummaryResult>& summaries, const BenchmarkResult& result) {
    for (auto& summary : summaries) {
        if (summary.operation == result.operation) {
            summary.calls += result.calls;
            summary.elapsed += result.elapsed;
            summary.checksum = othello::benchmarks::mix_checksum(summary.checksum, result.checksum);
            return;
        }
    }

    summaries.push_back(SummaryResult{.operation = result.operation,
                                      .calls = result.calls,
                                      .elapsed = result.elapsed,
                                      .checksum = result.checksum});
}

[[nodiscard]] double elapsed_ms(std::chrono::nanoseconds elapsed) noexcept {
    return static_cast<double>(elapsed.count()) / 1'000'000.0;
}

[[nodiscard]] double ns_per_call(const BenchmarkResult& result) noexcept {
    if (result.calls == 0) {
        return 0.0;
    }
    return static_cast<double>(result.elapsed.count()) / static_cast<double>(result.calls);
}

[[nodiscard]] double ns_per_call(const SummaryResult& result) noexcept {
    if (result.calls == 0) {
        return 0.0;
    }
    return static_cast<double>(result.elapsed.count()) / static_cast<double>(result.calls);
}

[[nodiscard]] double calls_per_second(const BenchmarkResult& result) noexcept {
    if (result.elapsed.count() == 0) {
        return 0.0;
    }
    return (static_cast<double>(result.calls) * 1'000'000'000.0) /
           static_cast<double>(result.elapsed.count());
}

void print_position_result_header() {
    std::cout << std::left << std::setw(36) << "position" << std::setw(16) << "operation"
              << std::right << std::setw(14) << "calls" << std::setw(14) << "elapsed_ms"
              << std::setw(14) << "ns_per_call" << std::setw(16) << "calls/s" << std::setw(22)
              << "checksum" << '\n';
}

void print_position_result(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(36) << result.position_name << std::setw(16)
              << result.operation << std::right << std::setw(14) << result.calls << std::fixed
              << std::setprecision(3) << std::setw(14) << elapsed_ms(result.elapsed)
              << std::setw(14) << ns_per_call(result) << std::setw(16) << calls_per_second(result)
              << std::setw(22) << result.checksum << '\n';
}

void print_summary(const std::vector<SummaryResult>& summaries) {
    std::cout << "\nSummary by operation\n";
    std::cout << std::left << std::setw(16) << "operation" << std::right << std::setw(14)
              << "total_calls" << std::setw(18) << "total_elapsed_ms" << std::setw(18)
              << "avg_ns_per_call" << std::setw(22) << "checksum" << '\n';

    for (const auto& summary : summaries) {
        std::cout << std::left << std::setw(16) << summary.operation << std::right << std::setw(14)
                  << summary.calls << std::fixed << std::setprecision(3) << std::setw(18)
                  << elapsed_ms(summary.elapsed) << std::setw(18) << ns_per_call(summary)
                  << std::setw(22) << summary.checksum << '\n';
    }
}

[[nodiscard]] std::vector<BenchmarkResult>
run_position_benchmarks(const std::vector<RulePosition>& positions, std::uint64_t iterations) {
    std::vector<BenchmarkResult> results;
    results.reserve(positions.size() * 7);

    for (const auto& position : positions) {
        results.push_back(benchmark_legal_moves(position, iterations));
        results.push_back(benchmark_flips_for_move(position, iterations));
        results.push_back(benchmark_apply_move(position, iterations));
        results.push_back(benchmark_pass_turn(position, iterations));
        results.push_back(benchmark_is_game_over(position, iterations));
        results.push_back(benchmark_disc_count(position, iterations));
        results.push_back(benchmark_score(position, iterations));
    }

    return results;
}

int run_benchmark(std::span<char* const> args) {
    const auto options = parse_options(args);
    if (!options.has_value()) {
        return 2;
    }
    if (options->show_help) {
        print_usage(args.front());
        return 0;
    }

    const auto positions = make_positions(options->position_set);
    if (!positions.has_value()) {
        return 1;
    }

    std::cout << "Othello rule-core operation benchmark\n";
    std::cout << "positions: " << position_set_name(options->position_set) << '\n';
    std::cout << "position count: " << positions->size() << '\n';
    std::cout << "iterations: " << options->iterations << '\n';
    std::cout << "flips/apply move coverage: all 64 squares per position\n\n";

    const auto results = run_position_benchmarks(*positions, options->iterations);
    std::vector<SummaryResult> summaries;

    print_position_result_header();
    for (const auto& result : results) {
        print_position_result(result);
        add_to_summary(summaries, result);
    }
    print_summary(summaries);

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
