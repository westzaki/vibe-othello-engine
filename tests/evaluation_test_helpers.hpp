#pragma once

#include "common/eval_config_io.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace othello::test::evaluation {

using ::othello::Bitboard;
using ::othello::Board;
using ::othello::Side;


[[nodiscard]] inline Board midgame_board() {
    return othello::test::board_from_text(R"(........
.B......
..B.B...
...BB...
.WWWWW..
..B.....
........
........
side=W)");
}

[[nodiscard]] inline Board phase_midgame_board() {
    return othello::test::board_from_text(R"(.....W..
...WWW..
...WWW..
..WWWWBB
..WWWWBW
..WWWWWW
...WB...
...W....
side=B)");
}

[[nodiscard]] inline Board phase_late_board() {
    return othello::test::board_from_text(R"(...B....
WWBWWW.B
WWWWWWWW
WBWWWWW.
WWWBBWBB
WBBBWB.B
WWBWBWBB
WWWWWWWB
side=B)");
}

[[nodiscard]] inline std::filesystem::path sample_eval_config_path(std::string_view file_name) {
    return std::filesystem::path{OTHELLO_SOURCE_DIR} / "data" / "eval" / file_name;
}

[[nodiscard]] inline std::filesystem::path sample_eval_config_dir() {
    return std::filesystem::path{OTHELLO_SOURCE_DIR} / "data" / "eval";
}

[[nodiscard]] inline std::filesystem::path test_eval_config_fixture_path(std::string_view file_name) {
    return std::filesystem::path{OTHELLO_SOURCE_DIR} / "tests" / "fixtures" / "eval" /
           file_name;
}

[[nodiscard]] inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    REQUIRE(input.is_open());

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

inline void replace_all(std::string& text, std::string_view from, std::string_view to) {
    REQUIRE_FALSE(from.empty());
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

inline void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path};
    REQUIRE(output.is_open());
    output << text;
}

[[nodiscard]] inline std::filesystem::path unique_temp_dir(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (std::string{"vibe-othello-"} + std::string{name} + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

[[nodiscard]] inline std::string eval_config_with_pattern_table_paths(
    const std::vector<std::pair<std::string, std::string>>& table_paths) {
    std::string text = R"(schema_version=eval.v1
mode=pattern_only
name=test_pattern_table_paths
opening_max_occupied=20
midgame_max_occupied=44
)";
    for (const auto& [key, value] : table_paths) {
        text += key + "=" + value + "\n";
    }
    text += "\nopening.pattern_table=1\nmidgame.pattern_table=1\nlate.pattern_table=1\n";
    return text;
}

[[nodiscard]] inline std::string eval_config_with_pattern_table(std::string_view table_path) {
    return eval_config_with_pattern_table_paths(
        {{std::string{"pattern_table"}, std::string{table_path}}});
}

[[nodiscard]] inline std::vector<std::filesystem::path> committed_eval_config_paths() {
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path eval_dir = sample_eval_config_dir();

    for (const auto& entry : std::filesystem::directory_iterator{eval_dir}) {
        if (entry.is_regular_file() && entry.path().extension() == ".eval") {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

[[nodiscard]] inline std::vector<std::string> committed_artifact_names(
    const std::filesystem::path& directory, std::string_view extension) {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator{directory}) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

template <std::size_t Size>
[[nodiscard]] inline std::vector<std::string> sorted_names(
    const std::array<std::string_view, Size>& names) {
    std::vector<std::string> sorted;
    sorted.reserve(names.size());
    for (std::string_view name : names) {
        sorted.emplace_back(name);
    }
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

[[nodiscard]] inline Board corner_occupancy_board() {
    return Board{
        .black = Board::initial().black | othello::test::bit("a1"),
        .white = Board::initial().white | othello::test::bit("h8"),
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] inline Board corner_access_board() {
    return othello::test::board_from_text(R"(........
........
........
...BW...
...WB...
........
........
.WB.....
side=B)");
}

[[nodiscard]] inline Board x_square_danger_board() {
    return Board{
        .black = Board::initial().black | othello::test::bit("b2"),
        .white = Board::initial().white,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] inline Board extra_disc_board(Bitboard black_extra, Bitboard white_extra = 0) {
    return Board{
        .black = Board::initial().black | black_extra,
        .white = Board::initial().white | white_extra,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] inline Board pattern_disc_board(Bitboard black, Bitboard white = 0) {
    return Board{
        .black = black,
        .white = white,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] inline Board frontier_sensitive_board() {
    return othello::test::board_from_text(R"(........
........
..WWW...
..WBW...
..WWW...
........
........
........
side=B)");
}

[[nodiscard]] inline Board swapped_colors(const Board& board) noexcept {
    return Board{
        .black = board.white,
        .white = board.black,
        .side_to_move = othello::opponent(board.side_to_move),
    };
}

inline void check_breakdown_matches_basic(const Board& board, Side side) {
    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, side);

    CHECK(breakdown.total == othello::evaluate_basic(board, side));
    CHECK(breakdown.total ==
          othello::evaluate_with_config(board, side, othello::default_evaluation_config()));
}

[[nodiscard]] inline int non_terminal_score_sum(const othello::EvaluationBreakdown& breakdown) noexcept {
    return breakdown.disc_difference_score + breakdown.mobility_score +
           breakdown.corner_occupancy_score + breakdown.potential_mobility_score +
           breakdown.corner_access_score + breakdown.x_square_danger_score +
           breakdown.frontier_score + breakdown.corner_local_2x3_score +
           breakdown.corner_2x3_pattern_score +
           breakdown.edge_stability_lite_score + breakdown.edge_8_pattern_score +
           breakdown.pattern_table_score;
}

[[nodiscard]] inline othello::EvaluationConfig corner_local_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.corner_local_2x3 = weight},
        .midgame = othello::EvaluationFeatureWeights{.corner_local_2x3 = weight},
        .late = othello::EvaluationFeatureWeights{.corner_local_2x3 = weight},
    };
}

[[nodiscard]] inline othello::EvaluationConfig edge_stability_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.edge_stability_lite = weight},
        .midgame = othello::EvaluationFeatureWeights{.edge_stability_lite = weight},
        .late = othello::EvaluationFeatureWeights{.edge_stability_lite = weight},
    };
}

[[nodiscard]] inline othello::EvaluationConfig corner_pattern_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
        .midgame = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
        .late = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
    };
}

