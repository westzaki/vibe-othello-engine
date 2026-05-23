#include "search_positions.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
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

enum class SearchBenchmarkMode {
    Fixed,
    Iterative,
    Both,
};

enum class SearchRunMode {
    Fixed,
    Iterative,
};

enum class PositionSet {
    Smoke,
    Suite,
};

struct BenchmarkOptions {
    std::vector<int> depths{1, 2, 3, 4, 5};
    std::uint64_t repetitions = 3;
    SearchBenchmarkMode mode = SearchBenchmarkMode::Fixed;
    PositionSet position_set = PositionSet::Smoke;
    bool describe_positions = false;
    bool by_position = false;
    std::optional<bool> use_transposition_table;
    std::optional<bool> use_pvs;
    std::size_t transposition_table_entries = othello::SearchOptions{}.transposition_table_entries;
    int exact_endgame_empty_threshold = othello::SearchOptions{}.exact_endgame_empty_threshold;
};

struct SearchBenchmarkResult {
    std::string_view name;
    SearchRunMode mode;
    bool use_transposition_table;
    bool use_pvs;
    std::size_t transposition_table_entries;
    std::uint64_t position_count;
    int depth;
    std::optional<othello::Square> sample_best_move;
    int sample_score;
    std::vector<othello::Square> sample_principal_variation;
    std::uint64_t searches;
    std::chrono::nanoseconds elapsed;
    std::uint64_t total_nodes;
    othello::SearchStats total_stats;
    std::uint64_t result_checksum;
    std::uint64_t work_checksum;
};

