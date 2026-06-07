#include "match_runner/jsonl_writer.hpp"

#include "common/jsonl.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <system_error>

namespace othello::match_runner {

namespace {

void write_optional_square(std::ostream& output, const std::optional<Square>& square) {
    if (square.has_value()) {
        tools::write_json_string(output, to_string(*square));
    } else {
        output << "null";
    }
}

void write_square_array(std::ostream& output, std::span<const Square> squares) {
    output << "[";
    for (std::size_t index = 0; index < squares.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        tools::write_json_string(output, to_string(squares[index]));
    }
    output << "]";
}

void write_exact_root_trace_stats(std::ostream& output, const ExactRootTraceStats& stats) {
    output << "{";
    output << "\"nodes\":" << stats.nodes << ',';
    output << "\"tt_lookups\":" << stats.tt_lookups << ',';
    output << "\"tt_hits\":" << stats.tt_hits << ',';
    output << "\"tt_exact_hits\":" << stats.tt_exact_hits << ',';
    output << "\"tt_lower_hits\":" << stats.tt_lower_hits << ',';
    output << "\"tt_upper_hits\":" << stats.tt_upper_hits << ',';
    output << "\"tt_stores\":" << stats.tt_stores << ',';
    output << "\"tt_leaf_stores\":" << stats.tt_leaf_stores << ',';
    output << "\"tt_leaf_store_skipped\":" << stats.tt_leaf_store_skipped << ',';
    output << "\"tt_probe_skipped_by_depth\":" << stats.tt_probe_skipped_by_depth << ',';
    output << "\"tt_store_skipped_by_depth\":" << stats.tt_store_skipped_by_depth << ',';
    output << "\"tt_overwrites\":" << stats.tt_overwrites << ',';
    output << "\"tt_collisions\":" << stats.tt_collisions << ',';
    output << "\"tt_rejected_stores\":" << stats.tt_rejected_stores << ',';
    output << "\"tt_move_ordering_probes\":" << stats.tt_move_ordering_probes << ',';
    output << "\"tt_move_ordering_hits\":" << stats.tt_move_ordering_hits << ',';
    output << "\"tt_move_ordering_used\":" << stats.tt_move_ordering_used << ',';
    output << "\"ordering_full_builds\":" << stats.ordering_full_builds << ',';
    output << "\"ordering_lazy_first_hits\":" << stats.ordering_lazy_first_hits << ',';
    output << "\"ordering_lazy_cut_before_full_sort\":" << stats.ordering_lazy_cut_before_full_sort
           << ',';
    output << "\"ordering_scored_moves_saved\":" << stats.ordering_scored_moves_saved << ',';
    output << "\"preferred_move_legal_count\":" << stats.preferred_move_legal_count << ',';
    output << "\"preferred_move_beta_cut_count\":" << stats.preferred_move_beta_cut_count;
    output << "}";
}

void write_exact_root_trace(std::ostream& output, const ExactRootTrace& trace) {
    output << "{";
    output << "\"ply\":" << trace.ply << ',';
    output << "\"side\":";
    tools::write_json_string(output, trace.side);
    output << ',';
    output << "\"player\":";
    tools::write_json_string(output, trace.player);
    output << ',';
    output << "\"board\":";
    tools::write_json_string(output, trace.board);
    output << ',';
    output << "\"empties\":" << trace.empties << ',';
    output << "\"legal_moves_current\":" << trace.legal_moves_current << ',';
    output << "\"legal_moves_opponent\":" << trace.legal_moves_opponent << ',';
    output << "\"best_move\":";
    write_optional_square(output, trace.best_move);
    output << ',';
    output << "\"score\":" << trace.score << ',';
    output << "\"depth\":" << trace.depth << ',';
    output << "\"nodes\":" << trace.nodes << ',';
    output << "\"elapsed_ms\":" << trace.elapsed_ms << ',';
    output << "\"stats\":";
    write_exact_root_trace_stats(output, trace.stats);
    output << ',';
    output << "\"pv\":";
    write_square_array(output, trace.principal_variation);
    output << "}";
}

void write_jsonl_record(std::ostream& output, const GameRecord& record) {
    output << "{";
    output << "\"game_index\":" << record.game_index << ',';
    output << "\"seed\":" << record.seed << ',';
    output << "\"opening_index\":" << record.opening_index << ',';
    output << "\"opening_name\":";
    tools::write_json_string(output, record.opening_name);
    output << ',';
    output << "\"opening_moves\":[";
    for (std::size_t index = 0; index < record.opening_moves.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        tools::write_json_string(output, record.opening_moves[index]);
    }
    output << "],";
    output << "\"start_board\":";
    tools::write_json_string(output, record.start_board);
    output << ',';
    output << "\"black_spec\":";
    tools::write_json_string(output, record.black_spec);
    output << ',';
    output << "\"white_spec\":";
    tools::write_json_string(output, record.white_spec);
    output << ',';
    output << "\"black_is_player_a\":" << (record.black_is_player_a ? "true" : "false") << ',';
    output << "\"player_a_spec\":";
    tools::write_json_string(output, record.player_a_spec);
    output << ',';
    output << "\"player_b_spec\":";
    tools::write_json_string(output, record.player_b_spec);
    output << ',';
    output << "\"winner\":";
    tools::write_json_string(output, record.winner);
    output << ',';
    output << "\"black_score\":" << record.black_score << ',';
    output << "\"white_score\":" << record.white_score << ',';
    output << "\"score_diff_from_black\":" << record.score_diff_from_black << ',';
    output << "\"score_diff_from_player_a\":" << record.score_diff_from_player_a << ',';
    output << "\"nodes_black\":" << record.nodes_black << ',';
    output << "\"nodes_white\":" << record.nodes_white << ',';
    output << "\"nodes_player_a\":" << record.nodes_player_a << ',';
    output << "\"nodes_player_b\":" << record.nodes_player_b << ',';
    output << "\"exact_roots_black\":" << record.exact_roots_black << ',';
    output << "\"exact_roots_white\":" << record.exact_roots_white << ',';
    output << "\"exact_roots_player_a\":" << record.exact_roots_player_a << ',';
    output << "\"exact_roots_player_b\":" << record.exact_roots_player_b << ',';
    output << "\"time_ms_black\":" << record.time_ms_black << ',';
    output << "\"time_ms_white\":" << record.time_ms_white << ',';
    output << "\"time_ms_player_a\":" << record.time_ms_player_a << ',';
    output << "\"time_ms_player_b\":" << record.time_ms_player_b << ',';
    output << "\"plies\":" << record.plies << ',';
    output << "\"passes\":" << record.passes << ',';
    output << "\"moves\":[";
    for (std::size_t index = 0; index < record.moves.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        tools::write_json_string(output, record.moves[index]);
    }
    output << "],";
    output << "\"exact_root_events\":[";
    for (std::size_t index = 0; index < record.exact_root_events.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_exact_root_trace(output, record.exact_root_events[index]);
    }
    output << "],";
    output << "\"illegal_or_error\":" << (record.illegal_or_error ? "true" : "false") << ',';
    output << "\"error_reason\":";
    if (record.error_reason.has_value()) {
        tools::write_json_string(output, *record.error_reason);
    } else {
        output << "null";
    }
    output << "}\n";
}

} // namespace

bool write_jsonl_file(const std::filesystem::path& output_path,
                      std::span<const GameRecord> records) {
    if (output_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error) {
            std::cerr << "failed to create output directory: " << output_path.parent_path() << '\n';
            return false;
        }
    }

    std::ofstream output{output_path};
    if (!output) {
        std::cerr << "failed to open output file: " << output_path << '\n';
        return false;
    }

    for (const GameRecord& record : records) {
        write_jsonl_record(output, record);
    }

    if (!output) {
        std::cerr << "failed to write output file: " << output_path << '\n';
        return false;
    }

    return true;
}

} // namespace othello::match_runner
