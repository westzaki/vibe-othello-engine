#include "benchmarks/search_bench_reporter.hpp"

#include "common/formatting.hpp"
#include "common/jsonl.hpp"
#include "common/search_cli_options.hpp"
#include "common/stats.hpp"
#include "positions/fixtures.hpp"
#include "positions/metrics.hpp"
#include "positions/tags.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace othello::benchmarks::search_bench {

using othello::benchmarks::check_tag_consistency;
using othello::benchmarks::corner_bits;
using othello::benchmarks::count_bits;
using othello::benchmarks::edge_bits;
using othello::benchmarks::has_x_square_risk;
using othello::benchmarks::mobility_bucket;
using othello::benchmarks::same_board;
using othello::benchmarks::split_tags;
using othello::tools::beta_cut_first_move_percentage;
using othello::tools::elapsed_ms;
using othello::tools::format_principal_variation;
using othello::tools::JsonObjectWriter;
using othello::tools::search_score_kind_name;
using othello::tools::tt_hit_percentage;

namespace {} // namespace

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
              << "  " << std::setw(3) << "tt" << "  " << std::setw(13) << "tt_store_leaf"
              << "  " << std::setw(12) << "tt_min_probe"
              << "  " << std::setw(12) << "tt_min_store"
              << "  " << std::setw(3) << "pvs"
              << "  " << std::setw(3) << "asp" << "  " << std::setw(10) << "asp_window"
              << "  " << std::setw(11) << "asp_max_re"
              << "  " << std::setw(10) << "tt_entries"
              << "  exact_profile  exact_pos  exact_roots  positions  depth  best_move  score  "
              << std::setw(28) << "pv"
              << "  searches  elapsed_ms      searches/s  total_nodes         nodes/s"
                 "  nodes/search  searched_moves  legal_nodes  eval_calls  pass_nodes"
                 "  game_over_nodes  beta_cutoffs  beta_cut_first_move_pct"
                 "  tt_lookups  tt_hits  tt_hit_rate  tt_stores  tt_leaf_stores"
                 "  tt_leaf_skip  tt_probe_skip  tt_store_skip  tt_overwrites"
                 "  tt_collisions  tt_rejected_stores  tt_order_probes  tt_order_hits"
                 "  tt_order_used  pvs_scouts  pvs_researches  pvs_scout_cutoffs"
                 "  asp_searches  asp_researches  asp_fail_lows  asp_fail_highs  asp_fallbacks"
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
              << (result.use_transposition_table ? "on" : "off") << "  " << std::setw(13)
              << (result.store_leaf_tt_entries ? "on" : "off") << "  " << std::setw(3)
              << result.tt_min_probe_depth << "  " << std::setw(12) << result.tt_min_store_depth
              << "  " << std::setw(3) << (result.use_pvs ? "on" : "off") << "  " << std::setw(3)
              << (result.use_aspiration_window ? "on" : "off") << "  " << std::right
              << std::setw(10) << result.aspiration_window << "  " << std::setw(11)
              << result.aspiration_max_researches << "  " << std::setw(10)
              << result.transposition_table_entries << "  " << std::setw(13)
              << result.exact_root_profile << "  " << std::setw(9) << result.exact_root_positions
              << "  " << std::setw(11) << result.exact_root_searches << "  " << std::setw(9)
              << result.position_count << "  " << std::setw(5) << result.depth << "  " << std::left
              << std::setw(9)
              << (result.sample_best_move.has_value() ? othello::to_string(*result.sample_best_move)
                                                      : "-")
              << "  " << std::right << std::setw(5) << result.sample_score << "  " << std::left
              << std::setw(28) << principal_variation_text << "  " << std::right << std::setw(8)
              << result.searches << "  " << std::fixed << std::setprecision(3) << std::setw(10)
              << elapsed_ms << "  " << std::setw(14) << searches_per_second << "  " << std::setw(11)
              << result.total_nodes << "  " << std::setw(14) << nodes_per_second << "  "
              << std::setw(12) << nodes_per_search << "  " << std::setw(14)
              << result.total_stats.searched_moves << "  " << std::setw(11)
              << result.total_stats.legal_move_nodes << "  " << std::setw(10)
              << result.total_stats.eval_calls << "  " << std::setw(10)
              << result.total_stats.pass_nodes << "  " << std::setw(15)
              << result.total_stats.game_over_nodes << "  " << std::setw(12)
              << result.total_stats.beta_cutoffs << "  " << std::setw(23)
              << beta_cut_first_move_percentage(result.total_stats) << "  " << std::setw(10)
              << result.total_stats.tt_lookups << "  " << std::setw(7) << result.total_stats.tt_hits
              << "  " << std::setw(11) << tt_hit_percentage(result.total_stats) << "  "
              << std::setw(9) << result.total_stats.tt_stores << "  " << std::setw(14)
              << result.total_stats.tt_leaf_stores << "  " << std::setw(13)
              << result.total_stats.tt_leaf_store_skipped << "  " << std::setw(13)
              << result.total_stats.tt_probe_skipped_by_depth << "  " << std::setw(13)
              << result.total_stats.tt_store_skipped_by_depth << "  " << std::setw(13)
              << result.total_stats.tt_overwrites << "  " << std::setw(13)
              << result.total_stats.tt_collisions << "  " << std::setw(18)
              << result.total_stats.tt_rejected_stores << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_probes << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_hits << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_used << "  " << std::setw(9)
              << result.total_stats.pvs_scouts << "  " << std::setw(14)
              << result.total_stats.pvs_researches << "  " << std::setw(17)
              << result.total_stats.pvs_scout_cutoffs << "  " << std::setw(9)
              << result.total_stats.aspiration_searches << "  " << std::setw(14)
              << result.total_stats.aspiration_researches << "  " << std::setw(13)
              << result.total_stats.aspiration_fail_lows << "  " << std::setw(14)
              << result.total_stats.aspiration_fail_highs << "  " << std::setw(13)
              << result.total_stats.aspiration_full_window_fallbacks << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_nodes << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_moves << "  " << result.result_checksum << "  "
              << result.work_checksum << '\n';
}