struct PositionBenchmarkResult {
    std::string_view position_name;
    std::string_view phase;
    std::string_view tags;
    SearchRunMode mode;
    bool use_transposition_table;
    bool use_pvs;
    std::size_t transposition_table_entries;
    int depth;
    std::optional<othello::Square> sample_best_move;
    int sample_score;
    std::vector<othello::Square> sample_principal_variation;
    std::uint64_t searches;
    std::chrono::nanoseconds elapsed;
    std::uint64_t total_nodes;
    othello::SearchStats total_stats;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " [--mode fixed|iterative|both] [--depths 1,2,3,4,5]"
                 " [--repetitions N] [--positions smoke|suite]"
                 " [--describe-positions] [--by-position] [--tt on|off] [--tt-entries N]"
                 " [--pvs on|off] [--exact-endgame-threshold N]\n"
              << '\n'
              << "Options:\n"
              << "  --depths LIST       comma-separated positive search depths\n"
              << "  --repetitions N     positive repetition count per depth\n"
              << "  --mode MODE         fixed, iterative, or both (default: fixed)\n"
              << "  --positions SET     smoke or suite (default: smoke)\n"
              << "  --describe-positions\n"
              << "                      print selected position metadata and metrics only\n"
              << "  --by-position       print per-position benchmark rows and summaries\n"
              << "  --tt on|off         override the SearchOptions TT default\n"
              << "  --tt-entries N      requested transposition table entry count\n"
              << "  --pvs on|off        override the SearchOptions PVS default\n"
              << "  --exact-endgame-threshold N\n"
              << "                       solve root positions with at most N empties exactly; N <= "
                 "0 disables\n"
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

[[nodiscard]] std::string_view position_set_name(PositionSet position_set) noexcept {
    switch (position_set) {
    case PositionSet::Smoke:
        return "smoke";
    case PositionSet::Suite:
        return "suite";
    }

    return "unknown";
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

[[nodiscard]] std::optional<int> parse_int(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
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

[[nodiscard]] std::optional<PositionSet> parse_position_set(std::string_view text) {
    if (text == "smoke") {
        return PositionSet::Smoke;
    }
    if (text == "suite") {
        return PositionSet::Suite;
    }

    return std::nullopt;
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

[[nodiscard]] std::optional<bool> parse_on_off(std::string_view text) {
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

        if (option == "--describe-positions") {
            options.describe_positions = true;
            continue;
        }

        if (option == "--by-position") {
            options.by_position = true;
            continue;
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

        if (option == "--positions") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--positions requires smoke or suite\n";
                return std::nullopt;
            }

            const auto position_set = parse_position_set(args[index]);
            if (!position_set.has_value()) {
                std::cerr << "--positions must be smoke or suite\n";
                return std::nullopt;
            }
            options.position_set = *position_set;
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

        if (option == "--pvs") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--pvs requires on or off\n";
                return std::nullopt;
            }

            const auto pvs_setting = parse_on_off(args[index]);
            if (!pvs_setting.has_value()) {
                std::cerr << "--pvs must be on or off\n";
                return std::nullopt;
            }
            options.use_pvs = pvs_setting;
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

        if (option == "--exact-endgame-threshold") {
            ++index;
            if (index >= args.size()) {
                std::cerr << "--exact-endgame-threshold requires an integer\n";
                return std::nullopt;
            }

            const auto threshold = parse_int(args[index]);
            if (!threshold.has_value()) {
                std::cerr << "--exact-endgame-threshold must be an integer\n";
                return std::nullopt;
            }
            options.exact_endgame_empty_threshold = *threshold;
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

[[nodiscard]] std::optional<std::vector<othello::benchmarks::Position>>
make_positions(PositionSet position_set) {
    switch (position_set) {
    case PositionSet::Smoke:
        return othello::benchmarks::make_search_smoke_positions();
    case PositionSet::Suite:
        return othello::benchmarks::make_search_suite_positions();
    }

    return std::nullopt;
}

[[nodiscard]] bool same_board(const othello::Board& lhs, const othello::Board& rhs) noexcept {
    return lhs.black == rhs.black && lhs.white == rhs.white && lhs.side_to_move == rhs.side_to_move;
}

[[nodiscard]] std::vector<std::string_view> split_tags(std::string_view tags) {
    std::vector<std::string_view> result;
    if (tags.empty()) {
        return result;
    }

    std::size_t begin = 0;
    while (begin <= tags.size()) {
        const auto comma = tags.find(',', begin);
        const auto end = comma == std::string_view::npos ? tags.size() : comma;
        const auto tag = tags.substr(begin, end - begin);
        if (!tag.empty()) {
            result.push_back(tag);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }

    return result;
}

[[nodiscard]] bool has_tag(std::string_view tags, std::string_view expected_tag) {
    const auto split = split_tags(tags);
    return std::ranges::any_of(
        split, [expected_tag](std::string_view tag) { return tag == expected_tag; });
}

[[nodiscard]] int count_bits(othello::Bitboard bits) noexcept {
    return std::popcount(bits);
}

[[nodiscard]] constexpr othello::Bitboard corner_bits() noexcept {
    return (othello::Bitboard{1} << 0) | (othello::Bitboard{1} << 7) |
           (othello::Bitboard{1} << 56) | (othello::Bitboard{1} << 63);
}

[[nodiscard]] constexpr othello::Bitboard edge_bits() noexcept {
    return 0xFF818181818181FFULL;
}

[[nodiscard]] constexpr othello::Bitboard x_square_bits() noexcept {
    return (othello::Bitboard{1} << 9) | (othello::Bitboard{1} << 14) |
           (othello::Bitboard{1} << 49) | (othello::Bitboard{1} << 54);
}

[[nodiscard]] constexpr othello::Bitboard corner_for_x_square(othello::Bitboard x_square) noexcept {
    switch (x_square) {
    case othello::Bitboard{1} << 9:
        return othello::Bitboard{1} << 0;
    case othello::Bitboard{1} << 14:
        return othello::Bitboard{1} << 7;
    case othello::Bitboard{1} << 49:
        return othello::Bitboard{1} << 56;
    case othello::Bitboard{1} << 54:
        return othello::Bitboard{1} << 63;
    default:
        return 0;
    }
}

[[nodiscard]] bool has_x_square_risk(const othello::Board& board,
                                     othello::Bitboard legal_moves) noexcept {
    auto risky_x_squares = legal_moves & x_square_bits();
    while (risky_x_squares != 0) {
        const auto x_square = risky_x_squares & (~risky_x_squares + 1);
        const auto adjacent_corner = corner_for_x_square(x_square);
        if (adjacent_corner != 0 && (board.occupied() & adjacent_corner) == 0) {
            return true;
        }
        risky_x_squares &= ~x_square;
    }

    return false;
}

[[nodiscard]] std::string_view mobility_bucket(int legal_move_count) noexcept {
    if (legal_move_count <= 3) {
        return "low";
    }
    if (legal_move_count >= 9) {
        return "high";
    }
    return "normal";
}

void check_tag_consistency(std::string_view position_name, std::string_view tags,
                           std::string_view tag, bool expected, int& warning_count) {
    const auto actual = has_tag(tags, tag);
    if (actual == expected) {
        return;
    }

    ++warning_count;
    std::cout << "  warning: tag '" << tag << "' is " << (actual ? "present" : "missing")
              << " but computed value is " << (expected ? "true" : "false") << " for "
              << position_name << '\n';
}

[[nodiscard]] bool describe_positions(const std::vector<othello::benchmarks::Position>& positions) {
    std::map<std::string_view, int> phase_counts;
    std::map<std::string_view, int> mobility_counts;
    std::map<std::string_view, int> tag_counts;
    std::set<othello::ZobristHash> hashes;
    int duplicate_hash_count = 0;
    int parse_failure_count = 0;
    int roundtrip_failure_count = 0;
    int tag_warning_count = 0;

    std::cout << "Search benchmark positions\n\n";
    std::cout << std::left << std::setw(28) << "name" << "  " << std::setw(14) << "phase"
              << "  " << std::setw(46) << "tags" << "  side  B   W   empty  score_black"
              << "  legal_cur  legal_opp  pass  game_over  corners_B  corners_W"
              << "  legal_corner  zobrist_hash\n";

    for (const auto& position : positions) {
        const auto reparsed = othello::board_from_string(position.board_text);
        if (!reparsed.has_value() || !same_board(*reparsed, position.board)) {
            ++parse_failure_count;
        }

        const auto roundtrip = othello::board_from_string(othello::to_string(position.board));
        if (!roundtrip.has_value() || !same_board(*roundtrip, position.board)) {
            ++roundtrip_failure_count;
        }

        const auto hash = othello::zobrist_hash(position.board);
        if (!hashes.insert(hash).second) {
            ++duplicate_hash_count;
        }

        const auto black_count = othello::disc_count(position.board, othello::Side::Black);
        const auto white_count = othello::disc_count(position.board, othello::Side::White);
        const auto empty_count = 64 - black_count - white_count;
        const auto legal_moves = othello::legal_moves(position.board);
        const auto legal_current = count_bits(legal_moves);

        auto opponent_board = position.board;
        opponent_board.side_to_move = othello::opponent(position.board.side_to_move);
        const auto legal_opponent = count_bits(othello::legal_moves(opponent_board));

        const auto is_pass = othello::pass_turn(position.board).has_value();
        const auto is_game_over = othello::is_game_over(position.board);
        const auto legal_corner = (legal_moves & corner_bits()) != othello::Bitboard{0};
        const auto edge_count = count_bits(position.board.occupied() & edge_bits());
        const auto score_black = othello::score(position.board, othello::Side::Black);

        ++phase_counts[position.phase];
        ++mobility_counts[mobility_bucket(legal_current)];
        for (const auto tag : split_tags(position.tags)) {
            ++tag_counts[tag];
        }

        std::cout << std::left << std::setw(28) << position.name << "  " << std::setw(14)
                  << position.phase << "  " << std::setw(46)
                  << (position.tags.empty() ? "-" : position.tags) << "  "
                  << (position.board.side_to_move == othello::Side::Black ? "B" : "W") << "     "
                  << std::right << std::setw(2) << black_count << "  " << std::setw(2)
                  << white_count << "  " << std::setw(5) << empty_count << "  " << std::setw(11)
                  << othello::score(position.board, othello::Side::Black) << "  " << std::setw(9)
                  << legal_current << "  " << std::setw(9) << legal_opponent << "  " << std::setw(4)
                  << (is_pass ? "yes" : "no") << "  " << std::setw(9)
                  << (is_game_over ? "yes" : "no") << "  " << std::setw(9)
                  << count_bits(position.board.black & corner_bits()) << "  " << std::setw(9)
                  << count_bits(position.board.white & corner_bits()) << "  " << std::setw(12)
                  << (legal_corner ? "yes" : "no") << "  0x" << std::hex << hash << std::dec
                  << '\n';
        if (!position.notes.empty()) {
            std::cout << "  notes: " << position.notes << '\n';
        }
        if (position.phase != "smoke") {
            check_tag_consistency(position.name, position.tags, "high_mobility", legal_current >= 9,
                                  tag_warning_count);
            check_tag_consistency(position.name, position.tags, "low_mobility", legal_current <= 3,
                                  tag_warning_count);
            check_tag_consistency(position.name, position.tags, "pass", is_pass, tag_warning_count);
            check_tag_consistency(position.name, position.tags, "corner_available", legal_corner,
                                  tag_warning_count);
            check_tag_consistency(position.name, position.tags, "edge_heavy", edge_count >= 13,
                                  tag_warning_count);
            check_tag_consistency(position.name, position.tags, "x_square_risk",
                                  has_x_square_risk(position.board, legal_moves),
                                  tag_warning_count);
            check_tag_consistency(position.name, position.tags, "score_lopsided",
                                  score_black <= -18 || score_black >= 18, tag_warning_count);
            check_tag_consistency(position.name, position.tags, "dense_late_game",
                                  empty_count <= 10, tag_warning_count);
        }
    }

    std::cout << "\nSummary\n";
    std::cout << "total position count: " << positions.size() << '\n';
    std::cout << "phase counts:\n";
    for (const auto& [phase, count] : phase_counts) {
        std::cout << "  " << phase << ": " << count << '\n';
    }
    std::cout << "mobility bucket counts:\n";
    std::cout << "  low: " << mobility_counts["low"] << '\n';
    std::cout << "  normal: " << mobility_counts["normal"] << '\n';
    std::cout << "  high: " << mobility_counts["high"] << '\n';
    std::cout << "special tag counts:\n";
    for (const auto& [tag, count] : tag_counts) {
        std::cout << "  " << tag << ": " << count << '\n';
    }
    std::cout << "duplicate hash count: " << duplicate_hash_count << '\n';
    std::cout << "parse validation: " << (parse_failure_count == 0 ? "ok" : "failed") << '\n';
    std::cout << "round-trip validation: " << (roundtrip_failure_count == 0 ? "ok" : "failed")
              << '\n';
    std::cout << "tag consistency warnings: " << tag_warning_count << '\n';

    return parse_failure_count == 0 && roundtrip_failure_count == 0 && duplicate_hash_count == 0 &&
           tag_warning_count == 0;
}

[[nodiscard]] othello::SearchOptions make_search_options(const BenchmarkOptions& options,
                                                         int depth) noexcept {
    auto search_options = othello::SearchOptions{.max_depth = depth};
    if (options.use_transposition_table.has_value()) {
        search_options.use_transposition_table = *options.use_transposition_table;
    }
    if (options.use_pvs.has_value()) {
        search_options.use_pvs = *options.use_pvs;
    }
    search_options.transposition_table_entries = options.transposition_table_entries;
    search_options.exact_endgame_empty_threshold = options.exact_endgame_empty_threshold;
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

void add_search_stats(othello::SearchStats& total, const othello::SearchStats& stats) noexcept {
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
    total.tt_move_ordering_probes += stats.tt_move_ordering_probes;
    total.tt_move_ordering_hits += stats.tt_move_ordering_hits;
    total.tt_move_ordering_used += stats.tt_move_ordering_used;
    total.pvs_scouts += stats.pvs_scouts;
    total.pvs_researches += stats.pvs_researches;
    total.pvs_scout_cutoffs += stats.pvs_scout_cutoffs;
    total.dynamic_ordering_nodes += stats.dynamic_ordering_nodes;
    total.dynamic_ordering_moves += stats.dynamic_ordering_moves;
}

[[nodiscard]] double tt_hit_rate(const othello::SearchStats& stats) noexcept {
    if (stats.tt_lookups == 0) {
        return 0.0;
    }
    return (static_cast<double>(stats.tt_hits) * 100.0) / static_cast<double>(stats.tt_lookups);
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
    othello::SearchStats total_stats;
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
            add_search_stats(total_stats, result.stats);
            ++searches;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return SearchBenchmarkResult{
        .name = "search",
        .mode = mode,
        .use_transposition_table = search_options.use_transposition_table,
        .use_pvs = search_options.use_pvs,
        .transposition_table_entries = search_options.transposition_table_entries,
        .position_count = positions.size(),
        .depth = depth,
        .sample_best_move = sample_best_move,
        .sample_score = sample_score,
        .sample_principal_variation = sample_principal_variation,
        .searches = searches,
        .elapsed = elapsed,
        .total_nodes = total_nodes,
        .total_stats = total_stats,
        .result_checksum = result_checksum,
        .work_checksum = work_checksum,
    };
}

[[nodiscard]] PositionBenchmarkResult
benchmark_position(const othello::benchmarks::Position& position, int depth,
                   std::uint64_t repetitions, const BenchmarkOptions& benchmark_options,
                   SearchRunMode mode) {
    const auto search_options = make_search_options(benchmark_options, depth);
    std::uint64_t searches = 0;
    std::uint64_t total_nodes = 0;
    othello::SearchStats total_stats;
    std::optional<othello::Square> sample_best_move;
    int sample_score = 0;
    std::vector<othello::Square> sample_principal_variation;

    const auto start = Clock::now();
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        const auto result = run_search(position.board, search_options, mode);
        if (searches == 0) {
            sample_best_move = result.best_move;
            sample_score = result.score;
            sample_principal_variation = result.principal_variation;
        }
        total_nodes += result.nodes;
        add_search_stats(total_stats, result.stats);
        ++searches;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return PositionBenchmarkResult{
        .position_name = position.name,
        .phase = position.phase,
        .tags = position.tags,
        .mode = mode,
        .use_transposition_table = search_options.use_transposition_table,
        .use_pvs = search_options.use_pvs,
        .transposition_table_entries = search_options.transposition_table_entries,
        .depth = depth,
        .sample_best_move = sample_best_move,
        .sample_score = sample_score,
        .sample_principal_variation = sample_principal_variation,
        .searches = searches,
        .elapsed = elapsed,
        .total_nodes = total_nodes,
        .total_stats = total_stats,
    };
}

[[nodiscard]] double elapsed_ms(std::chrono::nanoseconds elapsed) noexcept {
    return static_cast<double>(elapsed.count()) / 1'000'000.0;
}

[[nodiscard]] double nodes_per_search(const PositionBenchmarkResult& result) noexcept {
    if (result.searches == 0) {
        return 0.0;
    }
    return static_cast<double>(result.total_nodes) / static_cast<double>(result.searches);
}

[[nodiscard]] double nodes_per_second(const PositionBenchmarkResult& result) noexcept {
    if (result.elapsed.count() == 0) {
        return 0.0;
    }
    return (static_cast<double>(result.total_nodes) * 1'000'000'000.0) /
           static_cast<double>(result.elapsed.count());
}

void print_search_result_header() {
    std::cout << std::left << std::setw(12) << "benchmark" << "  " << std::setw(10) << "mode"
              << "  " << std::setw(3) << "tt" << "  " << std::setw(3) << "pvs"
              << "  " << std::setw(10) << "tt_entries"
              << "  positions  depth  best_move  score  " << std::setw(28) << "pv"
              << "  searches  elapsed_ms      searches/s  total_nodes         nodes/s"
                 "  nodes/search  tt_lookups  tt_hits  tt_hit_rate  tt_stores"
                 "  tt_collisions  tt_rejected_stores  tt_order_probes  tt_order_hits"
                 "  tt_order_used  pvs_scouts  pvs_researches  pvs_scout_cutoffs"
                 "  dyn_nodes  dyn_moves"
                 "  result_checksum  work_checksum\n";
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
              << (result.use_transposition_table ? "on" : "off") << "  " << std::setw(3)
              << (result.use_pvs ? "on" : "off") << "  " << std::right << std::setw(10)
              << result.transposition_table_entries << "  " << std::setw(9) << result.position_count
              << "  " << std::setw(5) << result.depth << "  " << std::left << std::setw(9)
              << (result.sample_best_move.has_value() ? othello::to_string(*result.sample_best_move)
                                                      : "-")
              << "  " << std::right << std::setw(5) << result.sample_score << "  " << std::left
              << std::setw(28) << principal_variation_text << "  " << std::right << std::setw(8)
              << result.searches << "  " << std::fixed << std::setprecision(3) << std::setw(10)
              << elapsed_ms << "  " << std::setw(14) << searches_per_second << "  " << std::setw(11)
              << result.total_nodes << "  " << std::setw(14) << nodes_per_second << "  "
              << std::setw(12) << nodes_per_search << "  " << std::setw(10)
              << result.total_stats.tt_lookups << "  " << std::setw(7) << result.total_stats.tt_hits
              << "  " << std::setw(11) << tt_hit_rate(result.total_stats) << "  " << std::setw(9)
              << result.total_stats.tt_stores << "  " << std::setw(13)
              << result.total_stats.tt_collisions << "  " << std::setw(18)
              << result.total_stats.tt_rejected_stores << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_probes << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_hits << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_used << "  " << std::setw(9)
              << result.total_stats.pvs_scouts << "  " << std::setw(14)
              << result.total_stats.pvs_researches << "  " << std::setw(17)
              << result.total_stats.pvs_scout_cutoffs << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_nodes << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_moves << "  " << result.result_checksum << "  "
              << result.work_checksum << '\n';
}

void print_position_result_header() {
    std::cout << std::left << std::setw(28) << "position" << "  " << std::setw(14) << "phase"
              << "  " << std::setw(44) << "tags" << "  " << std::setw(10) << "mode"
              << "  " << std::setw(3) << "tt" << "  " << std::setw(3) << "pvs"
              << "  " << std::setw(10) << "tt_entries"
              << "  depth  best_move  score  " << std::setw(28) << "pv"
              << "  searches  elapsed_ms       nodes  nodes/search         nodes/s"
                 "  tt_lookups  tt_hits  tt_hit_rate  tt_stores  tt_collisions"
                 "  tt_rejected_stores  tt_order_probes  tt_order_hits  tt_order_used"
                 "  pvs_scouts  pvs_researches  pvs_scout_cutoffs  dyn_nodes  dyn_moves\n";
}

void print_position_result(const PositionBenchmarkResult& result) {
    const std::string principal_variation_text =
        format_principal_variation(result.sample_principal_variation);

    std::cout << std::left << std::setw(28) << result.position_name << "  " << std::setw(14)
              << result.phase << "  " << std::setw(44) << (result.tags.empty() ? "-" : result.tags)
              << "  " << std::setw(10) << mode_name(result.mode) << "  " << std::setw(3)
              << (result.use_transposition_table ? "on" : "off") << "  " << std::setw(3)
              << (result.use_pvs ? "on" : "off") << "  " << std::right << std::setw(10)
              << result.transposition_table_entries << "  " << std::setw(5) << result.depth << "  "
              << std::left << std::setw(9)
              << (result.sample_best_move.has_value() ? othello::to_string(*result.sample_best_move)
                                                      : "-")
              << "  " << std::right << std::setw(5) << result.sample_score << "  " << std::left
              << std::setw(28) << principal_variation_text << "  " << std::right << std::setw(8)
              << result.searches << "  " << std::fixed << std::setprecision(3) << std::setw(10)
              << elapsed_ms(result.elapsed) << "  " << std::setw(10) << result.total_nodes << "  "
              << std::setw(12) << nodes_per_search(result) << "  " << std::setw(14)
              << nodes_per_second(result) << "  " << std::setw(10) << result.total_stats.tt_lookups
              << "  " << std::setw(7) << result.total_stats.tt_hits << "  " << std::setw(11)
              << tt_hit_rate(result.total_stats) << "  " << std::setw(9)
              << result.total_stats.tt_stores << "  " << std::setw(13)
              << result.total_stats.tt_collisions << "  " << std::setw(18)
              << result.total_stats.tt_rejected_stores << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_probes << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_hits << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_used << "  " << std::setw(9)
              << result.total_stats.pvs_scouts << "  " << std::setw(14)
              << result.total_stats.pvs_researches << "  " << std::setw(17)
              << result.total_stats.pvs_scout_cutoffs << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_nodes << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_moves << '\n';
}

[[nodiscard]] std::size_t nearest_rank_index(std::size_t count, int percentile) noexcept {
    if (count == 0) {
        return 0;
    }
    return (((static_cast<std::size_t>(percentile) * count) + 99) / 100) - 1;
}

[[nodiscard]] double percentile_value(std::vector<double> values, int percentile) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    return values[nearest_rank_index(values.size(), percentile)];
}

[[nodiscard]] std::uint64_t percentile_value(std::vector<std::uint64_t> values, int percentile) {
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    return values[nearest_rank_index(values.size(), percentile)];
}

void print_position_summary_header() {
    std::cout << std::left << std::setw(10) << "mode" << "  " << std::setw(3) << "tt"
              << "  " << std::setw(3) << "pvs"
              << "  depth  positions  total_elapsed_ms  avg_ms_per_position"
                 "  p50_ms_per_position  p95_ms_per_position  max_ms_per_position"
                 "  avg_nodes_per_position  p50_nodes_per_position  p95_nodes_per_position"
                 "  max_nodes_per_position\n";
}

void print_position_summary(std::span<const PositionBenchmarkResult> results, SearchRunMode mode,
                            int depth) {
    std::vector<double> per_position_ms;
    std::vector<std::uint64_t> per_position_nodes;
    per_position_ms.reserve(results.size());
    per_position_nodes.reserve(results.size());

    std::chrono::nanoseconds total_elapsed{0};
    std::uint64_t total_nodes = 0;
    bool use_transposition_table = false;
    bool use_pvs = false;

    for (const auto& result : results) {
        per_position_ms.push_back(elapsed_ms(result.elapsed));
        per_position_nodes.push_back(result.total_nodes);
        total_elapsed += result.elapsed;
        total_nodes += result.total_nodes;
        use_transposition_table = result.use_transposition_table;
        use_pvs = result.use_pvs;
    }

    const auto position_count = results.size();
    const auto avg_ms =
        position_count == 0 ? 0.0 : elapsed_ms(total_elapsed) / static_cast<double>(position_count);
    const auto avg_nodes = position_count == 0 ? 0.0
                                               : static_cast<double>(total_nodes) /
                                                     static_cast<double>(position_count);
    const auto max_ms = per_position_ms.empty() ? 0.0 : *std::ranges::max_element(per_position_ms);
    const auto max_nodes = per_position_nodes.empty()
                               ? std::uint64_t{0}
                               : *std::ranges::max_element(per_position_nodes);

    std::cout << std::left << std::setw(10) << mode_name(mode) << "  " << std::setw(3)
              << (use_transposition_table ? "on" : "off") << "  " << std::setw(3)
              << (use_pvs ? "on" : "off") << "  " << std::right << std::setw(5) << depth << "  "
              << std::setw(9) << position_count << "  " << std::fixed << std::setprecision(3)
              << std::setw(16) << elapsed_ms(total_elapsed) << "  " << std::setw(19) << avg_ms
              << "  " << std::setw(20) << percentile_value(per_position_ms, 50) << "  "
              << std::setw(20) << percentile_value(per_position_ms, 95) << "  " << std::setw(19)
              << max_ms << "  " << std::setw(22) << avg_nodes << "  " << std::setw(22)
              << percentile_value(per_position_nodes, 50) << "  " << std::setw(22)
              << percentile_value(per_position_nodes, 95) << "  " << std::setw(22) << max_nodes
              << '\n';
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

void run_requested_position_benchmarks(const std::vector<othello::benchmarks::Position>& positions,
                                       const BenchmarkOptions& options, int depth,
                                       std::vector<PositionBenchmarkResult>& results) {
    const auto run_mode = [&](SearchRunMode mode) {
        for (const auto& position : positions) {
            auto result = benchmark_position(position, depth, options.repetitions, options, mode);
            print_position_result(result);
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

    for (const int depth : options.depths) {
        const auto print_mode = [&](SearchRunMode mode) {
            std::vector<PositionBenchmarkResult> group;
            for (const auto& result : results) {
                if (result.depth == depth && result.mode == mode) {
                    group.push_back(result);
                }
            }
            if (!group.empty()) {
                print_position_summary(group, mode, depth);
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
              << (options.use_transposition_table.value_or(
                      default_search_options.use_transposition_table)
                      ? "on"
                      : "off")
              << '\n';
    std::cout << "pvs: "
              << (options.use_pvs.value_or(default_search_options.use_pvs) ? "on" : "off") << '\n';
    std::cout << "tt entries: " << options.transposition_table_entries << '\n';
    std::cout << "exact endgame threshold: " << options.exact_endgame_empty_threshold << '\n';
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

    if (options.by_position) {
        std::cout << "by-position: on\n";
        std::cout << "per-position elapsed_ms and nodes are totals across repetitions\n\n";

        std::vector<PositionBenchmarkResult> position_results;
        position_results.reserve(positions->size() * options.depths.size() * 2);
        print_position_result_header();
        for (const int depth : options.depths) {
            run_requested_position_benchmarks(*positions, options, depth, position_results);
        }
        print_requested_position_summaries(position_results, options);
        return 0;
    }

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
