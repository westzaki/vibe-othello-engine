#include "bench_common.hpp"

#include <algorithm>
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

struct LegalMoveSet {
    std::string_view name;
    othello::Board board;
    std::vector<othello::Square> moves;
};

struct BenchmarkResult {
    std::string_view name;
    std::uint64_t units;
    std::string_view unit_label;
    std::string_view per_unit_label;
    std::chrono::nanoseconds elapsed;
    std::uint64_t checksum;
};

[[nodiscard]] std::vector<LegalMoveSet>
make_legal_move_sets(const std::vector<othello::benchmarks::Position>& positions) {
    std::vector<LegalMoveSet> move_sets;
    move_sets.reserve(positions.size());

    for (const auto& position : positions) {
        move_sets.push_back(LegalMoveSet{
            .name = position.name,
            .board = position.board,
            .moves =
                othello::benchmarks::squares_from_bitboard(othello::legal_moves(position.board)),
        });
    }

    return move_sets;
}

[[nodiscard]] BenchmarkResult
benchmark_legal_moves(const std::vector<othello::benchmarks::Position>& positions,
                      std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (const auto& position : positions) {
            const auto moves = othello::legal_moves(position.board);
            checksum = othello::benchmarks::mix_checksum(checksum, moves);
            ++calls;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.name = "legal_moves fixed positions",
                           .units = calls,
                           .unit_label = "calls",
                           .per_unit_label = "call",
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] BenchmarkResult benchmark_flips_for_move(const std::vector<LegalMoveSet>& move_sets,
                                                       std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (const auto& move_set : move_sets) {
            for (const auto square : move_set.moves) {
                const auto flips = othello::flips_for_move(move_set.board, square);
                checksum = othello::benchmarks::mix_checksum(checksum, flips);
                ++calls;
            }
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.name = "flips_for_move legal moves",
                           .units = calls,
                           .unit_label = "calls",
                           .per_unit_label = "call",
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] std::optional<BenchmarkResult>
benchmark_apply_move(const std::vector<LegalMoveSet>& move_sets, std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (const auto& move_set : move_sets) {
            for (const auto square : move_set.moves) {
                const auto next = othello::apply_move(move_set.board, square);
                if (!next.has_value()) {
                    std::cerr << "apply_move failed for benchmark position " << move_set.name
                              << " at " << othello::to_string(square) << '\n';
                    return std::nullopt;
                }

                checksum = othello::benchmarks::mix_checksum(
                    checksum, othello::benchmarks::board_checksum(*next));
                ++calls;
            }
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.name = "apply_move legal moves",
                           .units = calls,
                           .unit_label = "calls",
                           .per_unit_label = "call",
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] std::optional<BenchmarkResult> benchmark_playouts(std::uint64_t games) {
    std::uint64_t checksum = 0;

    const auto start = Clock::now();
    for (std::uint64_t game = 0; game < games; ++game) {
        auto board = othello::Board::initial();
        int step = 0;

        while (!othello::is_game_over(board)) {
            const auto moves = othello::legal_moves(board);
            if (moves != 0) {
                const auto squares = othello::benchmarks::squares_from_bitboard(moves);
                const auto selected =
                    squares[(game + static_cast<std::uint64_t>(step)) % squares.size()];
                const auto next = othello::apply_move(board, selected);
                if (!next.has_value()) {
                    std::cerr << "apply_move failed during deterministic playout at step " << step
                              << '\n';
                    return std::nullopt;
                }
                board = *next;
            } else {
                const auto next = othello::pass_turn(board);
                if (!next.has_value()) {
                    std::cerr << "pass_turn failed during deterministic playout at step " << step
                              << '\n';
                    return std::nullopt;
                }
                board = *next;
            }

            checksum = othello::benchmarks::mix_checksum(
                checksum, othello::benchmarks::board_checksum(board));
            ++step;
        }

        const auto normalized_score = othello::score(board, othello::Side::Black) + 64;
        checksum = othello::benchmarks::mix_checksum(checksum,
                                                     static_cast<std::uint64_t>(normalized_score));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.name = "deterministic full playouts",
                           .units = games,
                           .unit_label = "games",
                           .per_unit_label = "game",
                           .elapsed = elapsed,
                           .checksum = checksum};
}

void print_result(const BenchmarkResult& result) {
    const auto elapsed_count = result.elapsed.count();
    const auto elapsed_ms = static_cast<double>(elapsed_count) / 1'000'000.0;
    const auto ns_per_unit =
        result.units == 0 ? 0.0
                          : static_cast<double>(elapsed_count) / static_cast<double>(result.units);
    const auto units_per_second = elapsed_count == 0
                                      ? 0.0
                                      : (static_cast<double>(result.units) * 1'000'000'000.0) /
                                            static_cast<double>(elapsed_count);

    std::cout << std::left << std::setw(32) << result.name << "  " << std::right << std::setw(10)
              << result.units << ' ' << std::left << std::setw(5) << result.unit_label << "  "
              << std::right << std::fixed << std::setprecision(3) << std::setw(10) << elapsed_ms
              << " ms  " << std::setw(10) << ns_per_unit << " ns/" << result.per_unit_label << "  "
              << std::setw(14) << units_per_second << ' ' << result.unit_label
              << "/s  checksum=" << result.checksum << '\n';
}

int run_benchmark(std::span<char* const> args) {
    constexpr std::uint64_t default_iterations = 20'000;

    if (args.size() > 2) {
        std::cerr << "usage: " << args.front() << " [fixed-position-iterations]\n";
        return 2;
    }

    auto iterations = default_iterations;
    if (args.size() == 2) {
        const auto parsed_iterations = othello::benchmarks::parse_positive_count(args[1]);
        if (!parsed_iterations.has_value()) {
            std::cerr << "iteration count must be a positive integer\n";
            return 2;
        }
        iterations = *parsed_iterations;
    }

    const auto positions = othello::benchmarks::make_fixed_positions();
    if (!positions.has_value()) {
        return 1;
    }

    const auto move_sets = make_legal_move_sets(*positions);
    const auto playout_games = std::max<std::uint64_t>(1, iterations / 200);

    std::cout << "Othello rule-core benchmark\n";
    std::cout << "fixed positions: " << positions->size() << '\n';
    std::cout << "fixed-position iterations: " << iterations << '\n';
    std::cout << "deterministic playout games: " << playout_games << "\n\n";

    print_result(benchmark_legal_moves(*positions, iterations));
    print_result(benchmark_flips_for_move(move_sets, iterations));

    const auto apply_result = benchmark_apply_move(move_sets, iterations);
    if (!apply_result.has_value()) {
        return 1;
    }
    print_result(*apply_result);

    const auto playout_result = benchmark_playouts(playout_games);
    if (!playout_result.has_value()) {
        return 1;
    }
    print_result(*playout_result);

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