void print_position_result_header() {
    std::cout
        << std::left << std::setw(28) << "position" << "  " << std::setw(14) << "phase"
        << "  " << std::setw(44) << "tags" << "  " << std::setw(10) << "mode"
        << "  " << std::setw(3) << "tt" << "  " << std::setw(13) << "tt_store_leaf"
        << "  " << std::setw(12) << "tt_min_probe"
        << "  " << std::setw(12) << "tt_min_store"
        << "  " << std::setw(3) << "pvs"
        << "  " << std::setw(3) << "asp" << "  " << std::setw(10) << "asp_window"
        << "  " << std::setw(11) << "asp_max_re"
        << "  " << std::setw(10) << "tt_entries"
        << "  exact_profile  empty  exact_root  exact_skip_reason            depth  best_move  "
           "score  "
        << std::setw(28) << "pv"
        << "  searches  elapsed_ms       nodes  nodes/search         nodes/s"
           "  searched_moves  legal_nodes  eval_calls  pass_nodes  game_over_nodes"
           "  beta_cutoffs  beta_cut_first_move_pct"
           "  tt_lookups  tt_hits  tt_hit_rate  tt_stores  tt_leaf_stores"
           "  tt_leaf_skip  tt_probe_skip  tt_store_skip  tt_overwrites"
           "  tt_collisions  tt_rejected_stores  tt_order_probes  tt_order_hits  tt_order_used"
           "  pvs_scouts  pvs_researches  pvs_scout_cutoffs"
           "  asp_searches  asp_researches  asp_fail_lows  asp_fail_highs  asp_fallbacks"
           "  dyn_nodes  dyn_moves  result_checksum  work_checksum\n";
}

