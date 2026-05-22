#include "endgame_positions.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class PositionSet {
    Smoke,
    Suite,
    Endgame,
};

struct BenchmarkOptions {
    PositionSet position_set = PositionSet::Smoke;
    bool position_set_explicit = false;
    std::optional<std::set<int>> empties;
    std::uint64_t repetitions = 1;
    bool describe_positions = false;
    bool help = false;
};

struct PositionBenchmarkResult {
    std::string_view name;
    int empties = 0;
    std::string_view tags;
    std::optional<othello::Square> best_move;
    int disc_margin = 0;
    std::vector<othello::Square> principal_variation;
    std::uint64_t total_nodes = 0;
    std::chrono::nanoseconds elapsed{};
    std::uint64_t repetitions = 0;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " [--positions smoke|suite|endgame] [--empties 1,2,4,6,8,10,12]"
                 " [--repetitions N] [--describe-positions] [--help]\n"
              << '\n'
              << "Options:\n"
              << "  --positions SET       smoke, suite, or endgame (default: smoke)\n"
              << "  --empties LIST        comma-separated empty counts to include\n"
              << "  --repetitions N       positive solve count per selected position\n"
              << "  --describe-positions  validate and print selected position metadata only\n"
              << "  --help                show this help text\n";
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

[[nodiscard]] std::optional<int> parse_empty_count(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < 0 || value > 64) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<std::set<int>> parse_empty_counts(std::string_view text) {
    std::set<int> values;

    std::size_t begin = 0;
    while (begin <= text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto token = text.substr(begin, end - begin);
        const auto value = parse_empty_count(token);
        if (!value.has_value()) {
            return std::nullopt;
        }

        values.insert(*value);
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }

    if (values.empty()) {
        return std::nullopt;
    }
    return values;
}

[[nodiscard]] std::optional<PositionSet> parse_position_set(std::string_view text) noexcept {
    if (text == "smoke") {
        return PositionSet::Smoke;
    }
    if (text == "suite") {
        return PositionSet::Suite;
    }
    if (text == "endgame") {
        return PositionSet::Endgame;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<BenchmarkOptions> parse_options(std::span<char* const> args) {
    BenchmarkOptions options;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view option = args[index];

        if (option == "--help") {
            options.help = true;
            return options;
        }

        if (option == "--describe-positions") {
            options.describe_positions = true;
            continue;
        }

        if (option == "--positions") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--positions requires smoke, suite, or endgame\n";
                return std::nullopt;
            }

            const auto position_set = parse_position_set(args[index]);
            if (!position_set.has_value()) {
                std::cerr << "--positions must be smoke, suite, or endgame\n";
                return std::nullopt;
            }
            options.position_set = *position_set;
            options.position_set_explicit = true;
            continue;
        }

        if (option == "--empties") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--empties requires a comma-separated integer list\n";
                return std::nullopt;
            }

            const auto empties = parse_empty_counts(args[index]);
            if (!empties.has_value()) {
                std::cerr << "--empties must be a comma-separated integer list in [0, 64]\n";
                return std::nullopt;
            }
            options.empties = *empties;
            continue;
        }

        if (option == "--repetitions") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--repetitions requires a positive integer\n";
                return std::nullopt;
            }

            const auto repetitions = parse_positive_count(args[index]);
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

[[nodiscard]] bool same_board(const othello::Board& lhs, const othello::Board& rhs) noexcept {
    return lhs.black == rhs.black && lhs.white == rhs.white && lhs.side_to_move == rhs.side_to_move;
}

