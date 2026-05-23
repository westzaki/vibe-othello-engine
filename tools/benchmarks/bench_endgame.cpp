#include "endgame_positions.hpp"

#include <algorithm>
#include <array>
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
    bool root_breakdown = false;
    bool help = false;
};

struct EndgamePositionMetrics {
    int empties = 0;
    int score_current = 0;
    int legal_moves_current = 0;
    int legal_moves_opponent = 0;

    bool root_pass = false;
    bool game_over = false;

    int empty_corner_count = 0;
    int legal_corner_count = 0;
    int opponent_legal_corner_count = 0;

    int edge_empty_count = 0;
    int legal_edge_count = 0;

    int x_square_legal_risk_count = 0;

    int empty_region_count = 0;
    int odd_region_count = 0;
    int even_region_count = 0;
    int singleton_region_count = 0;
    int largest_region_size = 0;
};

struct PositionBenchmarkResult {
    std::string_view name;
    int empties = 0;
    std::string_view tags;
    EndgamePositionMetrics metrics;
    std::optional<othello::Square> best_move;
    int disc_margin = 0;
    std::vector<othello::Square> principal_variation;
    std::uint64_t total_nodes = 0;
    othello::ExactEndgameStats total_stats;
    std::chrono::nanoseconds elapsed{};
    std::uint64_t repetitions = 0;
};

struct RootCandidateBreakdown {
    std::string_view position_name;
    int empties = 0;
    std::optional<othello::Square> root_move;
    bool is_pass = false;
    int rank_by_margin = 0;
    int disc_margin_root = 0;
    EndgamePositionMetrics child_metrics;
    std::uint64_t nodes = 0;
    othello::ExactEndgameStats stats;
    std::chrono::nanoseconds elapsed{};
    std::vector<othello::Square> principal_variation;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " [--positions smoke|suite|endgame] [--empties 1,2,4,6,8,10,12,14,16,18]"
                 " [--repetitions N] [--describe-positions] [--root-breakdown] [--help]\n"
              << '\n'
              << "Options:\n"
              << "  --positions SET       smoke, suite, or endgame (default: smoke)\n"
              << "  --empties LIST        comma-separated empty counts to include\n"
              << "  --repetitions N       positive solve count per selected position\n"
              << "  --describe-positions  validate and print selected position metadata only\n"
              << "  --root-breakdown      solve each root candidate separately after the normal "
                 "benchmark\n"
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