void print_position_result(const PositionBenchmarkResult& result) {
    const std::string principal_variation_text =
        format_principal_variation(result.sample_principal_variation);

    std::cout << std::left << std::setw(28) << result.position_name << "  " << std::setw(14)
              << result.phase << "  " << std::setw(44) << (result.tags.empty() ? "-" : result.tags)
              << "  " << std::setw(10) << mode_name(result.mode) << "  " << std::setw(3)
              << (result.use_transposition_table ? "on" : "off") << "  " << std::setw(13)
              << (result.store_leaf_tt_entries ? "on" : "off") << "  " << std::setw(3)
              << result.tt_min_probe_depth << "  " << std::setw(12) << result.tt_min_store_depth
              << "  " << std::setw(3) << (result.use_pvs ? "on" : "off") << "  " << std::setw(3)
              << (result.use_aspiration_window ? "on" : "off") << "  " << std::right
              << std::setw(10) << result.aspiration_window << "  " << std::setw(11)
              << result.aspiration_max_researches << "  " << std::setw(10)
              << result.transposition_table_entries << "  " << std::setw(13)
              << result.exact_root_profile << "  " << std::setw(5) << result.empty_count << "  "
              << std::setw(10) << (result.exact_root ? "yes" : "no") << "  " << std::left
              << std::setw(28) << result.exact_skip_reason << "  " << std::right << std::setw(5)
              << result.depth << "  " << std::left << std::setw(9)
              << (result.sample_best_move.has_value() ? othello::to_string(*result.sample_best_move)
                                                      : "-")
              << "  " << std::right << std::setw(5) << result.sample_score << "  " << std::left
              << std::setw(28) << principal_variation_text << "  " << std::right << std::setw(8)
              << result.searches << "  " << std::fixed << std::setprecision(3) << std::setw(10)
              << elapsed_ms(result.elapsed) << "  " << std::setw(10) << result.total_nodes << "  "
              << std::setw(12) << nodes_per_search(result) << "  " << std::setw(14)
              << nodes_per_second(result) << "  " << std::setw(14)
              << result.total_stats.searched_moves << "  " << std::setw(11)
              << result.total_stats.legal_move_nodes << "  " << std::setw(10)
              << result.total_stats.eval_calls << "  " << std::setw(10)
              << result.total_stats.pass_nodes << "  " << std::setw(15)
              << result.total_stats.game_over_nodes << "  " << std::setw(12)
              << result.total_stats.beta_cutoffs << "  " << std::setw(23)
              << beta_cut_first_move_percentage(result.total_stats) << "  " << std::setw(10)
              << result.total_stats.tt_lookups << "  " << std::setw(7) << result.total_stats.tt_hits
              << "  " << std::setw(11) << tt_hit_percentage(result.total_stats) << "  "
              << std::setw(9) << result.total_stats.tt_stores << "  " << std::setw(14)
              << result.total_stats.tt_leaf_stores << "  " << std::setw(13)
              << result.total_stats.tt_leaf_store_skipped << "  " << std::setw(13)
              << result.total_stats.tt_probe_skipped_by_depth << "  " << std::setw(13)
              << result.total_stats.tt_store_skipped_by_depth << "  " << std::setw(13)
              << result.total_stats.tt_overwrites << "  " << std::setw(13)
              << result.total_stats.tt_collisions << "  " << std::setw(18)
              << result.total_stats.tt_rejected_stores << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_probes << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_hits << "  " << std::setw(9)
              << result.total_stats.tt_move_ordering_used << "  " << std::setw(9)
              << result.total_stats.pvs_scouts << "  " << std::setw(14)
              << result.total_stats.pvs_researches << "  " << std::setw(17)
              << result.total_stats.pvs_scout_cutoffs << "  " << std::setw(9)
              << result.total_stats.aspiration_searches << "  " << std::setw(14)
              << result.total_stats.aspiration_researches << "  " << std::setw(13)
              << result.total_stats.aspiration_fail_lows << "  " << std::setw(14)
              << result.total_stats.aspiration_fail_highs << "  " << std::setw(13)
              << result.total_stats.aspiration_full_window_fallbacks << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_nodes << "  " << std::setw(9)
              << result.total_stats.dynamic_ordering_moves << "  " << result.result_checksum << "  "
              << result.work_checksum << '\n';
}

void write_json_optional_int_field(JsonObjectWriter& writer, std::string_view name,
                                   std::optional<int> value) {
    if (value.has_value()) {
        writer.int_field(name, *value);
    } else {
        writer.null_field(name);
    }
}

void write_json_square_field(JsonObjectWriter& writer, std::string_view name,
                             std::optional<othello::Square> square) {
    if (square.has_value()) {
        writer.string_field(name, othello::to_string(*square));
    } else {
        writer.null_field(name);
    }
}