[[nodiscard]] bool has_tag(std::string_view tags, std::string_view expected_tag) {
    std::size_t begin = 0;
    while (begin <= tags.size()) {
        const auto comma = tags.find(',', begin);
        const auto end = comma == std::string_view::npos ? tags.size() : comma;
        const auto tag = tags.substr(begin, end - begin);
        if (tag == expected_tag) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    return false;
}

[[nodiscard]] int empty_count(const othello::Board& board) noexcept {
    return std::popcount(board.empty());
}

[[nodiscard]] int legal_move_count(const othello::Board& board) noexcept {
    return std::popcount(othello::legal_moves(board));
}

[[nodiscard]] std::vector<othello::benchmarks::EndgamePosition>
select_positions(const std::vector<othello::benchmarks::EndgamePosition>& positions,
                 const BenchmarkOptions& options) {
    std::vector<othello::benchmarks::EndgamePosition> selected;
    selected.reserve(positions.size());

    for (const auto& position : positions) {
        const bool use_smoke_set = options.position_set == PositionSet::Smoke &&
                                   !(options.describe_positions && !options.position_set_explicit);
        if (use_smoke_set && !position.smoke) {
            continue;
        }
        if (options.empties.has_value() && !options.empties->contains(position.empties)) {
            continue;
        }
        selected.push_back(position);
    }

    return selected;
}

[[nodiscard]] bool
validate_positions(const std::vector<othello::benchmarks::EndgamePosition>& positions,
                   bool verbose) {
    std::set<othello::ZobristHash> hashes;
    int error_count = 0;

    if (verbose) {
        std::cout << "Exact endgame benchmark positions\n\n";
        std::cout << std::left << std::setw(30) << "name" << "  " << std::setw(7) << "empties"
                  << "  " << std::setw(62) << "tags" << "  side  legal_cur  legal_opp  pass"
                  << "  game_over  zobrist_hash\n";
    }

    for (const auto& position : positions) {
        const auto reparsed = othello::board_from_string(position.board_text);
        const bool parse_ok = reparsed.has_value() && same_board(*reparsed, position.board);
        if (!parse_ok) {
            ++error_count;
            std::cerr << "position parse validation failed: " << position.name << '\n';
        }

        const auto roundtrip = othello::board_from_string(othello::to_string(position.board));
        if (!roundtrip.has_value() || !same_board(*roundtrip, position.board)) {
            ++error_count;
            std::cerr << "position round-trip validation failed: " << position.name << '\n';
        }

        const auto hash = othello::zobrist_hash(position.board);
        if (!hashes.insert(hash).second) {
            ++error_count;
            std::cerr << "duplicate position hash: " << position.name << '\n';
        }

        const int computed_empties = empty_count(position.board);
        if (computed_empties != position.empties) {
            ++error_count;
            std::cerr << "empty count metadata mismatch: " << position.name
                      << " metadata=" << position.empties << " computed=" << computed_empties
                      << '\n';
        }

        const bool root_pass = othello::pass_turn(position.board).has_value();
        if (has_tag(position.tags, "pass") && !root_pass) {
            ++error_count;
            std::cerr << "pass tag mismatch: " << position.name << '\n';
        }

        auto opponent_board = position.board;
        opponent_board.side_to_move = othello::opponent(position.board.side_to_move);

        if (verbose) {
            std::cout << std::left << std::setw(30) << position.name << "  " << std::right
                      << std::setw(7) << position.empties << "  " << std::left << std::setw(62)
                      << (position.tags.empty() ? "-" : position.tags) << "  "
                      << (position.board.side_to_move == othello::Side::Black ? "B" : "W")
                      << "     " << std::right << std::setw(9) << legal_move_count(position.board)
                      << "  " << std::setw(9) << legal_move_count(opponent_board) << "  "
                      << std::setw(4) << (root_pass ? "yes" : "no") << "  " << std::setw(9)
                      << (othello::is_game_over(position.board) ? "yes" : "no") << "  0x"
                      << std::hex << hash << std::dec << '\n';
            if (!position.notes.empty()) {
                std::cout << "  notes: " << position.notes << '\n';
            }
        }
    }

    if (verbose) {
        std::cout << "\nValidation: " << (error_count == 0 ? "ok" : "failed") << '\n';
    }

    return error_count == 0;
}

[[nodiscard]] std::string format_square(std::optional<othello::Square> square) {
    if (!square.has_value()) {
        return "-";
    }
    return othello::to_string(*square);
}

[[nodiscard]] std::string
format_principal_variation(const std::vector<othello::Square>& principal_variation) {
    if (principal_variation.empty()) {
        return "-";
    }

    std::string text;
    for (const auto square : principal_variation) {
        if (!text.empty()) {
            text += "->";
        }
        text += othello::to_string(square);
    }
    return text;
}

[[nodiscard]] bool same_result(const othello::ExactEndgameResult& lhs,
                               const othello::ExactEndgameResult& rhs) {
    return lhs.best_move == rhs.best_move && lhs.disc_margin == rhs.disc_margin &&
           lhs.principal_variation == rhs.principal_variation;
}

[[nodiscard]] bool validate_solver_result(const othello::benchmarks::EndgamePosition& position,
                                          const othello::ExactEndgameResult& result) {
    if (result.best_move.has_value()) {
        if (result.principal_variation.empty()) {
            std::cerr << "PV missing best move for " << position.name << '\n';
            return false;
        }
        if (result.principal_variation.front() != *result.best_move) {
            std::cerr << "PV does not start with best move for " << position.name << '\n';
            return false;
        }
    }

    if (has_tag(position.tags, "pass")) {
        if (result.best_move.has_value()) {
            std::cerr << "pass position unexpectedly reported a root best move: " << position.name
                      << '\n';
            return false;
        }
        const auto after_pass = othello::pass_turn(position.board);
        if (after_pass.has_value() && !result.principal_variation.empty() &&
            (othello::legal_moves(*after_pass) & result.principal_variation.front().bit()) == 0) {
            std::cerr << "pass position PV starts with an illegal after-pass move: "
                      << position.name << '\n';
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::optional<PositionBenchmarkResult>
run_benchmark(const othello::benchmarks::EndgamePosition& position, std::uint64_t repetitions) {
    std::optional<othello::ExactEndgameResult> sample;
    std::uint64_t total_nodes = 0;

    const auto started = Clock::now();
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        const auto result = othello::solve_exact_endgame(position.board);
        if (!validate_solver_result(position, result)) {
            return std::nullopt;
        }

        if (!sample.has_value()) {
            sample = result;
        } else if (!same_result(*sample, result)) {
            std::cerr << "non-deterministic exact result for " << position.name << '\n';
            return std::nullopt;
        }
        total_nodes += result.nodes;
    }
    const auto elapsed = Clock::now() - started;

    if (!sample.has_value()) {
        return std::nullopt;
    }

    return PositionBenchmarkResult{
        .name = position.name,
        .empties = position.empties,
        .tags = position.tags,
        .best_move = sample->best_move,
        .disc_margin = sample->disc_margin,
        .principal_variation = sample->principal_variation,
        .total_nodes = total_nodes,
        .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed),
        .repetitions = repetitions,
    };
}

[[nodiscard]] double milliseconds(std::chrono::nanoseconds elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

[[nodiscard]] double nodes_per_second(std::uint64_t nodes, std::chrono::nanoseconds elapsed) {
    const auto seconds = std::chrono::duration<double>(elapsed).count();
    if (seconds <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(nodes) / seconds;
}

void print_position_results(const std::vector<PositionBenchmarkResult>& results) {
    std::cout << std::left << std::setw(30) << "position" << "  " << std::right << std::setw(7)
              << "empties" << "  " << std::left << std::setw(58) << "tags" << "  " << std::setw(9)
              << "best_move" << "  " << std::right << std::setw(11) << "margin" << "  "
              << std::setw(12) << "nodes" << "  " << std::setw(12) << "elapsed_ms" << "  "
              << std::setw(12) << "nodes/s" << "  pv\n";

    for (const auto& result : results) {
        std::cout << std::left << std::setw(30) << result.name << "  " << std::right << std::setw(7)
                  << result.empties << "  " << std::left << std::setw(58)
                  << (result.tags.empty() ? "-" : result.tags) << "  " << std::setw(9)
                  << format_square(result.best_move) << "  " << std::right << std::setw(11)
                  << result.disc_margin << "  " << std::setw(12) << result.total_nodes << "  "
                  << std::setw(12) << std::fixed << std::setprecision(3)
                  << milliseconds(result.elapsed) << "  " << std::setw(12) << std::fixed
                  << std::setprecision(0) << nodes_per_second(result.total_nodes, result.elapsed)
                  << "  " << format_principal_variation(result.principal_variation) << '\n';
    }
}

[[nodiscard]] double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }

    std::ranges::sort(values);
    const auto index =
        static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

[[nodiscard]] double average(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const auto value : values) {
        total += value;
    }
    return total / static_cast<double>(values.size());
}

void print_summary_by_empty_count(const std::vector<PositionBenchmarkResult>& results) {
    std::map<int, std::vector<PositionBenchmarkResult>> by_empty_count;
    for (const auto& result : results) {
        by_empty_count[result.empties].push_back(result);
    }

    std::cout << "\nSummary by empty count\n";
    std::cout << std::right << std::setw(7) << "empties" << "  " << std::setw(5) << "count"
              << "  " << std::setw(16) << "total_elapsed_ms" << "  " << std::setw(10) << "avg_ms"
              << "  " << std::setw(10) << "p50_ms" << "  " << std::setw(10) << "p95_ms" << "  "
              << std::setw(10) << "max_ms" << "  " << std::setw(12) << "avg_nodes" << "  "
              << std::setw(12) << "p50_nodes" << "  " << std::setw(12) << "p95_nodes" << "  "
              << std::setw(12) << "max_nodes" << '\n';

    for (const auto& [empties, group] : by_empty_count) {
        std::vector<double> solve_ms;
        std::vector<double> solve_nodes;
        double total_elapsed_ms = 0.0;
        for (const auto& result : group) {
            total_elapsed_ms += milliseconds(result.elapsed);
            solve_ms.push_back(milliseconds(result.elapsed) /
                               static_cast<double>(result.repetitions));
            solve_nodes.push_back(static_cast<double>(result.total_nodes) /
                                  static_cast<double>(result.repetitions));
        }

        std::cout << std::right << std::setw(7) << empties << "  " << std::setw(5) << group.size()
                  << "  " << std::setw(16) << std::fixed << std::setprecision(3) << total_elapsed_ms
                  << "  " << std::setw(10) << average(solve_ms) << "  " << std::setw(10)
                  << percentile(solve_ms, 0.50) << "  " << std::setw(10)
                  << percentile(solve_ms, 0.95) << "  " << std::setw(10)
                  << *std::ranges::max_element(solve_ms) << "  " << std::setw(12) << std::fixed
                  << std::setprecision(0) << average(solve_nodes) << "  " << std::setw(12)
                  << percentile(solve_nodes, 0.50) << "  " << std::setw(12)
                  << percentile(solve_nodes, 0.95) << "  " << std::setw(12)
                  << *std::ranges::max_element(solve_nodes) << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto options =
        parse_options(std::span<char* const>(argv, static_cast<std::size_t>(argc)));
    if (!options.has_value()) {
        print_usage(argc > 0 ? argv[0] : "othello_endgame_bench");
        return 1;
    }
    if (options->help) {
        print_usage(argc > 0 ? argv[0] : "othello_endgame_bench");
        return 0;
    }

    const auto positions = othello::benchmarks::make_endgame_positions();
    if (!positions.has_value()) {
        return 1;
    }

    const auto selected_positions = select_positions(*positions, *options);
    if (selected_positions.empty()) {
        std::cerr << "no exact endgame benchmark positions selected\n";
        return 1;
    }

    if (!validate_positions(selected_positions, options->describe_positions)) {
        return 1;
    }

    if (options->describe_positions) {
        return 0;
    }

    std::vector<PositionBenchmarkResult> results;
    results.reserve(selected_positions.size());

    for (const auto& position : selected_positions) {
        const auto result = run_benchmark(position, options->repetitions);
        if (!result.has_value()) {
            return 1;
        }
        results.push_back(*result);
    }

    print_position_results(results);
    print_summary_by_empty_count(results);

    return 0;
}