        if (option == "--root-breakdown") {
            options.root_breakdown = true;
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

[[nodiscard]] constexpr bool is_corner_index(int index) noexcept {
    return index == 0 || index == 7 || index == 56 || index == 63;
}

[[nodiscard]] constexpr bool is_edge_index(int index) noexcept {
    const int file = index % 8;
    const int rank = index / 8;
    return file == 0 || file == 7 || rank == 0 || rank == 7;
}

[[nodiscard]] constexpr bool is_x_square_next_to_empty_corner(int index,
                                                              othello::Bitboard empty) noexcept {
    switch (index) {
    case 9:
        return (empty & (othello::Bitboard{1} << 0)) != 0;
    case 14:
        return (empty & (othello::Bitboard{1} << 7)) != 0;
    case 49:
        return (empty & (othello::Bitboard{1} << 56)) != 0;
    case 54:
        return (empty & (othello::Bitboard{1} << 63)) != 0;
    default:
        return false;
    }
}

[[nodiscard]] constexpr othello::Bitboard orthogonal_neighbors(int index) noexcept {
    othello::Bitboard neighbors = 0;
    const int file = index % 8;
    const int rank = index / 8;
    if (file > 0) {
        neighbors |= othello::Bitboard{1} << (index - 1);
    }
    if (file < 7) {
        neighbors |= othello::Bitboard{1} << (index + 1);
    }
    if (rank > 0) {
        neighbors |= othello::Bitboard{1} << (index - 8);
    }
    if (rank < 7) {
        neighbors |= othello::Bitboard{1} << (index + 8);
    }
    return neighbors;
}

void add_empty_region_metrics(othello::Bitboard empty, EndgamePositionMetrics& metrics) noexcept {
    othello::Bitboard remaining = empty;
    while (remaining != 0) {
        const int seed_index = std::countr_zero(remaining);
        othello::Bitboard region = othello::Bitboard{1} << seed_index;
        std::array<int, 64> stack{};
        std::size_t stack_size = 1;
        stack[0] = seed_index;

        while (stack_size > 0) {
            const int index = stack[--stack_size];
            othello::Bitboard neighbors = orthogonal_neighbors(index) & remaining & ~region;
            while (neighbors != 0) {
                const int next_index = std::countr_zero(neighbors);
                neighbors &= neighbors - 1;
                region |= othello::Bitboard{1} << next_index;
                stack[stack_size] = next_index;
                ++stack_size;
            }
        }

        remaining &= ~region;
        // NOLINTNEXTLINE(readability-redundant-casting)
        const int region_size = static_cast<int>(std::popcount(region));
        ++metrics.empty_region_count;
        if (region_size % 2 == 0) {
            ++metrics.even_region_count;
        } else {
            ++metrics.odd_region_count;
        }
        if (region_size == 1) {
            ++metrics.singleton_region_count;
        }
        metrics.largest_region_size = std::max(metrics.largest_region_size, region_size);
    }
}

[[nodiscard]] EndgamePositionMetrics compute_metrics(const othello::Board& board) noexcept {
    EndgamePositionMetrics metrics;
    const othello::Bitboard empty = board.empty();
    const othello::Bitboard current_moves = othello::legal_moves(board);
    auto opponent_board = board;
    opponent_board.side_to_move = othello::opponent(board.side_to_move);
    const othello::Bitboard opponent_moves = othello::legal_moves(opponent_board);

    metrics.empties = empty_count(board);
    metrics.score_current = othello::score(board, board.side_to_move);
    // NOLINTNEXTLINE(readability-redundant-casting)
    metrics.legal_moves_current = static_cast<int>(std::popcount(current_moves));
    // NOLINTNEXTLINE(readability-redundant-casting)
    metrics.legal_moves_opponent = static_cast<int>(std::popcount(opponent_moves));
    metrics.root_pass = othello::pass_turn(board).has_value();
    metrics.game_over = othello::is_game_over(board);

    for (int index = othello::Square::min_index; index <= othello::Square::max_index; ++index) {
        const othello::Bitboard bit = othello::Bitboard{1} << index;
        if ((empty & bit) != 0 && is_corner_index(index)) {
            ++metrics.empty_corner_count;
        }
        if ((empty & bit) != 0 && is_edge_index(index)) {
            ++metrics.edge_empty_count;
        }
        if ((current_moves & bit) != 0 && is_corner_index(index)) {
            ++metrics.legal_corner_count;
        }
        if ((opponent_moves & bit) != 0 && is_corner_index(index)) {
            ++metrics.opponent_legal_corner_count;
        }
        if ((current_moves & bit) != 0 && is_edge_index(index)) {
            ++metrics.legal_edge_count;
        }
        if ((current_moves & bit) != 0 && is_x_square_next_to_empty_corner(index, empty)) {
            ++metrics.x_square_legal_risk_count;
        }
    }

    add_empty_region_metrics(empty, metrics);
    return metrics;
}

[[nodiscard]] std::vector<othello::benchmarks::EndgamePosition>
select_positions(const std::vector<othello::benchmarks::EndgamePosition>& positions,
                 const BenchmarkOptions& options) {
    std::vector<othello::benchmarks::EndgamePosition> selected;
    selected.reserve(positions.size());

    for (const auto& position : positions) {
        const bool use_smoke_set = options.position_set == PositionSet::Smoke &&
                                   !options.empties.has_value() &&
                                   (!options.describe_positions || options.position_set_explicit);
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
        std::cout << "\nPosition metrics\n";
        std::cout << std::left << std::setw(30) << "name" << "  " << std::right << std::setw(7)
                  << "empties" << "  " << std::setw(9) << "score_cur" << "  " << std::setw(9)
                  << "legal_cur" << "  " << std::setw(9) << "legal_opp" << "  " << std::setw(4)
                  << "pass" << "  " << std::setw(13) << "corners_empty" << "  " << std::setw(12)
                  << "corner_legal" << "  " << std::setw(16) << "opp_corner_legal" << "  "
                  << std::setw(10) << "edge_empty" << "  " << std::setw(10) << "edge_legal" << "  "
                  << std::setw(6) << "x_risk" << "  " << std::setw(7) << "regions" << "  "
                  << std::setw(11) << "odd_regions" << "  " << std::setw(10) << "singletons" << "  "
                  << std::setw(14) << "largest_region" << '\n';

        for (const auto& position : positions) {
            const EndgamePositionMetrics metrics = compute_metrics(position.board);
            std::cout << std::left << std::setw(30) << position.name << "  " << std::right
                      << std::setw(7) << metrics.empties << "  " << std::setw(9)
                      << metrics.score_current << "  " << std::setw(9)
                      << metrics.legal_moves_current << "  " << std::setw(9)
                      << metrics.legal_moves_opponent << "  " << std::setw(4)
                      << (metrics.root_pass ? "yes" : "no") << "  " << std::setw(13)
                      << metrics.empty_corner_count << "  " << std::setw(12)
                      << metrics.legal_corner_count << "  " << std::setw(16)
                      << metrics.opponent_legal_corner_count << "  " << std::setw(10)
                      << metrics.edge_empty_count << "  " << std::setw(10)
                      << metrics.legal_edge_count << "  " << std::setw(6)
                      << metrics.x_square_legal_risk_count << "  " << std::setw(7)
                      << metrics.empty_region_count << "  " << std::setw(11)
                      << metrics.odd_region_count << "  " << std::setw(10)
                      << metrics.singleton_region_count << "  " << std::setw(14)
                      << metrics.largest_region_size << '\n';
        }
    }

    return error_count == 0;
}

[[nodiscard]] std::string format_square(std::optional<othello::Square> square) {
    if (!square.has_value()) {
        return "-";
    }
    return othello::to_string(*square);
}

[[nodiscard]] std::string format_root_move(const RootCandidateBreakdown& candidate) {
    if (candidate.is_pass) {
        return "pass";
    }
    return format_square(candidate.root_move);
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

void add_stats(othello::ExactEndgameStats& total, const othello::ExactEndgameStats& stats) {
    total.nodes += stats.nodes;
    total.tt_lookups += stats.tt_lookups;
    total.tt_hits += stats.tt_hits;
    total.tt_exact_hits += stats.tt_exact_hits;
    total.tt_lower_hits += stats.tt_lower_hits;
    total.tt_upper_hits += stats.tt_upper_hits;
    total.tt_stores += stats.tt_stores;
    total.tt_overwrites += stats.tt_overwrites;
    total.tt_collisions += stats.tt_collisions;
    total.tt_rejected_stores += stats.tt_rejected_stores;
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

[[nodiscard]] std::vector<othello::Square>
root_candidate_principal_variation(othello::Square root_move,
                                   const std::vector<othello::Square>& child_pv) {
    std::vector<othello::Square> pv;
    pv.reserve(child_pv.size() + 1);
    pv.push_back(root_move);
    pv.insert(pv.end(), child_pv.begin(), child_pv.end());
    return pv;
}

[[nodiscard]] std::optional<PositionBenchmarkResult>
run_benchmark(const othello::benchmarks::EndgamePosition& position, std::uint64_t repetitions) {
    std::optional<othello::ExactEndgameResult> sample;
    std::uint64_t total_nodes = 0;
    othello::ExactEndgameStats total_stats;

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
        add_stats(total_stats, result.stats);
    }
    const auto elapsed = Clock::now() - started;

    if (!sample.has_value()) {
        return std::nullopt;
    }

    return PositionBenchmarkResult{
        .name = position.name,
        .empties = position.empties,
        .tags = position.tags,
        .metrics = compute_metrics(position.board),
        .best_move = sample->best_move,
        .disc_margin = sample->disc_margin,
        .principal_variation = sample->principal_variation,
        .total_nodes = total_nodes,
        .total_stats = total_stats,
        .elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed),
        .repetitions = repetitions,
    };
}

[[nodiscard]] int candidate_tie_index(const RootCandidateBreakdown& candidate) noexcept {
    if (candidate.root_move.has_value()) {
        return candidate.root_move->index();
    }
    return othello::Square::min_index - 1;
}

void assign_margin_ranks(std::vector<RootCandidateBreakdown>& candidates) {
    std::vector<std::size_t> indices;
    indices.reserve(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        indices.push_back(index);
    }

    std::ranges::sort(indices, [&candidates](std::size_t lhs, std::size_t rhs) {
        const auto& left = candidates[lhs];
        const auto& right = candidates[rhs];
        if (left.disc_margin_root != right.disc_margin_root) {
            return left.disc_margin_root > right.disc_margin_root;
        }
        return candidate_tie_index(left) < candidate_tie_index(right);
    });

    int rank = 1;
    for (const auto index : indices) {
        candidates[index].rank_by_margin = rank;
        ++rank;
    }
}

[[nodiscard]] bool validate_root_breakdown(const othello::benchmarks::EndgamePosition& position,
                                           const PositionBenchmarkResult& benchmark_result,
                                           const std::vector<RootCandidateBreakdown>& candidates) {
    if (othello::is_game_over(position.board)) {
        if (!candidates.empty()) {
            std::cerr << "terminal root breakdown unexpectedly has candidates: " << position.name
                      << '\n';
            return false;
        }
        return true;
    }

    const auto legal_moves = othello::legal_moves(position.board);
    if (legal_moves == 0) {
        if (candidates.size() != 1 || !candidates.front().is_pass) {
            std::cerr << "root pass breakdown missing pass row: " << position.name << '\n';
            return false;
        }
        if (candidates.front().disc_margin_root != benchmark_result.disc_margin) {
            std::cerr << "root pass margin mismatch for " << position.name
                      << " breakdown=" << candidates.front().disc_margin_root
                      << " solve=" << benchmark_result.disc_margin << '\n';
            return false;
        }
        return true;
    }

    const auto best = std::ranges::max_element(
        candidates, [](const RootCandidateBreakdown& lhs, const RootCandidateBreakdown& rhs) {
            if (lhs.disc_margin_root != rhs.disc_margin_root) {
                return lhs.disc_margin_root < rhs.disc_margin_root;
            }
            return candidate_tie_index(lhs) > candidate_tie_index(rhs);
        });
    if (best == candidates.end()) {
        std::cerr << "root breakdown has no legal rows: " << position.name << '\n';
        return false;
    }

    if (best->disc_margin_root != benchmark_result.disc_margin) {
        std::cerr << "root candidate best margin mismatch for " << position.name
                  << " breakdown=" << best->disc_margin_root
                  << " solve=" << benchmark_result.disc_margin << '\n';
        return false;
    }
    if (best->root_move != benchmark_result.best_move) {
        std::cerr << "root candidate best move mismatch for " << position.name
                  << " breakdown=" << format_square(best->root_move)
                  << " solve=" << format_square(benchmark_result.best_move) << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<RootCandidateBreakdown>>
run_root_breakdown(const othello::benchmarks::EndgamePosition& position,
                   const PositionBenchmarkResult& benchmark_result) {
    std::vector<RootCandidateBreakdown> candidates;

    if (othello::is_game_over(position.board)) {
        return candidates;
    }

    othello::Bitboard legal_moves = othello::legal_moves(position.board);
    if (legal_moves == 0) {
        const auto after_pass = othello::pass_turn(position.board);
        if (!after_pass.has_value()) {
            std::cerr << "root breakdown found neither legal move nor pass: " << position.name
                      << '\n';
            return std::nullopt;
        }

        const auto started = Clock::now();
        const auto child_result = othello::solve_exact_endgame(*after_pass);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);

        candidates.push_back(RootCandidateBreakdown{
            .position_name = position.name,
            .empties = position.empties,
            .root_move = std::nullopt,
            .is_pass = true,
            .rank_by_margin = 1,
            .disc_margin_root = -child_result.disc_margin,
            .child_metrics = compute_metrics(*after_pass),
            .nodes = child_result.nodes,
            .stats = child_result.stats,
            .elapsed = elapsed,
            .principal_variation = child_result.principal_variation,
        });

        if (!validate_root_breakdown(position, benchmark_result, candidates)) {
            return std::nullopt;
        }
        return candidates;
    }

    while (legal_moves != 0) {
        const int move_index = std::countr_zero(legal_moves);
        legal_moves &= legal_moves - 1;
        const auto root_move = othello::Square::from_index(move_index);
        if (!root_move.has_value()) {
            std::cerr << "root breakdown produced invalid move index " << move_index << " for "
                      << position.name << '\n';
            return std::nullopt;
        }
        const auto child_board = othello::apply_move(position.board, *root_move);
        if (!child_board.has_value()) {
            std::cerr << "root breakdown failed to apply legal move "
                      << othello::to_string(*root_move) << " for " << position.name << '\n';
            return std::nullopt;
        }

        const auto started = Clock::now();
        const auto child_result = othello::solve_exact_endgame(*child_board);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started);

        candidates.push_back(RootCandidateBreakdown{
            .position_name = position.name,
            .empties = position.empties,
            .root_move = root_move,
            .is_pass = false,
            .rank_by_margin = 0,
            .disc_margin_root = -child_result.disc_margin,
            .child_metrics = compute_metrics(*child_board),
            .nodes = child_result.nodes,
            .stats = child_result.stats,
            .elapsed = elapsed,
            .principal_variation =
                root_candidate_principal_variation(*root_move, child_result.principal_variation),
        });
    }

    assign_margin_ranks(candidates);
    if (!validate_root_breakdown(position, benchmark_result, candidates)) {
        return std::nullopt;
    }

    std::ranges::sort(candidates,
                      [](const RootCandidateBreakdown& lhs, const RootCandidateBreakdown& rhs) {
                          if (lhs.elapsed != rhs.elapsed) {
                              return lhs.elapsed > rhs.elapsed;
                          }
                          return candidate_tie_index(lhs) < candidate_tie_index(rhs);
                      });
    return candidates;
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

[[nodiscard]] double hit_rate(std::uint64_t hits, std::uint64_t lookups) {
    if (lookups == 0) {
        return 0.0;
    }
    return (static_cast<double>(hits) * 100.0) / static_cast<double>(lookups);
}

void print_position_results(const std::vector<PositionBenchmarkResult>& results) {
    std::cout << std::left << std::setw(30) << "position" << "  " << std::right << std::setw(7)
              << "empties" << "  " << std::left << std::setw(58) << "tags" << "  " << std::setw(9)
              << "best_move" << "  " << std::right << std::setw(11) << "margin" << "  "
              << std::setw(12) << "nodes" << "  " << std::setw(12) << "elapsed_ms" << "  "
              << std::setw(12) << "nodes/s" << "  " << std::setw(12) << "tt_lookups" << "  "
              << std::setw(12) << "tt_hits" << "  " << std::setw(11) << "tt_hit_pct" << "  "
              << std::setw(12) << "tt_stores" << "  " << std::setw(13) << "tt_collisions"
              << "  " << std::setw(11) << "tt_rejects" << "  pv\n";

    for (const auto& result : results) {
        std::cout << std::left << std::setw(30) << result.name << "  " << std::right << std::setw(7)
                  << result.empties << "  " << std::left << std::setw(58)
                  << (result.tags.empty() ? "-" : result.tags) << "  " << std::setw(9)
                  << format_square(result.best_move) << "  " << std::right << std::setw(11)
                  << result.disc_margin << "  " << std::setw(12) << result.total_nodes << "  "
                  << std::setw(12) << std::fixed << std::setprecision(3)
                  << milliseconds(result.elapsed) << "  " << std::setw(12) << std::fixed
                  << std::setprecision(0) << nodes_per_second(result.total_nodes, result.elapsed)
                  << "  " << std::setw(12) << result.total_stats.tt_lookups << "  " << std::setw(12)
                  << result.total_stats.tt_hits << "  " << std::setw(11) << std::fixed
                  << std::setprecision(2)
                  << hit_rate(result.total_stats.tt_hits, result.total_stats.tt_lookups) << "  "
                  << std::setw(12) << result.total_stats.tt_stores << "  " << std::setw(13)
                  << result.total_stats.tt_collisions << "  " << std::setw(11)
                  << result.total_stats.tt_rejected_stores << "  "
                  << format_principal_variation(result.principal_variation) << '\n';
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

    std::cout << "\nTT summary by empty count\n";
    std::cout << std::right << std::setw(7) << "empties" << "  " << std::setw(5) << "count"
              << "  " << std::setw(14) << "tt_lookups" << "  " << std::setw(12) << "tt_hits"
              << "  " << std::setw(11) << "tt_hit_pct" << "  " << std::setw(12) << "exact_hits"
              << "  " << std::setw(12) << "lower_hits" << "  " << std::setw(12) << "upper_hits"
              << "  " << std::setw(12) << "tt_stores" << "  " << std::setw(13) << "tt_overwrites"
              << "  " << std::setw(13) << "tt_collisions" << "  " << std::setw(11) << "tt_rejects"
              << '\n';

    for (const auto& [empties, group] : by_empty_count) {
        othello::ExactEndgameStats total_stats;
        for (const auto& result : group) {
            add_stats(total_stats, result.total_stats);
        }

        std::cout << std::right << std::setw(7) << empties << "  " << std::setw(5) << group.size()
                  << "  " << std::setw(14) << total_stats.tt_lookups << "  " << std::setw(12)
                  << total_stats.tt_hits << "  " << std::setw(11) << std::fixed
                  << std::setprecision(2) << hit_rate(total_stats.tt_hits, total_stats.tt_lookups)
                  << "  " << std::setw(12) << total_stats.tt_exact_hits << "  " << std::setw(12)
                  << total_stats.tt_lower_hits << "  " << std::setw(12) << total_stats.tt_upper_hits
                  << "  " << std::setw(12) << total_stats.tt_stores << "  " << std::setw(13)
                  << total_stats.tt_overwrites << "  " << std::setw(13) << total_stats.tt_collisions
                  << "  " << std::setw(11) << total_stats.tt_rejected_stores << '\n';
    }
}

void print_position_metrics(const std::vector<PositionBenchmarkResult>& results) {
    std::cout << "\nPosition metrics\n";
    std::cout << std::left << std::setw(30) << "position" << "  " << std::right << std::setw(7)
              << "empties" << "  " << std::setw(9) << "legal_cur" << "  " << std::setw(9)
              << "legal_opp" << "  " << std::setw(12) << "corner_legal" << "  " << std::setw(10)
              << "edge_empty" << "  " << std::setw(7) << "regions" << "  " << std::setw(11)
              << "odd_regions" << "  " << std::setw(14) << "largest_region"
              << "  " << std::setw(12) << "elapsed_ms" << "  " << std::setw(12) << "nodes" << '\n';

    for (const auto& result : results) {
        const EndgamePositionMetrics& metrics = result.metrics;
        std::cout << std::left << std::setw(30) << result.name << "  " << std::right << std::setw(7)
                  << metrics.empties << "  " << std::setw(9) << metrics.legal_moves_current << "  "
                  << std::setw(9) << metrics.legal_moves_opponent << "  " << std::setw(12)
                  << metrics.legal_corner_count << "  " << std::setw(10) << metrics.edge_empty_count
                  << "  " << std::setw(7) << metrics.empty_region_count << "  " << std::setw(11)
                  << metrics.odd_region_count << "  " << std::setw(14)
                  << metrics.largest_region_size << "  " << std::setw(12) << std::fixed
                  << std::setprecision(3) << milliseconds(result.elapsed) << "  " << std::setw(12)
                  << result.total_nodes << '\n';
    }
}

void print_metrics_summary_by_empty_count(const std::vector<PositionBenchmarkResult>& results) {
    std::map<int, std::vector<PositionBenchmarkResult>> by_empty_count;
    for (const auto& result : results) {
        by_empty_count[result.empties].push_back(result);
    }

    std::cout << "\nPosition metrics summary by empty count\n";
    std::cout << std::right << std::setw(7) << "empties" << "  " << std::setw(5) << "count"
              << "  " << std::setw(13) << "avg_legal_cur" << "  " << std::setw(13)
              << "max_legal_cur" << "  " << std::setw(11) << "avg_regions" << "  " << std::setw(14)
              << "avg_odd_regs" << "  " << std::setw(14) << "max_largest_reg" << '\n';

    for (const auto& [empties, group] : by_empty_count) {
        double total_legal_moves = 0.0;
        double total_regions = 0.0;
        double total_odd_regions = 0.0;
        int max_legal_moves = 0;
        int max_largest_region = 0;

        for (const auto& result : group) {
            total_legal_moves += static_cast<double>(result.metrics.legal_moves_current);
            total_regions += static_cast<double>(result.metrics.empty_region_count);
            total_odd_regions += static_cast<double>(result.metrics.odd_region_count);
            max_legal_moves = std::max(max_legal_moves, result.metrics.legal_moves_current);
            max_largest_region = std::max(max_largest_region, result.metrics.largest_region_size);
        }

        const auto count = static_cast<double>(group.size());
        std::cout << std::right << std::setw(7) << empties << "  " << std::setw(5) << group.size()
                  << "  " << std::setw(13) << std::fixed << std::setprecision(2)
                  << total_legal_moves / count << "  " << std::setw(13) << max_legal_moves << "  "
                  << std::setw(11) << total_regions / count << "  " << std::setw(14)
                  << total_odd_regions / count << "  " << std::setw(14) << max_largest_region
                  << '\n';
    }
}

void print_root_breakdown_rows(const std::vector<RootCandidateBreakdown>& candidates) {
    if (candidates.empty()) {
        std::cout << "\nRoot candidate breakdown\n";
        std::cout << "No root candidates for selected terminal positions.\n";
        return;
    }

    std::cout << "\nRoot candidate breakdown (sorted by elapsed_ms descending within each "
                 "position)\n";
    std::cout << std::left << std::setw(30) << "position" << "  " << std::right << std::setw(7)
              << "empties" << "  " << std::left << std::setw(9) << "root_move" << "  " << std::right
              << std::setw(6) << "rank" << "  " << std::setw(11) << "margin"
              << "  " << std::setw(12) << "elapsed_ms" << "  " << std::setw(12) << "nodes"
              << "  " << std::setw(12) << "tt_lookups" << "  " << std::setw(12) << "tt_hits"
              << "  " << std::setw(11) << "tt_hit_pct" << "  " << std::setw(12) << "tt_stores"
              << "  " << std::setw(13) << "tt_collisions" << "  " << std::setw(11) << "tt_rejects"
              << "  " << std::setw(11) << "legal_after" << "  " << std::setw(13) << "child_regions"
              << "  " << std::setw(9) << "child_odd"
              << "  " << std::setw(13) << "child_largest" << "  pv\n";

    for (const auto& candidate : candidates) {
        std::cout << std::left << std::setw(30) << candidate.position_name << "  " << std::right
                  << std::setw(7) << candidate.empties << "  " << std::left << std::setw(9)
                  << format_root_move(candidate) << "  " << std::right << std::setw(6)
                  << candidate.rank_by_margin << "  " << std::setw(11) << candidate.disc_margin_root
                  << "  " << std::setw(12) << std::fixed << std::setprecision(3)
                  << milliseconds(candidate.elapsed) << "  " << std::setw(12) << candidate.nodes
                  << "  " << std::setw(12) << candidate.stats.tt_lookups << "  " << std::setw(12)
                  << candidate.stats.tt_hits << "  " << std::setw(11) << std::fixed
                  << std::setprecision(2)
                  << hit_rate(candidate.stats.tt_hits, candidate.stats.tt_lookups) << "  "
                  << std::setw(12) << candidate.stats.tt_stores << "  " << std::setw(13)
                  << candidate.stats.tt_collisions << "  " << std::setw(11)
                  << candidate.stats.tt_rejected_stores << "  " << std::setw(11)
                  << candidate.child_metrics.legal_moves_current << "  " << std::setw(13)
                  << candidate.child_metrics.empty_region_count << "  " << std::setw(9)
                  << candidate.child_metrics.odd_region_count << "  " << std::setw(13)
                  << candidate.child_metrics.largest_region_size << "  "
                  << format_principal_variation(candidate.principal_variation) << '\n';
    }
}

void print_root_breakdown_analysis(const std::vector<RootCandidateBreakdown>& candidates) {
    if (candidates.empty()) {
        return;
    }

    std::map<std::string_view, std::vector<RootCandidateBreakdown>> by_position;
    for (const auto& candidate : candidates) {
        by_position[candidate.position_name].push_back(candidate);
    }

    std::cout << "\nRoot breakdown analysis\n";
    std::cout << std::left << std::setw(30) << "position" << "  " << std::right << std::setw(10)
              << "candidates" << "  " << std::setw(18) << "total_candidate_ms" << "  "
              << std::setw(12) << "total_nodes" << "  " << std::left << std::setw(10)
              << "worst_move" << "  " << std::right << std::setw(12) << "worst_ms" << "  "
              << std::setw(12) << "worst_nodes" << "  " << std::setw(11) << "worst_margin"
              << "  " << std::setw(10) << "worst_rank" << "  " << std::setw(11) << "worst_best"
              << "  " << std::setw(11) << "avg_tt_pct" << '\n';

    for (const auto& [position_name, group] : by_position) {
        const auto worst = std::ranges::max_element(
            group, [](const RootCandidateBreakdown& lhs, const RootCandidateBreakdown& rhs) {
                return lhs.elapsed < rhs.elapsed;
            });
        std::chrono::nanoseconds total_elapsed{};
        std::uint64_t total_nodes = 0;
        othello::ExactEndgameStats total_stats;
        for (const auto& candidate : group) {
            total_elapsed += candidate.elapsed;
            total_nodes += candidate.nodes;
            add_stats(total_stats, candidate.stats);
        }

        std::cout << std::left << std::setw(30) << position_name << "  " << std::right
                  << std::setw(10) << group.size() << "  " << std::setw(18) << std::fixed
                  << std::setprecision(3) << milliseconds(total_elapsed) << "  " << std::setw(12)
                  << total_nodes << "  " << std::left << std::setw(10) << format_root_move(*worst)
                  << "  " << std::right << std::setw(12) << milliseconds(worst->elapsed) << "  "
                  << std::setw(12) << worst->nodes << "  " << std::setw(11)
                  << worst->disc_margin_root << "  " << std::setw(10) << worst->rank_by_margin
                  << "  " << std::setw(11) << (worst->rank_by_margin == 1 ? "yes" : "no") << "  "
                  << std::setw(11) << std::fixed << std::setprecision(2)
                  << hit_rate(total_stats.tt_hits, total_stats.tt_lookups) << '\n';
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
    print_position_metrics(results);
    print_metrics_summary_by_empty_count(results);

    if (options->root_breakdown) {
        std::vector<RootCandidateBreakdown> root_breakdowns;
        for (std::size_t index = 0; index < selected_positions.size(); ++index) {
            const auto breakdown = run_root_breakdown(selected_positions[index], results[index]);
            if (!breakdown.has_value()) {
                return 1;
            }
            root_breakdowns.insert(root_breakdowns.end(), breakdown->begin(), breakdown->end());
        }
        print_root_breakdown_rows(root_breakdowns);
        print_root_breakdown_analysis(root_breakdowns);
    }

    return 0;
}
