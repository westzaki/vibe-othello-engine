#pragma once

#include <string>
#include <vector>

namespace othello::match_summary {

struct GameRecord {
    int game_index = 0;
    std::string player_a_spec;
    std::string player_b_spec;
    std::string black_spec;
    std::string white_spec;
    bool black_is_player_a = true;
    int opening_index = 0;
    std::string opening_name;
    std::string winner;
    int black_score = 0;
    int white_score = 0;
    int score_diff_from_player_a = 0;
    int plies = 0;
    int passes = 0;
    bool illegal_or_error = false;
};

struct ParseResult {
    bool ok = false;
    GameRecord record;
    std::string error;
};

struct OpeningSummary {
    int opening_index = 0;
    std::string opening_name;
    int games = 0;
    int valid_games = 0;
    int error_games = 0;
    int player_a_wins = 0;
    int player_b_wins = 0;
    int draws = 0;
    double average_disc_diff_from_player_a = 0.0;
};

struct Summary {
    int games = 0;
    int valid_games = 0;
    int error_games = 0;
    int player_a_wins = 0;
    int player_b_wins = 0;
    int draws = 0;
    double player_a_win_rate = 0.0;
    double player_b_win_rate = 0.0;
    double average_disc_diff_from_player_a = 0.0;
    double average_plies = 0.0;
    double average_passes = 0.0;
    int unique_openings_count = 0;
    std::vector<std::string> player_a_specs;
    std::vector<std::string> player_b_specs;
    std::vector<OpeningSummary> openings;
};

} // namespace othello::match_summary
