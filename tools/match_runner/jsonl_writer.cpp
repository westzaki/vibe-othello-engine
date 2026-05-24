#include "match_runner/jsonl_writer.hpp"

#include "common/jsonl.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace othello::match_runner {

namespace {

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
            std::cerr << "failed to create output directory: " << output_path.parent_path()
                      << '\n';
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