void write_json_pv_field(JsonObjectWriter& writer, std::ostream& output, std::string_view name,
                         const std::vector<othello::Square>& principal_variation) {
    writer.field_name(name);
    output << '[';
    bool first_move = true;
    for (const othello::Square square : principal_variation) {
        if (!first_move) {
            output << ',';
        }
        first_move = false;
        othello::tools::write_json_string(output, othello::to_string(square));
    }
    output << ']';
}

void write_json_root_ordering_field(JsonObjectWriter& writer,
                                    const RootMoveOrderingDiagnostic& diagnostic) {
    writer.field_name("ordered_root_moves");
    std::cout << '[';
    bool first = true;
    for (const auto& entry : diagnostic.moves) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        JsonObjectWriter move_writer{std::cout};
        move_writer.begin_object();
        move_writer.string_field("move", othello::to_string(entry.move));
        move_writer.int_field("order_score", entry.order_score);
        move_writer.end_object();
    }
    std::cout << ']';
}

[[nodiscard]] double pvs_research_ratio(const othello::SearchStats& stats) noexcept {
    return stats.pvs_scouts == 0
               ? 0.0
               : static_cast<double>(stats.pvs_researches) / static_cast<double>(stats.pvs_scouts);
}

[[nodiscard]] double average_distance(std::uint64_t total, std::uint64_t count) noexcept {
    if (count == 0) {
        return 0.0;
    }
    return static_cast<double>(total) / static_cast<double>(count);
}

void write_json_search_stats_fields(JsonObjectWriter& writer, const othello::SearchStats& stats) {
    writer.uint_field("searched_moves", stats.searched_moves);
    writer.uint_field("legal_nodes", stats.legal_move_nodes);
    writer.uint_field("eval_calls", stats.eval_calls);
    writer.uint_field("pass_nodes", stats.pass_nodes);
    writer.uint_field("game_over_nodes", stats.game_over_nodes);
    writer.uint_field("beta_cutoffs", stats.beta_cutoffs);
    writer.double_field("beta_cut_first_move_pct", beta_cut_first_move_percentage(stats));
    writer.uint_field("tt_lookups", stats.tt_lookups);
    writer.uint_field("tt_hits", stats.tt_hits);
    writer.double_field("tt_hit_rate", tt_hit_percentage(stats));
    writer.uint_field("tt_stores", stats.tt_stores);
    writer.uint_field("tt_leaf_stores", stats.tt_leaf_stores);
    writer.uint_field("tt_leaf_store_skipped", stats.tt_leaf_store_skipped);
    writer.uint_field("tt_probe_skipped_by_depth", stats.tt_probe_skipped_by_depth);
    writer.uint_field("tt_store_skipped_by_depth", stats.tt_store_skipped_by_depth);
    writer.uint_field("tt_overwrites", stats.tt_overwrites);
    writer.uint_field("tt_collisions", stats.tt_collisions);
    writer.uint_field("tt_rejected_stores", stats.tt_rejected_stores);
    writer.uint_field("tt_order_probes", stats.tt_move_ordering_probes);
    writer.uint_field("tt_order_hits", stats.tt_move_ordering_hits);
    writer.uint_field("tt_order_used", stats.tt_move_ordering_used);
    writer.uint_field("pvs_scouts", stats.pvs_scouts);
    writer.uint_field("pvs_researches", stats.pvs_researches);
    writer.uint_field("pvs_scout_cutoffs", stats.pvs_scout_cutoffs);
    writer.uint_field("asp_searches", stats.aspiration_searches);
    writer.uint_field("asp_researches", stats.aspiration_researches);
    writer.uint_field("asp_fail_lows", stats.aspiration_fail_lows);
    writer.uint_field("asp_fail_highs", stats.aspiration_fail_highs);
    writer.uint_field("asp_fallbacks", stats.aspiration_full_window_fallbacks);
    writer.uint_field("dyn_nodes", stats.dynamic_ordering_nodes);
    writer.uint_field("dyn_moves", stats.dynamic_ordering_moves);
}

