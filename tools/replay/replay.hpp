#pragma once

#include <filesystem>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace othello::replay {

enum class OutputFormat {
    Markdown,
    Jsonl,
};

struct ReplayResult {
    bool ok = false;
    Board board;
    int turns = 0;
    int passes = 0;
    std::string error;
};

struct GameRecord {
    int game_index = 0;
    int opening_index = 0;
    std::string opening_name = "initial";
    std::vector<std::string> opening_moves;
    std::string start_board;
    bool black_is_player_a = true;
    int score_diff_from_player_a = 0;
    std::vector<std::string> moves;
    bool illegal_or_error = false;
};

struct Divergence {
    int pair_index = 0;
    int opening_index = 0;
    std::string opening_name;
    int head_game_index = 0;
    int base_game_index = 0;
    int ply = 0;
    std::string side_to_move;
    std::string board_text;
    std::string head_move;
    std::string base_move;
    int head_final_diff = 0;
    int base_game_head_final_diff = 0;
    bool illegal_or_error = false;
    std::vector<std::string> preceding_moves;
};

struct ParseRecordResult {
    bool ok = false;
    bool empty = false;
    GameRecord record;
    std::string error;
};

struct ReadRecordsResult {
    bool ok = false;
    std::vector<GameRecord> records;
    std::string error;
};

struct ExtractDivergencesResult {
    bool ok = false;
    std::vector<Divergence> divergences;
    std::string error;
};

[[nodiscard]] ReplayResult replay_moves(const Board& start_board,
                                        std::span<const std::string> moves);
[[nodiscard]] ReplayResult replay_moves(std::string_view start_board_text,
                                        std::span<const std::string> moves);
[[nodiscard]] ParseRecordResult parse_match_jsonl_record(std::string_view line);
[[nodiscard]] ReadRecordsResult read_match_jsonl_records(const std::filesystem::path& path);
[[nodiscard]] ExtractDivergencesResult extract_divergences(std::span<const GameRecord> records);
[[nodiscard]] std::string render_divergences(std::span<const Divergence> divergences,
                                             OutputFormat format);

} // namespace othello::replay