[[nodiscard]] inline othello::EvaluationConfig edge_pattern_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
        .midgame = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
        .late = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
    };
}

[[nodiscard]] inline othello::EvaluationConfig pattern_table_only_config(
    int weight, const std::shared_ptr<othello::PatternTableBundle>& tables) {
    othello::EvaluationConfig config{
        .opening = othello::EvaluationFeatureWeights{.pattern_table = weight},
        .midgame = othello::EvaluationFeatureWeights{.pattern_table = weight},
        .late = othello::EvaluationFeatureWeights{.pattern_table = weight},
    };
    config.pattern_tables = tables;
    return config;
}

[[nodiscard]] inline othello::EvaluationConfig pattern_table_only_config(int weight) {
    return pattern_table_only_config(weight, std::make_shared<othello::PatternTableBundle>());
}

[[nodiscard]] inline othello::EvaluationConfig sparse_scalar_config() noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{
            .mobility = 7,
            .corner_access = 11,
        },
        .midgame = othello::EvaluationFeatureWeights{
            .mobility = 5,
            .frontier = 3,
        },
        .late = othello::EvaluationFeatureWeights{
            .disc_difference = 2,
            .corner_occupancy = 4,
        },
    };
}

[[nodiscard]] inline othello::EvaluationConfig all_feature_guard_config() {
    auto tables = std::make_shared<othello::PatternTableBundle>();
    std::ranges::fill(tables->corner_2x3, std::int16_t{1});
    std::ranges::fill(tables->corner_3x3, std::int16_t{1});
    std::ranges::fill(tables->edge_8, std::int16_t{1});
    std::ranges::fill(tables->edge_x_10, std::int16_t{1});
    std::ranges::fill(tables->diagonal_8, std::int16_t{1});
    std::ranges::fill(tables->inner_row_8, std::int16_t{1});

    const othello::EvaluationFeatureWeights weights{
        .disc_difference = 1,
        .mobility = 2,
        .potential_mobility = 3,
        .corner_occupancy = 4,
        .corner_access = 5,
        .x_square_danger = 6,
        .frontier = 7,
        .corner_local_2x3 = 8,
        .corner_2x3_pattern = 9,
        .edge_stability_lite = 10,
        .edge_8_pattern = 11,
        .pattern_table = 12,
    };
    othello::EvaluationConfig config{
        .opening = weights,
        .midgame = weights,
        .late = weights,
    };
    config.pattern_tables = std::move(tables);
    return config;
}

