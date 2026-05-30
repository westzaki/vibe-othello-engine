#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <vector>

namespace othello::match_runner {

enum class PlayerKind {
    First,
    Random,
    Eval,
    Search,
    ExternalNBoard,
};

struct SearchPlayerOptions {
    int max_depth = SearchOptions{}.max_depth;
    bool use_transposition_table = SearchOptions{}.use_transposition_table;
    std::size_t transposition_table_entries = SearchOptions{}.transposition_table_entries;
    int exact_endgame_empty_threshold = SearchOptions{}.exact_endgame_empty_threshold;
    ExactEndgameRootPolicy exact_endgame_root_policy = SearchOptions{}.exact_endgame_root_policy;
    bool use_pvs = SearchOptions{}.use_pvs;
    EvaluationPreset evaluation_preset = EvaluationPreset::Default;
    std::optional<EvaluationConfig> evaluation_config_override;

    [[nodiscard]] friend bool operator==(const SearchPlayerOptions&,
                                         const SearchPlayerOptions&) = default;
};

struct PlayerSpec {
    PlayerKind kind = PlayerKind::First;
    int depth = 0;
    SearchPlayerOptions search_options;
    std::string external_engine_name;
    std::string text = "first";

    [[nodiscard]] friend bool operator==(const PlayerSpec&, const PlayerSpec&) = default;
};

struct ExternalEngineConfig {
    std::string name;
    int depth = 1;
    std::optional<std::string> cwd;
    std::vector<std::string> command;

    [[nodiscard]] friend bool operator==(const ExternalEngineConfig&,
                                         const ExternalEngineConfig&) = default;
};

struct MoveSelection {
    std::optional<Square> move;
    std::uint64_t nodes = 0;
    double elapsed_ms = 0.0;
    std::uint64_t exact_root_searches = 0;
    ExactEndgameRootDecision exact_root_decision;
    int score = 0;
    int depth = 0;
    std::vector<Square> principal_variation;
    std::optional<SearchStats> search_stats;
};

struct ExactRootTraceStats {
    std::uint64_t nodes = 0;
    std::uint64_t tt_lookups = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_exact_hits = 0;
    std::uint64_t tt_lower_hits = 0;
    std::uint64_t tt_upper_hits = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t tt_overwrites = 0;
    std::uint64_t tt_collisions = 0;
    std::uint64_t tt_rejected_stores = 0;
    std::uint64_t tt_move_ordering_probes = 0;
    std::uint64_t tt_move_ordering_hits = 0;
    std::uint64_t tt_move_ordering_used = 0;

    [[nodiscard]] friend bool operator==(const ExactRootTraceStats&,
                                         const ExactRootTraceStats&) = default;
};

struct ExactRootTrace {
    int ply = 0;
    std::string side;
    std::string player;
    std::string board;
    int empties = 0;
    int legal_moves_current = 0;
    int legal_moves_opponent = 0;
    std::optional<Square> best_move;
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
    double elapsed_ms = 0.0;
    ExactRootTraceStats stats;
    std::vector<Square> principal_variation;

    [[nodiscard]] friend bool operator==(const ExactRootTrace&,
                                         const ExactRootTrace&) = default;
};

struct Opening {
    std::string name = "initial";
    std::vector<std::string> moves;
    Board start_board = Board::initial();
};

struct MatchConfig {
    PlayerSpec player_a;
    PlayerSpec player_b;
    int games = 1;
    bool swap_sides = false;
    std::uint64_t seed = 1;
    std::vector<Opening> openings;
    std::vector<ExternalEngineConfig> external_engines;
    int external_timeout_ms = 10000;
};

struct GameRecord {
    int game_index = 0;
    std::uint64_t seed = 0;
    int opening_index = 0;
    std::string opening_name = "initial";
    std::vector<std::string> opening_moves;
    std::string start_board;
    std::string black_spec;
    std::string white_spec;
    std::string player_a_spec;
    std::string player_b_spec;
    bool black_is_player_a = true;
    std::string winner = "draw";
    int black_score = 0;
    int white_score = 0;
    int score_diff_from_black = 0;
    int score_diff_from_player_a = 0;
    std::uint64_t nodes_black = 0;
    std::uint64_t nodes_white = 0;
    std::uint64_t nodes_player_a = 0;
    std::uint64_t nodes_player_b = 0;
    std::uint64_t exact_roots_black = 0;
    std::uint64_t exact_roots_white = 0;
    std::uint64_t exact_roots_player_a = 0;
    std::uint64_t exact_roots_player_b = 0;
    double time_ms_black = 0.0;
    double time_ms_white = 0.0;
    double time_ms_player_a = 0.0;
    double time_ms_player_b = 0.0;
    int plies = 0;
    int passes = 0;
    std::vector<std::string> moves;
    std::vector<ExactRootTrace> exact_root_events;
    bool illegal_or_error = false;
    std::optional<std::string> error_reason;

    [[nodiscard]] friend bool operator==(const GameRecord&, const GameRecord&) = default;
};

struct OpeningParseResult {
    bool ok = false;
    bool has_opening = false;
    Opening opening;
    std::string error;
};

struct MatchSummary {
    int games = 0;
    int valid_games = 0;
    int error_games = 0;
    int player_a_wins = 0;
    int player_b_wins = 0;
    int draws = 0;
    double average_disc_diff_from_player_a = 0.0;
};

} // namespace othello::match_runner