void write_json_instrumentation_stats_fields(JsonObjectWriter& writer,
                                             const othello::SearchStats& stats) {
    write_json_search_stats_fields(writer, stats);
    writer.double_field("pvs_research_ratio", pvs_research_ratio(stats));
    writer.uint_field("asp_fail_low_distance_sum", stats.aspiration_fail_low_distance_sum);
    writer.uint_field("asp_fail_high_distance_sum", stats.aspiration_fail_high_distance_sum);
    writer.double_field(
        "asp_fail_low_distance_avg",
        average_distance(stats.aspiration_fail_low_distance_sum, stats.aspiration_fail_lows));
    writer.double_field(
        "asp_fail_high_distance_avg",
        average_distance(stats.aspiration_fail_high_distance_sum, stats.aspiration_fail_highs));
}

void write_search_jsonl(const SearchBenchmarkResult& result, PositionSet position_set,
                        std::uint64_t repetitions) {
    const auto elapsed_count = result.elapsed.count();
    const double searches_per_second =
        elapsed_count == 0 ? 0.0
                           : (static_cast<double>(result.searches) * 1'000'000'000.0) /
                                 static_cast<double>(elapsed_count);
    const double nodes_per_second =
        elapsed_count == 0 ? 0.0
                           : (static_cast<double>(result.total_nodes) * 1'000'000'000.0) /
                                 static_cast<double>(elapsed_count);
    const double nodes_per_search = result.searches == 0 ? 0.0
                                                         : static_cast<double>(result.total_nodes) /
                                                               static_cast<double>(result.searches);

    JsonObjectWriter writer{std::cout};
    writer.begin_object();
    writer.string_field("tool", "othello_search_bench");
    writer.string_field("row", "aggregate");
    writer.string_field("mode", mode_name(result.mode));
    writer.int_field("depth", result.depth);
    writer.string_field("positions", position_set_name(position_set));
    writer.uint_field("repetitions", repetitions);
    writer.bool_field("tt", result.use_transposition_table);
    writer.bool_field("tt_store_leaf", result.store_leaf_tt_entries);
    writer.int_field("tt_min_probe_depth", result.tt_min_probe_depth);
    writer.int_field("tt_min_store_depth", result.tt_min_store_depth);
    writer.bool_field("pvs", result.use_pvs);
    writer.bool_field("aspiration", result.use_aspiration_window);
    writer.int_field("aspiration_window", result.aspiration_window);
    writer.int_field("aspiration_max_researches", result.aspiration_max_researches);
    writer.string_field("aspiration_profile",
                        othello::tools::aspiration_profile_name(result.aspiration_profile));
    writer.uint_field("tt_entries", static_cast<std::uint64_t>(result.transposition_table_entries));
    writer.string_field("exact_endgame_profile", result.exact_root_profile);
    writer.uint_field("exact_root_positions", result.exact_root_positions);
    writer.uint_field("exact_root_searches", result.exact_root_searches);
    writer.uint_field("position_count", result.position_count);
    write_json_square_field(writer, "best_move", result.sample_best_move);
    writer.int_field("score", result.sample_score);
    writer.string_field("score_kind", search_score_kind_name(result.sample_score_kind));
    writer.bool_field("used_exact_endgame", result.sample_used_exact_endgame);
    write_json_optional_int_field(writer, "exact_disc_margin", result.sample_exact_disc_margin);
    write_json_pv_field(writer, std::cout, "principal_variation",
                        result.sample_principal_variation);
    writer.uint_field("searches", result.searches);
    writer.double_field("elapsed_ms", elapsed_ms(result.elapsed));
    writer.double_field("searches_per_second", searches_per_second);
    writer.uint_field("nodes", result.total_nodes);
    writer.double_field("nps", nodes_per_second);
    writer.double_field("nodes_per_search", nodes_per_search);
    write_json_search_stats_fields(writer, result.total_stats);
    writer.string_field("result_checksum", std::to_string(result.result_checksum));
    writer.string_field("work_checksum", std::to_string(result.work_checksum));
    writer.end_object();
    std::cout << '\n';
}

void write_iterative_depth_jsonl(const IterativeDepthBenchmarkResult& result,
                                 PositionSet position_set, std::uint64_t repetitions) {
    JsonObjectWriter writer{std::cout};
    writer.begin_object();
    writer.string_field("tool", "othello_search_bench");
    writer.string_field("row", "iterative_depth");
    writer.string_field("position", result.position_name);
    writer.string_field("phase", result.phase);
    writer.string_field("tags", result.tags);
    writer.string_field("mode", mode_name(result.mode));
    writer.string_field("positions", position_set_name(position_set));
    writer.int_field("depth", result.requested_depth);
    writer.int_field("completed_depth", result.completed_depth);
    writer.uint_field("repetitions", repetitions);
    writer.bool_field("tt", result.use_transposition_table);
    writer.bool_field("tt_store_leaf", result.store_leaf_tt_entries);
    writer.int_field("tt_min_probe_depth", result.tt_min_probe_depth);
    writer.int_field("tt_min_store_depth", result.tt_min_store_depth);
    writer.bool_field("pvs", result.use_pvs);
    writer.bool_field("aspiration", result.use_aspiration_window);
    writer.int_field("aspiration_window", result.aspiration_window);
    writer.int_field("aspiration_max_researches", result.aspiration_max_researches);
    writer.string_field("aspiration_profile",
                        othello::tools::aspiration_profile_name(result.aspiration_profile));
    writer.uint_field("tt_entries", static_cast<std::uint64_t>(result.transposition_table_entries));
    writer.string_field("exact_endgame_profile", result.exact_root_profile);
    writer.int_field("empty_count", result.empty_count);
    writer.bool_field("exact_root", result.exact_root);
    writer.string_field("exact_skip_reason", result.exact_skip_reason);
    write_json_optional_int_field(writer, "previous_score", result.previous_score);
    writer.int_field("score", result.score);
    writer.int_field("previous_score_delta", result.previous_score_delta);
    write_json_square_field(writer, "best_move", result.best_move);
    write_json_pv_field(writer, std::cout, "principal_variation", result.principal_variation);
    writer.double_field("elapsed_ms", elapsed_ms(result.elapsed));
    writer.uint_field("nodes", result.nodes);
    write_json_instrumentation_stats_fields(writer, result.stats);
    writer.string_field("result_checksum", std::to_string(result.result_checksum));
    writer.string_field("work_checksum", std::to_string(result.work_checksum));
    writer.end_object();
    std::cout << '\n';
}

void write_position_jsonl(const PositionBenchmarkResult& result, PositionSet position_set,
                          std::uint64_t repetitions, bool include_instrumentation) {
    JsonObjectWriter writer{std::cout};
    writer.begin_object();
    writer.string_field("tool", "othello_search_bench");
    writer.string_field("row", "position");
    writer.string_field("position", result.position_name);
    writer.string_field("phase", result.phase);
    writer.string_field("tags", result.tags);
    writer.string_field("mode", mode_name(result.mode));
    writer.string_field("positions", position_set_name(position_set));
    writer.int_field("depth", result.depth);
    writer.uint_field("repetitions", repetitions);
    writer.bool_field("tt", result.use_transposition_table);
    writer.bool_field("tt_store_leaf", result.store_leaf_tt_entries);
    writer.int_field("tt_min_probe_depth", result.tt_min_probe_depth);
    writer.int_field("tt_min_store_depth", result.tt_min_store_depth);
    writer.bool_field("pvs", result.use_pvs);
    writer.bool_field("aspiration", result.use_aspiration_window);
    writer.int_field("aspiration_window", result.aspiration_window);
    writer.int_field("aspiration_max_researches", result.aspiration_max_researches);
    writer.string_field("aspiration_profile",
                        othello::tools::aspiration_profile_name(result.aspiration_profile));
    writer.uint_field("tt_entries", static_cast<std::uint64_t>(result.transposition_table_entries));
    writer.string_field("exact_endgame_profile", result.exact_root_profile);
    writer.int_field("empty_count", result.empty_count);
    writer.bool_field("exact_root", result.exact_root);
    writer.string_field("exact_skip_reason", result.exact_skip_reason);
    write_json_square_field(writer, "best_move", result.sample_best_move);
    writer.int_field("score", result.sample_score);
    writer.string_field("score_kind", search_score_kind_name(result.sample_score_kind));
    writer.bool_field("used_exact_endgame", result.sample_used_exact_endgame);
    write_json_optional_int_field(writer, "exact_disc_margin", result.sample_exact_disc_margin);
    write_json_pv_field(writer, std::cout, "principal_variation",
                        result.sample_principal_variation);
    writer.uint_field("searches", result.searches);
    writer.double_field("elapsed_ms", elapsed_ms(result.elapsed));
    writer.uint_field("nodes", result.total_nodes);
    writer.double_field("nodes_per_search", nodes_per_search(result));
    writer.double_field("nps", nodes_per_second(result));
    write_json_search_stats_fields(writer, result.total_stats);
    if (include_instrumentation) {
        writer.uint_field("root_move_count", result.root_move_ordering.moves.size());
        write_json_optional_int_field(writer, "best_move_initial_order_rank",
                                      result.root_move_ordering.best_move_initial_order_rank);
        write_json_root_ordering_field(writer, result.root_move_ordering);
    }
    writer.string_field("result_checksum", std::to_string(result.result_checksum));
    writer.string_field("work_checksum", std::to_string(result.work_checksum));
    writer.end_object();
    std::cout << '\n';
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
              << "  " << std::setw(13) << "tt_store_leaf" << "  " << std::setw(12) << "tt_min_probe"
              << "  " << std::setw(12) << "tt_min_store" << "  " << std::setw(3) << "pvs" << "  "
              << std::setw(3) << "asp"
              << "  exact_profile  exact_pos  exact_roots  depth  positions  total_elapsed_ms"
                 "  avg_ms_per_position"
                 "  p50_ms_per_position  p95_ms_per_position  max_ms_per_position"
                 "  avg_nodes_per_position  p50_nodes_per_position  p95_nodes_per_position"
                 "  max_nodes_per_position\n";
}

void print_position_summary(std::span<const PositionBenchmarkResult> results, SearchRunMode mode,
                            int depth, std::string_view exact_root_profile) {
    std::vector<double> per_position_ms;
    std::vector<std::uint64_t> per_position_nodes;
    per_position_ms.reserve(results.size());
    per_position_nodes.reserve(results.size());

    std::chrono::nanoseconds total_elapsed{0};
    std::uint64_t total_nodes = 0;
    bool use_transposition_table = false;
    bool store_leaf_tt_entries = true;
    int tt_min_probe_depth = 0;
    int tt_min_store_depth = 0;
    bool use_pvs = false;
    bool use_aspiration_window = false;
    std::uint64_t exact_root_positions = 0;
    std::uint64_t exact_root_searches = 0;

    for (const auto& result : results) {
        per_position_ms.push_back(elapsed_ms(result.elapsed));
        per_position_nodes.push_back(result.total_nodes);
        total_elapsed += result.elapsed;
        total_nodes += result.total_nodes;
        use_transposition_table = result.use_transposition_table;
        store_leaf_tt_entries = result.store_leaf_tt_entries;
        tt_min_probe_depth = result.tt_min_probe_depth;
        tt_min_store_depth = result.tt_min_store_depth;
        use_pvs = result.use_pvs;
        use_aspiration_window = result.use_aspiration_window;
        if (result.exact_root) {
            ++exact_root_positions;
            exact_root_searches += result.searches;
        }
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
              << (use_transposition_table ? "on" : "off") << "  " << std::setw(13)
              << (store_leaf_tt_entries ? "on" : "off") << "  " << std::setw(12)
              << tt_min_probe_depth << "  " << std::setw(12) << tt_min_store_depth << "  "
              << std::setw(3) << (use_pvs ? "on" : "off") << "  " << std::setw(3)
              << (use_aspiration_window ? "on" : "off") << "  " << std::right << std::setw(13)
              << exact_root_profile << "  " << std::setw(9) << exact_root_positions << "  "
              << std::setw(11) << exact_root_searches << "  " << std::setw(5) << depth << "  "
              << std::setw(9) << position_count << "  " << std::fixed << std::setprecision(3)
              << std::setw(16) << elapsed_ms(total_elapsed) << "  " << std::setw(19) << avg_ms
              << "  " << std::setw(20) << percentile_value(per_position_ms, 50) << "  "
              << std::setw(20) << percentile_value(per_position_ms, 95) << "  " << std::setw(19)
              << max_ms << "  " << std::setw(22) << avg_nodes << "  " << std::setw(22)
              << percentile_value(per_position_nodes, 50) << "  " << std::setw(22)
              << percentile_value(per_position_nodes, 95) << "  " << std::setw(22) << max_nodes
              << '\n';
}

} // namespace othello::benchmarks::search_bench