[[nodiscard]] inline Board terminal_black_win_board() noexcept {
    return Board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] inline std::vector<int> corner_2x3_indexes_for_boards(
    const std::vector<Board>& boards, Side side) {
    constexpr std::array corners{
        othello::Corner2x3PatternCorner::A1,
        othello::Corner2x3PatternCorner::H1,
        othello::Corner2x3PatternCorner::A8,
        othello::Corner2x3PatternCorner::H8,
    };
    std::vector<int> indexes;
    for (const Board& board : boards) {
        for (const othello::Corner2x3PatternCorner corner : corners) {
            const int index = othello::corner_2x3_pattern_index(board, side, corner);
            if (std::ranges::find(indexes, index) == indexes.end()) {
                indexes.push_back(index);
            }
        }
    }
    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

inline void write_corner_2x3_table(const std::filesystem::path& path,
                            const std::vector<int>& indexes, int value) {
    std::ostringstream text;
    for (const int index : indexes) {
        text << "corner_2x3\t" << index << '\t' << value << '\n';
    }
    write_text_file(path, text.str());
}

inline void write_phase_table_fixture(const std::filesystem::path& temp_dir,
                               std::string_view name,
                               const std::vector<int>& indexes, int value) {
    const std::filesystem::path path = temp_dir / "patterns" / std::string{name};
    write_corner_2x3_table(path, indexes, value);
}

inline void check_scalar_weights_zero(const othello::EvaluationFeatureWeights& weights) {
    CHECK(weights.disc_difference == 0);
    CHECK(weights.mobility == 0);
    CHECK(weights.potential_mobility == 0);
    CHECK(weights.corner_occupancy == 0);
    CHECK(weights.corner_access == 0);
    CHECK(weights.x_square_danger == 0);
    CHECK(weights.frontier == 0);
    CHECK(weights.corner_local_2x3 == 0);
    CHECK(weights.corner_2x3_pattern == 0);
    CHECK(weights.edge_stability_lite == 0);
    CHECK(weights.edge_8_pattern == 0);
}

inline void check_non_terminal_breakdown_math(const Board& board, Side side) {
    REQUIRE_FALSE(othello::is_game_over(board));

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, side);

    CHECK_FALSE(breakdown.terminal);
    CHECK(breakdown.disc_difference_score ==
          breakdown.disc_difference * breakdown.disc_difference_weight);
    CHECK(breakdown.mobility_score == breakdown.mobility * breakdown.mobility_weight);
    CHECK(breakdown.corner_occupancy_score ==
          breakdown.corner_occupancy * breakdown.corner_occupancy_weight);
    CHECK(breakdown.potential_mobility_score ==
          breakdown.potential_mobility * breakdown.potential_mobility_weight);
    CHECK(breakdown.corner_access_score ==
          breakdown.corner_access * breakdown.corner_access_weight);
    CHECK(breakdown.x_square_danger_score ==
          breakdown.x_square_danger * breakdown.x_square_danger_weight);
    CHECK(breakdown.frontier_score == breakdown.frontier * breakdown.frontier_weight);
    CHECK(breakdown.corner_local_2x3_score ==
          breakdown.corner_local_2x3 * breakdown.corner_local_2x3_weight);
    CHECK(breakdown.corner_2x3_pattern_score ==
          breakdown.corner_2x3_pattern * breakdown.corner_2x3_pattern_weight);
    CHECK(breakdown.edge_stability_lite_score ==
          breakdown.edge_stability_lite * breakdown.edge_stability_lite_weight);
    CHECK(breakdown.edge_8_pattern_score ==
          breakdown.edge_8_pattern * breakdown.edge_8_pattern_weight);
    CHECK(breakdown.pattern_table_score ==
          breakdown.pattern_table * breakdown.pattern_table_weight);
    CHECK(breakdown.terminal_disc_difference == 0);
    CHECK(breakdown.terminal_score == 0);
    CHECK(breakdown.total == non_terminal_score_sum(breakdown));
}

} // namespace othello::test::evaluation
