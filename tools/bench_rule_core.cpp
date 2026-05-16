#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Position {
    std::string_view name;
    othello::Board board;
};

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

struct SearchBenchmarkResult {
    std::string_view name;
    std::uint64_t position_count;
    int depth;
    std::uint64_t searches;
    std::chrono::nanoseconds elapsed;
    std::uint64_t total_nodes;
    std::uint64_t checksum;
};

[[nodiscard]] std::uint64_t mix_checksum(std::uint64_t checksum, std::uint64_t value) noexcept {
    return std::rotl(checksum ^ value, 7) + 0x9E3779B97F4A7C15ULL;
}

[[nodiscard]] std::uint64_t side_checksum(othello::Side side) noexcept {
    return side == othello::Side::Black ? 0xB1A2C3D4E5F60718ULL : 0xF1E2D3C4B5A69788ULL;
}

[[nodiscard]] std::uint64_t board_checksum(const othello::Board& board) noexcept {
    auto checksum = mix_checksum(0, board.black);
    checksum = mix_checksum(checksum, board.white);
    return mix_checksum(checksum, side_checksum(board.side_to_move));
}

[[nodiscard]] std::uint64_t search_result_checksum(const othello::SearchResult& result) noexcept {
    auto checksum = mix_checksum(0, static_cast<std::uint64_t>(result.score));
    checksum = mix_checksum(checksum, static_cast<std::uint64_t>(result.depth));
    checksum = mix_checksum(checksum, result.nodes);

    const auto move_value = result.best_move.has_value()
                                ? static_cast<std::uint64_t>(result.best_move->index() + 1)
                                : 0;
    return mix_checksum(checksum, move_value);
}

[[nodiscard]] std::optional<std::uint64_t> parse_positive_count(std::string_view text) noexcept {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value == 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::vector<othello::Square> squares_from_bitboard(othello::Bitboard bits) {
    std::vector<othello::Square> squares;
    squares.reserve(64);
    for (int index = 0; index < 64; ++index) {
        const auto mask = othello::Bitboard{1} << index;
        if ((bits & mask) != 0) {
            const auto square = othello::Square::from_index(index);
            if (square.has_value()) {
                squares.push_back(*square);
            }
        }
    }
    return squares;
}

[[nodiscard]] bool add_position(std::vector<Position>& positions, std::string_view name,
                                std::string_view board_text) {
    auto board = othello::board_from_string(board_text);
    if (!board.has_value()) {
        std::cerr << "failed to parse fixed benchmark position: " << name << '\n';
        return false;
    }

    positions.push_back(Position{.name = name, .board = *board});
    return true;
}

[[nodiscard]] std::optional<std::vector<Position>> make_fixed_positions() {
    std::vector<Position> positions;
    positions.reserve(8);

    if (!add_position(positions, "initial",
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BW...\n"
                      "...WB...\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "after d3",
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BW...\n"
                      "...BB...\n"
                      "...B....\n"
                      "........\n"
                      "........\n"
                      "side=W\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "multi-direction",
                      "........\n"
                      "........\n"
                      "...B.B..\n"
                      "...WW...\n"
                      ".BW.WB..\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "edge horizontal",
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      ".WWWWWWB\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "edge vertical",
                      "B.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "corner flip",
                      "........\n"
                      "......W.\n"
                      ".....B..\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "pass",
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BWWWW\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "dense late-game-like",
                      "BBBBBBBB\n"
                      "BWWWWWWB\n"
                      "BWBBBBWB\n"
                      "BWB..BWB\n"
                      "BWBBBBWB\n"
                      "BWWWWWWB\n"
                      "BBBBBBBB\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    return positions;
}

[[nodiscard]] std::vector<LegalMoveSet>
make_legal_move_sets(const std::vector<Position>& positions) {
    std::vector<LegalMoveSet> move_sets;
    move_sets.reserve(positions.size());

    for (const auto& position : positions) {
        move_sets.push_back(LegalMoveSet{
            .name = position.name,
            .board = position.board,
            .moves = squares_from_bitboard(othello::legal_moves(position.board)),
        });
    }

    return move_sets;
}

[[nodiscard]] BenchmarkResult benchmark_legal_moves(const std::vector<Position>& positions,
                                                    std::uint64_t iterations) {
    std::uint64_t checksum = 0;
    std::uint64_t calls = 0;

    const auto start = Clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        for (const auto& position : positions) {
            const auto moves = othello::legal_moves(position.board);
            checksum = mix_checksum(checksum, moves);
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
                checksum = mix_checksum(checksum, flips);
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

                checksum = mix_checksum(checksum, board_checksum(*next));
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
                const auto squares = squares_from_bitboard(moves);
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

            checksum = mix_checksum(checksum, board_checksum(board));
            ++step;
        }

        const auto normalized_score = othello::score(board, othello::Side::Black) + 64;
        checksum = mix_checksum(checksum, static_cast<std::uint64_t>(normalized_score));
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return BenchmarkResult{.name = "deterministic full playouts",
                           .units = games,
                           .unit_label = "games",
                           .per_unit_label = "game",
                           .elapsed = elapsed,
                           .checksum = checksum};
}

[[nodiscard]] SearchBenchmarkResult
benchmark_search_fixed_depth(const std::vector<Position>& positions, int depth,
                             std::uint64_t repetitions) {
    std::uint64_t checksum = 0;
    std::uint64_t searches = 0;
    std::uint64_t total_nodes = 0;

    const auto start = Clock::now();
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& position : positions) {
            const auto result = othello::search_fixed_depth(position.board, depth);
            checksum = mix_checksum(checksum, search_result_checksum(result));
            checksum = mix_checksum(checksum, board_checksum(position.board));
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
        .checksum = checksum,
    };
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

    std::cout << std::left << std::setw(32) << result.name << "  positions=" << std::right
              << std::setw(2) << result.position_count << "  depth=" << std::setw(2) << result.depth
              << "  " << std::setw(10) << result.searches << " searches  " << std::fixed
              << std::setprecision(3) << std::setw(10) << elapsed_ms << " ms  " << std::setw(14)
              << searches_per_second << " searches/s  nodes=" << result.total_nodes << "  "
              << std::setw(14) << nodes_per_second << " nodes/s  checksum=" << result.checksum
              << '\n';
}

int run_benchmark(std::span<char* const> args) {
    constexpr std::uint64_t default_iterations = 20'000;
    constexpr std::array search_depths{1, 2, 3};

    if (args.size() > 2) {
        std::cerr << "usage: " << args.front() << " [fixed-position-iterations]\n";
        return 2;
    }

    auto iterations = default_iterations;
    if (args.size() == 2) {
        const auto parsed_iterations = parse_positive_count(args[1]);
        if (!parsed_iterations.has_value()) {
            std::cerr << "iteration count must be a positive integer\n";
            return 2;
        }
        iterations = *parsed_iterations;
    }

    const auto positions = make_fixed_positions();
    if (!positions.has_value()) {
        return 1;
    }

    const auto move_sets = make_legal_move_sets(*positions);
    const auto playout_games = std::max<std::uint64_t>(1, iterations / 200);
    const auto search_repetitions = std::max<std::uint64_t>(1, iterations / 2'000);

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

    std::cout << '\n';
    std::cout << "search repetitions: " << search_repetitions << '\n';
    for (const int depth : search_depths) {
        print_search_result(benchmark_search_fixed_depth(*positions, depth, search_repetitions));
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
