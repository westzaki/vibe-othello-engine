#include "common/eval_config_io.hpp"
#include "common/evaluator_selection.hpp"
#include "positions/metrics.hpp"
#include "positions/search_positions.hpp"
#include "positions/tags.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using othello::Bitboard;
using othello::Board;
using othello::Side;

namespace {

[[nodiscard]] Board midgame_board() {
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

[[nodiscard]] std::filesystem::path sample_eval_config_path(std::string_view file_name) {
    return std::filesystem::path{OTHELLO_SOURCE_DIR} / "data" / "eval" / file_name;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input{path};
    REQUIRE(input.is_open());

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path};
    REQUIRE(output.is_open());
    output << text;
}

[[nodiscard]] std::filesystem::path unique_temp_dir(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (std::string{"vibe-othello-"} + std::string{name} + "-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

[[nodiscard]] std::string eval_config_with_pattern_table(std::string_view table_path) {
    std::string text = read_text_file(sample_eval_config_path("current_default.eval"));
    const std::string marker = "name=current_default\n";
    const std::size_t insert_at = text.find(marker);
    REQUIRE(insert_at != std::string::npos);
    text.insert(insert_at + marker.size(), "pattern_table=" + std::string{table_path} + "\n");
    return text;
}

[[nodiscard]] std::vector<std::filesystem::path> committed_eval_config_paths() {
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path eval_dir =
        std::filesystem::path{OTHELLO_SOURCE_DIR} / "data" / "eval";

    for (const auto& entry : std::filesystem::directory_iterator{eval_dir}) {
        if (entry.is_regular_file() && entry.path().extension() == ".eval") {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

[[nodiscard]] Board corner_occupancy_board() {
    return Board{
        .black = Board::initial().black | othello::test::bit("a1"),
        .white = Board::initial().white | othello::test::bit("h8"),
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] Board corner_access_board() {
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

[[nodiscard]] Board x_square_danger_board() {
    return Board{
        .black = Board::initial().black | othello::test::bit("b2"),
        .white = Board::initial().white,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] Board extra_disc_board(Bitboard black_extra, Bitboard white_extra = 0) {
    return Board{
        .black = Board::initial().black | black_extra,
        .white = Board::initial().white | white_extra,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] Board pattern_disc_board(Bitboard black, Bitboard white = 0) {
    return Board{
        .black = black,
        .white = white,
        .side_to_move = Side::Black,
    };
}

[[nodiscard]] Board frontier_sensitive_board() {
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

[[nodiscard]] Board swapped_colors(const Board& board) noexcept {
    return Board{
        .black = board.white,
        .white = board.black,
        .side_to_move = othello::opponent(board.side_to_move),
    };
}

void check_breakdown_matches_basic(const Board& board, Side side) {
    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, side);

    CHECK(breakdown.total == othello::evaluate_basic(board, side));
    CHECK(breakdown.total ==
          othello::evaluate_with_config(board, side, othello::default_evaluation_config()));
}

[[nodiscard]] int non_terminal_score_sum(const othello::EvaluationBreakdown& breakdown) noexcept {
    return breakdown.disc_difference_score + breakdown.mobility_score +
           breakdown.corner_occupancy_score + breakdown.potential_mobility_score +
           breakdown.corner_access_score + breakdown.x_square_danger_score +
           breakdown.frontier_score + breakdown.corner_local_2x3_score +
           breakdown.corner_2x3_pattern_score +
           breakdown.edge_stability_lite_score + breakdown.edge_8_pattern_score +
           breakdown.pattern_table_score;
}

[[nodiscard]] othello::EvaluationConfig corner_local_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.corner_local_2x3 = weight},
        .midgame = othello::EvaluationFeatureWeights{.corner_local_2x3 = weight},
        .late = othello::EvaluationFeatureWeights{.corner_local_2x3 = weight},
    };
}

[[nodiscard]] othello::EvaluationConfig edge_stability_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.edge_stability_lite = weight},
        .midgame = othello::EvaluationFeatureWeights{.edge_stability_lite = weight},
        .late = othello::EvaluationFeatureWeights{.edge_stability_lite = weight},
    };
}

[[nodiscard]] othello::EvaluationConfig corner_pattern_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
        .midgame = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
        .late = othello::EvaluationFeatureWeights{.corner_2x3_pattern = weight},
    };
}

[[nodiscard]] othello::EvaluationConfig edge_pattern_only_config(int weight) noexcept {
    return othello::EvaluationConfig{
        .opening = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
        .midgame = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
        .late = othello::EvaluationFeatureWeights{.edge_8_pattern = weight},
    };
}

[[nodiscard]] othello::EvaluationConfig pattern_table_only_config(int weight) {
    othello::EvaluationConfig config{
        .opening = othello::EvaluationFeatureWeights{.pattern_table = weight},
        .midgame = othello::EvaluationFeatureWeights{.pattern_table = weight},
        .late = othello::EvaluationFeatureWeights{.pattern_table = weight},
    };
    config.pattern_tables.enabled = true;
    return config;
}

void check_non_terminal_breakdown_math(const Board& board, Side side) {
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

} // namespace

TEST_CASE("Sample eval configs round-trip to expected evaluator configs", "[evaluation]") {
    const othello::tools::EvaluationConfigLoadResult current_default =
        othello::tools::load_evaluation_config_file(sample_eval_config_path("current_default.eval"));
    REQUIRE(current_default.ok());
    REQUIRE(current_default.name.has_value());
    CHECK(*current_default.name == "current_default");
    CHECK(current_default.config == othello::default_evaluation_config());
    CHECK_FALSE(current_default.config.pattern_tables.enabled);
    CHECK(current_default.config.opening.pattern_table == 0);
    CHECK(current_default.config.midgame.pattern_table == 0);
    CHECK(current_default.config.late.pattern_table == 0);

    const othello::tools::EvaluationConfigLoadResult phase_aware =
        othello::tools::load_evaluation_config_file(sample_eval_config_path("phase_aware_v1.eval"));
    REQUIRE(phase_aware.ok());
    REQUIRE(phase_aware.name.has_value());
    CHECK(*phase_aware.name == "phase_aware_v1");
    CHECK(phase_aware.config ==
          othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1));

    const othello::tools::EvaluationConfigLoadResult scalar_anchor =
        othello::tools::load_evaluation_config_file(
            sample_eval_config_path("classic_othello_v3_teacher_aggressive.eval"));
    REQUIRE(scalar_anchor.ok());
    REQUIRE(scalar_anchor.name.has_value());
    CHECK(*scalar_anchor.name == "classic_othello_v3_teacher_aggressive");
    CHECK(scalar_anchor.config != othello::default_evaluation_config());
    CHECK_FALSE(scalar_anchor.config.pattern_tables.enabled);

    const othello::tools::EvaluationConfigLoadResult pattern =
        othello::tools::load_evaluation_config_file(
            sample_eval_config_path("pattern_teacher_v0.eval"));
    REQUIRE(pattern.ok());
    REQUIRE(pattern.name.has_value());
    CHECK(*pattern.name == "pattern_teacher_v0");
    CHECK(pattern.pattern_table_path == "patterns/pattern_teacher_v0.tsv");
    CHECK(pattern.config.pattern_tables.enabled);
    CHECK(pattern.config.opening.pattern_table == 10);
    CHECK(pattern.config.midgame.pattern_table == 10);
    CHECK(pattern.config.late.pattern_table == 10);
    CHECK(std::ranges::count_if(pattern.config.pattern_tables.corner_2x3,
                                [](std::int16_t value) { return value != 0; }) == 64);
    CHECK(std::ranges::count_if(pattern.config.pattern_tables.edge_8,
                                [](std::int16_t value) { return value != 0; }) == 64);
}

TEST_CASE("Rejected eval experiments are pruned from the active eval surface",
          "[evaluation]") {
    constexpr std::array<std::string_view, 14> pruned_artifacts{{
        "classic_othello_v1.eval",
        "classic_othello_v2_teacher_safe.eval",
        "classic_othello_v3_late_exact.eval",
        "classic_othello_v3_teacher_rank.eval",
        "classic_pattern_v0.eval",
        "default_edge_pattern_8_soft.eval",
        "experimental_edge8_soft_frontier_stability.eval",
        "pattern_teacher_v1.eval",
        "pattern_teacher_v1_phase.eval",
        "pattern_teacher_v1_rank.eval",
        "patterns/classic_pattern_v0.tsv",
        "patterns/pattern_teacher_v1.tsv",
        "patterns/pattern_teacher_v1_phase.tsv",
        "patterns/pattern_teacher_v1_rank.tsv",
    }};

    for (std::string_view artifact : pruned_artifacts) {
        const std::string artifact_name{artifact};
        CAPTURE(artifact_name);
        CHECK_FALSE(std::filesystem::exists(sample_eval_config_path(artifact_name)));
    }
}

TEST_CASE("Committed eval config fixtures parse and preserve intended identities",
          "[evaluation]") {
    const std::vector<std::filesystem::path> paths = committed_eval_config_paths();
    REQUIRE_FALSE(paths.empty());

    const othello::EvaluationConfig default_config = othello::default_evaluation_config();
    const othello::EvaluationConfig phase_aware_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1);

    for (const std::filesystem::path& path : paths) {
        CAPTURE(path.string());

        const othello::tools::EvaluationConfigLoadResult loaded =
            othello::tools::load_evaluation_config_file(path);
        REQUIRE(loaded.ok());
        REQUIRE(loaded.name.has_value());

        const std::string file_name = path.filename().string();
        if (file_name == "current_default.eval") {
            CHECK(loaded.config == default_config);
        } else {
            CHECK(loaded.config != default_config);
        }

        if (file_name == "phase_aware_v1.eval") {
            CHECK(loaded.config == phase_aware_config);
        }
    }
}

TEST_CASE("Committed evaluation diagnostic positions parse", "[evaluation]") {
    std::string error;

    const auto positions = othello::benchmarks::load_positions_from_text(
        read_text_file(othello::benchmarks::evaluation_diagnostic_suite_path()),
        "diagnostic_suite.txt", error);

    INFO(error);
    REQUIRE(positions.has_value());
    CHECK(positions->size() == 8);
    for (const auto& position : *positions) {
        CHECK_FALSE(othello::is_game_over(position.board));
    }
}

TEST_CASE("Search benchmark exposes evaluation diagnostic positions", "[evaluation]") {
    std::string error;
    const auto file_positions = othello::benchmarks::load_positions_from_file(
        othello::benchmarks::evaluation_diagnostic_suite_path(), error);
    const auto search_positions = othello::benchmarks::make_search_evaluation_positions();

    INFO(error);
    REQUIRE(file_positions.has_value());
    REQUIRE(search_positions.has_value());
    REQUIRE(search_positions->size() == file_positions->size());
    REQUIRE(search_positions->size() == 8);

    for (std::size_t index = 0; index < search_positions->size(); ++index) {
        const auto& expected = (*file_positions)[index];
        const auto& actual = (*search_positions)[index];
        CHECK(actual.name == expected.name);
        CHECK(actual.phase == expected.phase);
        CHECK(actual.tags == expected.tags);
        CHECK(actual.notes == expected.notes);
        CHECK(actual.board_text == expected.board_text);
        CHECK(othello::benchmarks::same_board(actual.board, expected.board));
    }

    const auto& corner_access = search_positions->front();
    CHECK(corner_access.name == "eval-corner-access-a1");
    CHECK(corner_access.phase == "opening");
    CHECK(othello::benchmarks::has_tag(corner_access.tags, "corner_access"));
    CHECK(corner_access.notes == "White has tactical corner access pressure near A1.");
    CHECK(corner_access.board_text ==
          "........\n"
          ".B......\n"
          "..B.B...\n"
          "...BB...\n"
          ".WWWWW..\n"
          "..B.....\n"
          "........\n"
          "........\n"
          "side=W");

    const auto& dense_late = search_positions->back();
    CHECK(dense_late.name == "eval-late-dense-mobility");
    CHECK(dense_late.phase == "endgame-ish");
    CHECK(othello::benchmarks::has_tag(dense_late.tags, "late_pre_endgame"));
    CHECK(othello::benchmarks::has_tag(dense_late.tags, "dense_late_game"));
}

TEST_CASE("Tool evaluator selection resolves presets and config files", "[evaluation]") {
    std::string error;

    const std::optional<othello::tools::EvaluatorSelection> default_selection =
        othello::tools::parse_evaluator_selection({}, error);
    REQUIRE(default_selection.has_value());
    CHECK(default_selection->preset == othello::EvaluationPreset::Default);
    CHECK_FALSE(default_selection->config_override.has_value());
    CHECK_FALSE(othello::tools::has_custom_eval_config(*default_selection));
    CHECK(othello::tools::resolve_evaluator_selection(*default_selection) ==
          othello::default_evaluation_config());

    const std::optional<othello::tools::EvaluatorSelection> preset_selection =
        othello::tools::parse_evaluator_selection(
            {.preset_name = std::string{"phase_aware_v1"}}, error);
    REQUIRE(preset_selection.has_value());
    CHECK(preset_selection->preset == othello::EvaluationPreset::PhaseAwareV1);
    CHECK(othello::tools::resolve_evaluator_selection(*preset_selection) ==
          othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1));

    const std::string config_path =
        sample_eval_config_path("pattern_teacher_v0.eval").string();
    const std::optional<othello::tools::EvaluatorSelection> config_selection =
        othello::tools::parse_evaluator_selection({.config_path = config_path}, error);
    REQUIRE(config_selection.has_value());
    CHECK(config_selection->preset == othello::EvaluationPreset::Default);
    CHECK(config_selection->config_path == config_path);
    CHECK(othello::tools::has_custom_eval_config(*config_selection));
    CHECK(config_selection->config_override.has_value());
    CHECK(config_selection->config_override->pattern_tables.enabled);
    CHECK(config_selection->config_override->opening.pattern_table == 10);
    CHECK(othello::tools::resolve_evaluator_selection(*config_selection) ==
          *config_selection->config_override);
}

TEST_CASE("Tool evaluator selection rejects ambiguous or invalid input", "[evaluation]") {
    std::string error;

    const std::optional<othello::tools::EvaluatorSelection> both =
        othello::tools::parse_evaluator_selection(
            {.preset_name = std::string{"default"},
             .config_path = sample_eval_config_path("current_default.eval").string()},
            error);
    CHECK_FALSE(both.has_value());
    CHECK(error.find("cannot combine --eval-preset and --eval-config") != std::string::npos);

    const std::optional<othello::tools::EvaluatorSelection> unknown =
        othello::tools::parse_evaluator_selection({.preset_name = std::string{"unknown"}},
                                                  error);
    CHECK_FALSE(unknown.has_value());
    CHECK(error.find("unknown evaluation preset") != std::string::npos);

    const std::optional<othello::tools::EvaluatorSelection> missing =
        othello::tools::parse_evaluator_selection(
            {.config_path = sample_eval_config_path("missing.eval").string()}, error);
    CHECK_FALSE(missing.has_value());
    CHECK(error.find("failed to open evaluation config") != std::string::npos);
}

TEST_CASE("Eval config parser rejects malformed v1 files", "[evaluation]") {
    const auto unknown = othello::tools::parse_evaluation_config("unknown=1\n");
    CHECK_FALSE(unknown.ok());
    CHECK(unknown.error.find("unknown key") != std::string::npos);

    const auto duplicate = othello::tools::parse_evaluation_config(
        "opening.disc_difference=1\nopening.disc_difference=2\n");
    CHECK_FALSE(duplicate.ok());
    CHECK(duplicate.error.find("duplicate key") != std::string::npos);

    const auto invalid_int =
        othello::tools::parse_evaluation_config("opening.disc_difference=x\n");
    CHECK_FALSE(invalid_int.ok());
    CHECK(invalid_int.error.find("invalid integer") != std::string::npos);

    const auto missing = othello::tools::parse_evaluation_config("name=missing_keys\n");
    CHECK_FALSE(missing.ok());
    CHECK(missing.error.find("missing required key") != std::string::npos);
}

TEST_CASE("Eval config loader rejects missing and malformed pattern tables",
          "[evaluation]") {
    const std::filesystem::path temp_dir = unique_temp_dir("pattern-table-errors");

    const std::filesystem::path missing_eval = temp_dir / "missing.eval";
    write_text_file(missing_eval, eval_config_with_pattern_table("patterns/missing.tsv"));
    const othello::tools::EvaluationConfigLoadResult missing =
        othello::tools::load_evaluation_config_file(missing_eval);
    CHECK_FALSE(missing.ok());
    CHECK(missing.error.find("failed to open pattern table") != std::string::npos);

    write_text_file(temp_dir / "patterns" / "duplicate.tsv",
                    "corner_2x3\t1\t2\ncorner_2x3\t1\t3\n");
    const std::filesystem::path duplicate_eval = temp_dir / "duplicate.eval";
    write_text_file(duplicate_eval, eval_config_with_pattern_table("patterns/duplicate.tsv"));
    const othello::tools::EvaluationConfigLoadResult duplicate =
        othello::tools::load_evaluation_config_file(duplicate_eval);
    CHECK_FALSE(duplicate.ok());
    CHECK(duplicate.error.find("duplicate corner_2x3 index") != std::string::npos);

    write_text_file(temp_dir / "patterns" / "duplicate_broad.tsv",
                    "edge_x_10\t1\t2\nedge_x_10\t1\t3\n");
    const std::filesystem::path duplicate_broad_eval = temp_dir / "duplicate_broad.eval";
    write_text_file(duplicate_broad_eval,
                    eval_config_with_pattern_table("patterns/duplicate_broad.tsv"));
    const othello::tools::EvaluationConfigLoadResult duplicate_broad =
        othello::tools::load_evaluation_config_file(duplicate_broad_eval);
    CHECK_FALSE(duplicate_broad.ok());
    CHECK(duplicate_broad.error.find("duplicate edge_x_10 index") != std::string::npos);

    write_text_file(temp_dir / "patterns" / "out_of_range.tsv", "corner_3x3\t19683\t1\n");
    const std::filesystem::path out_of_range_eval = temp_dir / "out_of_range.eval";
    write_text_file(out_of_range_eval,
                    eval_config_with_pattern_table("patterns/out_of_range.tsv"));
    const othello::tools::EvaluationConfigLoadResult out_of_range =
        othello::tools::load_evaluation_config_file(out_of_range_eval);
    CHECK_FALSE(out_of_range.ok());
    CHECK(out_of_range.error.find("corner_3x3 index out of range") != std::string::npos);

    write_text_file(temp_dir / "patterns" / "malformed.tsv", "corner_2x3\t1\n");
    const std::filesystem::path malformed_eval = temp_dir / "malformed.eval";
    write_text_file(malformed_eval, eval_config_with_pattern_table("patterns/malformed.tsv"));
    const othello::tools::EvaluationConfigLoadResult malformed =
        othello::tools::load_evaluation_config_file(malformed_eval);
    CHECK_FALSE(malformed.ok());
    CHECK(malformed.error.find("expected '<family> <index> <value>'") != std::string::npos);
}

TEST_CASE("Initial board evaluation is symmetric", "[evaluation]") {
    const Board board = Board::initial();

    CHECK(othello::evaluate_disc_difference(board, Side::Black) == 0);
    CHECK(othello::evaluate_disc_difference(board, Side::White) == 0);
    CHECK(othello::evaluate_mobility(board, Side::Black) == 0);
    CHECK(othello::evaluate_mobility(board, Side::White) == 0);
    CHECK(othello::evaluate_basic(board, Side::Black) ==
          -othello::evaluate_basic(board, Side::White));
}

TEST_CASE("Default evaluation config promotes frontier corner and edge pattern weights",
          "[evaluation]") {
    const othello::EvaluationConfig config = othello::default_evaluation_config();

    CHECK(config.opening == othello::EvaluationFeatureWeights{.disc_difference = 0,
                                                              .mobility = 8,
                                                              .potential_mobility = 4,
                                                              .corner_occupancy = 35,
                                                              .corner_access = 30,
                                                              .x_square_danger = 25,
                                                              .frontier = 5,
                                                              .corner_local_2x3 = 0,
                                                              .corner_2x3_pattern = 4,
                                                              .edge_stability_lite = 2,
                                                              .edge_8_pattern = 2});
    CHECK(config.midgame == othello::EvaluationFeatureWeights{.disc_difference = 1,
                                                              .mobility = 10,
                                                              .potential_mobility = 5,
                                                              .corner_occupancy = 40,
                                                              .corner_access = 35,
                                                              .x_square_danger = 30,
                                                              .frontier = 6,
                                                              .corner_local_2x3 = 0,
                                                              .corner_2x3_pattern = 6,
                                                              .edge_stability_lite = 4,
                                                              .edge_8_pattern = 4});
    CHECK(config.late == othello::EvaluationFeatureWeights{.disc_difference = 4,
                                                           .mobility = 6,
                                                           .potential_mobility = 2,
                                                           .corner_occupancy = 45,
                                                           .corner_access = 20,
                                                           .x_square_danger = 20,
                                                           .frontier = 3,
                                                           .corner_local_2x3 = 0,
                                                           .corner_2x3_pattern = 4,
                                                           .edge_stability_lite = 8,
                                                           .edge_8_pattern = 6});
    CHECK(config.opening_max_occupied == 20);
    CHECK(config.midgame_max_occupied == 44);
    CHECK(othello::evaluation_config_for_preset(othello::EvaluationPreset::Default) == config);
    CHECK(std::string{othello::evaluation_preset_name(othello::EvaluationPreset::Default)} ==
          "default");
    const std::optional<othello::EvaluationPreset> parsed_default =
        othello::evaluation_preset_from_name("default");
    REQUIRE(parsed_default.has_value());
    CHECK(*parsed_default == othello::EvaluationPreset::Default);
    CHECK(config == othello::evaluation_config_for_preset(
                        othello::EvaluationPreset::DefaultEdgePattern8V1));
}

TEST_CASE("Phase-aware v1 preset preserves the previous default evaluator",
          "[evaluation]") {
    const othello::EvaluationConfig legacy =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1);

    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::PhaseAwareV1)} == "phase_aware_v1");
    CHECK(othello::evaluation_preset_from_name("phase_aware_v1") ==
          othello::EvaluationPreset::PhaseAwareV1);
    CHECK(othello::evaluation_preset_from_name("legacy_phase_aware_v1") ==
          othello::EvaluationPreset::PhaseAwareV1);

    CHECK(legacy.opening.frontier == 3);
    CHECK(legacy.midgame.frontier == 4);
    CHECK(legacy.late.frontier == 2);
    CHECK(legacy.opening.corner_local_2x3 == 0);
    CHECK(legacy.midgame.corner_local_2x3 == 0);
    CHECK(legacy.late.corner_local_2x3 == 0);
    CHECK(legacy.opening.corner_2x3_pattern == 0);
    CHECK(legacy.midgame.corner_2x3_pattern == 0);
    CHECK(legacy.late.corner_2x3_pattern == 0);
    CHECK(legacy.opening.edge_stability_lite == 0);
    CHECK(legacy.midgame.edge_stability_lite == 0);
    CHECK(legacy.late.edge_stability_lite == 0);
    CHECK(legacy.opening.edge_8_pattern == 0);
    CHECK(legacy.midgame.edge_8_pattern == 0);
    CHECK(legacy.late.edge_8_pattern == 0);
}

TEST_CASE("Smoke evaluation preset is explicit and lightweight", "[evaluation]") {
    const othello::EvaluationConfig legacy_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1);
    const othello::EvaluationConfig smoke_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::MobilityPlusSmoke);

    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::MobilityPlusSmoke)} == "mobility_plus_smoke");
    const std::optional<othello::EvaluationPreset> parsed_smoke =
        othello::evaluation_preset_from_name("mobility_plus_smoke");
    REQUIRE(parsed_smoke.has_value());
    CHECK(*parsed_smoke == othello::EvaluationPreset::MobilityPlusSmoke);
    CHECK_FALSE(othello::evaluation_preset_from_name("unknown").has_value());
    CHECK(smoke_config.opening.mobility == legacy_config.opening.mobility + 2);
    CHECK(smoke_config.midgame.mobility == legacy_config.midgame.mobility + 2);
    CHECK(smoke_config.late.mobility == legacy_config.late.mobility + 2);
    CHECK(smoke_config.opening.potential_mobility ==
          legacy_config.opening.potential_mobility);
    CHECK(smoke_config.opening.corner_2x3_pattern == 0);
    CHECK(smoke_config.opening.edge_stability_lite == 0);
    CHECK(smoke_config.opening.edge_8_pattern == 0);
}

TEST_CASE("Frontier refinement preset keeps its previous explicit semantics", "[evaluation]") {
    const othello::EvaluationConfig legacy_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1);
    const othello::EvaluationConfig frontier_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::FrontierOpen2Mid2LatePlus1);

    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::FrontierOpen2Mid2LatePlus1)} ==
          "frontier_open2_mid2_late_plus1");
    const std::optional<othello::EvaluationPreset> parsed =
        othello::evaluation_preset_from_name("frontier_open2_mid2_late_plus1");
    REQUIRE(parsed.has_value());
    CHECK(*parsed == othello::EvaluationPreset::FrontierOpen2Mid2LatePlus1);

    CHECK(frontier_config.opening.frontier == legacy_config.opening.frontier + 2);
    CHECK(frontier_config.midgame.frontier == legacy_config.midgame.frontier + 2);
    CHECK(frontier_config.late.frontier == legacy_config.late.frontier + 1);
    CHECK(frontier_config.opening.corner_2x3_pattern == 0);
    CHECK(frontier_config.opening.edge_stability_lite == 0);
    CHECK(frontier_config.opening.edge_8_pattern == 0);
}

TEST_CASE("Classic lite presets are explicit and keep default separate", "[evaluation]") {
    const othello::EvaluationConfig legacy_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1);
    const othello::EvaluationConfig corner_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::ClassicCornerLiteV1);
    const othello::EvaluationConfig edge_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::ClassicEdgeLiteV1);
    const othello::EvaluationConfig features_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::ClassicFeaturesLiteV1);
    const othello::EvaluationConfig aggressive_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::ClassicFeaturesLiteAggressive);
    const othello::EvaluationConfig frontier_classic_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::FrontierClassicFeaturesLiteV1);

    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::ClassicCornerLiteV1)} == "classic_corner_lite_v1");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::ClassicEdgeLiteV1)} == "classic_edge_lite_v1");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::ClassicFeaturesLiteV1)} == "classic_features_lite_v1");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::ClassicFeaturesLiteAggressive)} ==
          "classic_features_lite_aggressive");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::FrontierClassicFeaturesLiteV1)} ==
          "frontier_classic_features_lite_v1");

    CHECK(othello::evaluation_preset_from_name("classic_corner_lite_v1") ==
          othello::EvaluationPreset::ClassicCornerLiteV1);
    CHECK(othello::evaluation_preset_from_name("classic_edge_lite_v1") ==
          othello::EvaluationPreset::ClassicEdgeLiteV1);
    CHECK(othello::evaluation_preset_from_name("classic_features_lite_v1") ==
          othello::EvaluationPreset::ClassicFeaturesLiteV1);
    CHECK(othello::evaluation_preset_from_name("classic_features_lite_aggressive") ==
          othello::EvaluationPreset::ClassicFeaturesLiteAggressive);
    CHECK(othello::evaluation_preset_from_name("frontier_classic_features_lite_v1") ==
          othello::EvaluationPreset::FrontierClassicFeaturesLiteV1);

    CHECK(legacy_config.opening.corner_local_2x3 == 0);
    CHECK(legacy_config.midgame.corner_local_2x3 == 0);
    CHECK(legacy_config.late.corner_local_2x3 == 0);
    CHECK(legacy_config.opening.corner_2x3_pattern == 0);
    CHECK(legacy_config.midgame.corner_2x3_pattern == 0);
    CHECK(legacy_config.late.corner_2x3_pattern == 0);
    CHECK(legacy_config.opening.edge_stability_lite == 0);
    CHECK(legacy_config.midgame.edge_stability_lite == 0);
    CHECK(legacy_config.late.edge_stability_lite == 0);
    CHECK(legacy_config.opening.edge_8_pattern == 0);
    CHECK(legacy_config.midgame.edge_8_pattern == 0);
    CHECK(legacy_config.late.edge_8_pattern == 0);

    CHECK(corner_config.opening.corner_local_2x3 == 8);
    CHECK(corner_config.midgame.corner_local_2x3 == 10);
    CHECK(corner_config.late.corner_local_2x3 == 6);
    CHECK(corner_config.opening.edge_stability_lite == 0);

    CHECK(edge_config.opening.edge_stability_lite == 2);
    CHECK(edge_config.midgame.edge_stability_lite == 4);
    CHECK(edge_config.late.edge_stability_lite == 8);
    CHECK(edge_config.opening.corner_local_2x3 == 0);

    CHECK(features_config.opening.corner_local_2x3 == 8);
    CHECK(features_config.midgame.edge_stability_lite == 4);
    CHECK(features_config.opening.corner_2x3_pattern == 0);
    CHECK(features_config.opening.edge_8_pattern == 0);
    CHECK(aggressive_config.opening.corner_local_2x3 == 14);
    CHECK(aggressive_config.midgame.corner_local_2x3 == 18);
    CHECK(aggressive_config.late.edge_stability_lite == 12);
    CHECK(frontier_classic_config.opening.frontier == legacy_config.opening.frontier + 2);
    CHECK(frontier_classic_config.midgame.frontier == legacy_config.midgame.frontier + 2);
    CHECK(frontier_classic_config.late.frontier == legacy_config.late.frontier + 1);
    CHECK(othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1) ==
          legacy_config);
}

TEST_CASE("Corner pattern presets are explicit and promoted default is named",
          "[evaluation]") {
    const othello::EvaluationConfig default_config = othello::default_evaluation_config();
    const othello::EvaluationConfig legacy_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1);
    const othello::EvaluationConfig pattern_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::CornerPattern2x3V1);
    const othello::EvaluationConfig aggressive_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::CornerPattern2x3Aggressive);
    const othello::EvaluationConfig frontier_pattern_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::FrontierCornerPattern2x3V1);
    const othello::EvaluationConfig frontier_pattern_edge_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::FrontierCornerPatternEdgeLiteV1);

    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::CornerPattern2x3V1)} == "corner_pattern_2x3_v1");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::CornerPattern2x3Aggressive)} ==
          "corner_pattern_2x3_aggressive");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::FrontierCornerPattern2x3V1)} ==
          "frontier_corner_pattern_2x3_v1");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::FrontierCornerPatternEdgeLiteV1)} ==
          "frontier_corner_pattern_edge_lite_v1");

    CHECK(othello::evaluation_preset_from_name("corner_pattern_2x3_v1") ==
          othello::EvaluationPreset::CornerPattern2x3V1);
    CHECK(othello::evaluation_preset_from_name("corner_pattern_2x3_aggressive") ==
          othello::EvaluationPreset::CornerPattern2x3Aggressive);
    CHECK(othello::evaluation_preset_from_name("frontier_corner_pattern_2x3_v1") ==
          othello::EvaluationPreset::FrontierCornerPattern2x3V1);
    CHECK(othello::evaluation_preset_from_name("frontier_corner_pattern_edge_lite_v1") ==
          othello::EvaluationPreset::FrontierCornerPatternEdgeLiteV1);

    CHECK(legacy_config.opening.corner_2x3_pattern == 0);
    CHECK(pattern_config.opening.corner_2x3_pattern == 4);
    CHECK(pattern_config.midgame.corner_2x3_pattern == 6);
    CHECK(pattern_config.late.corner_2x3_pattern == 4);
    CHECK(pattern_config.opening.corner_local_2x3 == 0);
    CHECK(pattern_config.opening.frontier == legacy_config.opening.frontier);
    CHECK(pattern_config.opening.edge_stability_lite == 0);
    CHECK(aggressive_config.opening.corner_2x3_pattern == 8);
    CHECK(aggressive_config.midgame.corner_2x3_pattern == 10);
    CHECK(aggressive_config.late.corner_2x3_pattern == 6);
    CHECK(frontier_pattern_config.opening.frontier == legacy_config.opening.frontier + 2);
    CHECK(frontier_pattern_config.midgame.frontier == legacy_config.midgame.frontier + 2);
    CHECK(frontier_pattern_config.late.frontier == legacy_config.late.frontier + 1);
    CHECK(frontier_pattern_config.opening.corner_2x3_pattern == 4);
    CHECK(frontier_pattern_config.opening.corner_local_2x3 == 0);
    CHECK(frontier_pattern_edge_config.opening.edge_stability_lite == 2);
    CHECK(frontier_pattern_edge_config.midgame.edge_stability_lite == 4);
    CHECK(frontier_pattern_edge_config.late.edge_stability_lite == 8);
    CHECK(frontier_pattern_edge_config.opening.edge_8_pattern == 0);
    CHECK(frontier_pattern_edge_config.midgame.edge_8_pattern == 0);
    CHECK(frontier_pattern_edge_config.late.edge_8_pattern == 0);
    CHECK(othello::default_evaluation_config() == default_config);
    CHECK(default_config.opening.edge_8_pattern == 2);
    CHECK(default_config.midgame.edge_8_pattern == 4);
    CHECK(default_config.late.edge_8_pattern == 6);
}

TEST_CASE("Edge 8 pattern presets are explicit and promoted default is named",
          "[evaluation]") {
    const othello::EvaluationConfig default_config = othello::default_evaluation_config();
    const othello::EvaluationConfig legacy_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::PhaseAwareV1);
    const othello::EvaluationConfig edge_config =
        othello::evaluation_config_for_preset(othello::EvaluationPreset::EdgePattern8V1);
    const othello::EvaluationConfig edge_aggressive_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::EdgePattern8Aggressive);
    const othello::EvaluationConfig default_edge_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::DefaultEdgePattern8V1);
    const othello::EvaluationConfig default_no_edge_lite_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::DefaultEdgePattern8NoEdgeLite);
    const othello::EvaluationConfig default_edge_aggressive_config =
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::DefaultEdgePattern8Aggressive);

    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::EdgePattern8V1)} == "edge_pattern_8_v1");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::EdgePattern8Aggressive)} ==
          "edge_pattern_8_aggressive");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::DefaultEdgePattern8V1)} ==
          "default_edge_pattern_8_v1");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::DefaultEdgePattern8NoEdgeLite)} ==
          "default_edge_pattern_8_no_edge_lite");
    CHECK(std::string{othello::evaluation_preset_name(
              othello::EvaluationPreset::DefaultEdgePattern8Aggressive)} ==
          "default_edge_pattern_8_aggressive");

    CHECK(othello::evaluation_preset_from_name("edge_pattern_8_v1") ==
          othello::EvaluationPreset::EdgePattern8V1);
    CHECK(othello::evaluation_preset_from_name("edge_pattern_8_aggressive") ==
          othello::EvaluationPreset::EdgePattern8Aggressive);
    CHECK(othello::evaluation_preset_from_name("default_edge_pattern_8_v1") ==
          othello::EvaluationPreset::DefaultEdgePattern8V1);
    CHECK(othello::evaluation_preset_from_name("default_edge_pattern_8_no_edge_lite") ==
          othello::EvaluationPreset::DefaultEdgePattern8NoEdgeLite);
    CHECK(othello::evaluation_preset_from_name("default_edge_pattern_8_aggressive") ==
          othello::EvaluationPreset::DefaultEdgePattern8Aggressive);

    CHECK(legacy_config.opening.edge_8_pattern == 0);
    CHECK(default_config.opening.edge_8_pattern == 2);
    CHECK(default_config.midgame.edge_8_pattern == 4);
    CHECK(default_config.late.edge_8_pattern == 6);

    CHECK(edge_config.opening.edge_8_pattern == 2);
    CHECK(edge_config.midgame.edge_8_pattern == 4);
    CHECK(edge_config.late.edge_8_pattern == 6);
    CHECK(edge_config.opening.edge_stability_lite == 0);
    CHECK(edge_config.opening.corner_2x3_pattern == 0);
    CHECK(edge_config.opening.frontier == legacy_config.opening.frontier);

    CHECK(edge_aggressive_config.opening.edge_8_pattern == 4);
    CHECK(edge_aggressive_config.midgame.edge_8_pattern == 8);
    CHECK(edge_aggressive_config.late.edge_8_pattern == 10);
    CHECK(edge_aggressive_config.opening.edge_stability_lite == 0);

    CHECK(default_edge_config.opening.corner_2x3_pattern ==
          default_config.opening.corner_2x3_pattern);
    CHECK(default_edge_config.opening.edge_stability_lite ==
          default_config.opening.edge_stability_lite);
    CHECK(default_edge_config.opening.edge_8_pattern == 2);
    CHECK(default_edge_config.midgame.edge_8_pattern == 4);
    CHECK(default_edge_config.late.edge_8_pattern == 6);
    CHECK(default_edge_config == default_config);

    CHECK(default_no_edge_lite_config.opening.corner_2x3_pattern ==
          default_config.opening.corner_2x3_pattern);
    CHECK(default_no_edge_lite_config.opening.edge_stability_lite == 0);
    CHECK(default_no_edge_lite_config.midgame.edge_stability_lite == 0);
    CHECK(default_no_edge_lite_config.late.edge_stability_lite == 0);
    CHECK(default_no_edge_lite_config.opening.edge_8_pattern == 2);

    CHECK(default_edge_aggressive_config.opening.edge_8_pattern == 4);
    CHECK(default_edge_aggressive_config.midgame.edge_8_pattern == 8);
    CHECK(default_edge_aggressive_config.late.edge_8_pattern == 10);
    CHECK(othello::default_evaluation_config() == default_config);
}

TEST_CASE("Corner ownership improves the owning side evaluation", "[evaluation]") {
    const Board board{
        .black = Board::initial().black | othello::test::bit("a1"),
        .white = Board::initial().white,
        .side_to_move = Side::Black,
    };

    CHECK(othello::evaluate_basic(board, Side::Black) > 0);
    CHECK(othello::evaluate_basic(board, Side::White) < 0);
    CHECK(othello::evaluate_basic(board, Side::Black) >
          othello::evaluate_basic(board, Side::White));
}

TEST_CASE("Evaluation breakdown total matches basic evaluator", "[evaluation]") {
    const Board terminal{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    for (const Board& board :
         {Board::initial(), midgame_board(), corner_occupancy_board(), corner_access_board(),
          x_square_danger_board(), frontier_sensitive_board(), terminal}) {
        check_breakdown_matches_basic(board, Side::Black);
        check_breakdown_matches_basic(board, Side::White);
    }
}

TEST_CASE("Default config matches compatibility evaluator", "[evaluation]") {
    const Board terminal{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    for (const Board& board :
         {Board::initial(), midgame_board(), corner_occupancy_board(), corner_access_board(),
          x_square_danger_board(), frontier_sensitive_board(), terminal}) {
        for (const Side side : {Side::Black, Side::White}) {
            CAPTURE(othello::to_string(board));
            CHECK(othello::evaluate_with_config(board, side,
                                                othello::default_evaluation_config()) ==
                  othello::evaluate_basic(board, side));
            const othello::EvaluationBreakdown configured = othello::evaluate_basic_breakdown(
                board, side, othello::default_evaluation_config());
            const othello::EvaluationBreakdown compatibility =
                othello::evaluate_basic_breakdown(board, side);
            CHECK(configured.phase == compatibility.phase);
            CHECK(configured.disc_difference_weight == compatibility.disc_difference_weight);
            CHECK(configured.mobility_weight == compatibility.mobility_weight);
            CHECK(configured.corner_occupancy_weight == compatibility.corner_occupancy_weight);
            CHECK(configured.potential_mobility_weight ==
                  compatibility.potential_mobility_weight);
            CHECK(configured.corner_access_weight == compatibility.corner_access_weight);
            CHECK(configured.x_square_danger_weight == compatibility.x_square_danger_weight);
            CHECK(configured.frontier_weight == compatibility.frontier_weight);
            CHECK(configured.corner_local_2x3_weight == compatibility.corner_local_2x3_weight);
            CHECK(configured.corner_2x3_pattern_weight ==
                  compatibility.corner_2x3_pattern_weight);
            CHECK(configured.edge_stability_lite_weight ==
                  compatibility.edge_stability_lite_weight);
            CHECK(configured.edge_8_pattern_weight == compatibility.edge_8_pattern_weight);
            CHECK(configured.total == compatibility.total);
        }
    }
}

TEST_CASE("Non-terminal evaluation breakdown explains component math", "[evaluation]") {
    for (const Board& board : {Board::initial(), midgame_board(), corner_occupancy_board(),
                               corner_access_board(), x_square_danger_board(),
                               frontier_sensitive_board()}) {
        check_non_terminal_breakdown_math(board, Side::Black);
        check_non_terminal_breakdown_math(board, Side::White);
    }
}

TEST_CASE("Evaluation breakdown reports phase and board fill", "[evaluation]") {
    const Board initial = Board::initial();
    const Board midgame = midgame_board();
    const Board late = othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
WWWWWWWW
WWWWWWWW
WWWWWW..
........
side=B)");

    const othello::EvaluationBreakdown initial_breakdown =
        othello::evaluate_basic_breakdown(initial, Side::Black);
    const othello::EvaluationBreakdown midgame_breakdown =
        othello::evaluate_basic_breakdown(midgame, Side::Black);
    const othello::EvaluationBreakdown late_breakdown =
        othello::evaluate_basic_breakdown(late, Side::Black);

    CHECK(initial_breakdown.phase == othello::EvaluationPhase::Opening);
    CHECK(initial_breakdown.occupied_count == 4);
    CHECK(initial_breakdown.empty_count == 60);
    CHECK(initial_breakdown.disc_difference_weight == 0);

    CHECK(midgame_breakdown.phase == othello::EvaluationPhase::Opening);
    CHECK(midgame_breakdown.occupied_count == 11);

    CHECK(late_breakdown.phase == othello::EvaluationPhase::Late);
    CHECK(late_breakdown.occupied_count == 54);
    CHECK(late_breakdown.empty_count == 10);
    CHECK(late_breakdown.disc_difference_weight == 4);
}

TEST_CASE("Corner occupancy appears in evaluation breakdown", "[evaluation]") {
    const Board board{
        .black = Board::initial().black | othello::test::bit("a1"),
        .white = Board::initial().white,
        .side_to_move = Side::Black,
    };

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    REQUIRE_FALSE(black.terminal);
    CHECK(black.corner_occupancy == 1);
    CHECK(black.corner_occupancy_weight == 35);
    CHECK(black.corner_occupancy_score == 35);
    CHECK(white.corner_occupancy == -1);
    CHECK(white.corner_occupancy_score == -35);
}

TEST_CASE("Legal corner access is positive for the side with a corner move", "[evaluation]") {
    const Board board = corner_access_board();

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK((othello::legal_moves(board) & othello::test::bit("a1")) != 0);
    CHECK(black.corner_access > 0);
    CHECK(black.corner_access_score > 0);
    CHECK(white.corner_access < 0);
    CHECK(white.corner_access_score < 0);
}

TEST_CASE("X-square next to an empty corner is dangerous", "[evaluation]") {
    const Board board = x_square_danger_board();

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK(black.x_square_danger == -1);
    CHECK(black.x_square_danger_score < 0);
    CHECK(white.x_square_danger == 1);
    CHECK(white.x_square_danger_score > 0);
}

TEST_CASE("Lower own frontier is better", "[evaluation]") {
    const Board board = frontier_sensitive_board();

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK(black.frontier > 0);
    CHECK(black.frontier_score > 0);
    CHECK(white.frontier < 0);
    CHECK(white.frontier_score < 0);
}

TEST_CASE("Corner-local 2x3 lite penalizes empty-corner X and C squares", "[evaluation]") {
    const othello::EvaluationConfig config = corner_local_only_config(10);
    const Board own_x = extra_disc_board(othello::test::bit("b2"));
    const Board opponent_x = extra_disc_board(0, othello::test::bit("b2"));
    const Board own_c = extra_disc_board(othello::test::bit("b1"));

    const othello::EvaluationBreakdown own_x_breakdown =
        othello::evaluate_basic_breakdown(own_x, Side::Black, config);
    const othello::EvaluationBreakdown opponent_x_breakdown =
        othello::evaluate_basic_breakdown(opponent_x, Side::Black, config);
    const othello::EvaluationBreakdown own_c_breakdown =
        othello::evaluate_basic_breakdown(own_c, Side::Black, config);

    CHECK(own_x_breakdown.corner_local_2x3 == -2);
    CHECK(own_x_breakdown.corner_local_2x3_weight == 10);
    CHECK(own_x_breakdown.corner_local_2x3_score == -20);
    CHECK(opponent_x_breakdown.corner_local_2x3 == 2);
    CHECK(opponent_x_breakdown.corner_local_2x3_score == 20);
    CHECK(own_c_breakdown.corner_local_2x3 == -1);
    CHECK(own_c_breakdown.corner_local_2x3_score == -10);
}

TEST_CASE("Corner-local 2x3 lite is symmetric across corners and colors", "[evaluation]") {
    const othello::EvaluationConfig config = corner_local_only_config(10);

    for (const std::string_view square : {"b2", "g2", "b7", "g7"}) {
        const Board board = extra_disc_board(othello::test::bit(square));
        const othello::EvaluationBreakdown black =
            othello::evaluate_basic_breakdown(board, Side::Black, config);
        const othello::EvaluationBreakdown white =
            othello::evaluate_basic_breakdown(board, Side::White, config);
        CHECK(black.corner_local_2x3 == -2);
        CHECK(white.corner_local_2x3 == 2);
    }

    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1"));
    const Board swapped = swapped_colors(board);
    CHECK(othello::evaluate_with_config(board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped, Side::Black, config));
}

TEST_CASE("Corner-local 2x3 lite rewards owned-corner adjacent support", "[evaluation]") {
    const othello::EvaluationConfig config = corner_local_only_config(10);
    const Board owned =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));
    const Board opponent_owned =
        extra_disc_board(0, othello::test::bit("a1") | othello::test::bit("b1") |
                                othello::test::bit("a2"));

    const othello::EvaluationBreakdown owned_breakdown =
        othello::evaluate_basic_breakdown(owned, Side::Black, config);
    const othello::EvaluationBreakdown opponent_owned_breakdown =
        othello::evaluate_basic_breakdown(opponent_owned, Side::Black, config);

    CHECK(owned_breakdown.corner_local_2x3 == 2);
    CHECK(owned_breakdown.corner_local_2x3_score == 20);
    CHECK(opponent_owned_breakdown.corner_local_2x3 == -2);
    CHECK(opponent_owned_breakdown.corner_local_2x3_score == -20);
}

TEST_CASE("Corner 2x3 pattern index is deterministic across symmetric corners",
          "[evaluation]") {
    using Corner = othello::Corner2x3PatternCorner;

    CHECK(othello::corner_2x3_pattern_table_size == 729);
    CHECK(othello::corner_2x3_pattern_table_value(0) == 0);
    CHECK(othello::corner_2x3_pattern_table_value(-1) == 0);
    CHECK(othello::corner_2x3_pattern_table_value(729) == 0);
    CHECK(othello::corner_2x3_pattern_index(Board::initial(), Side::Black, Corner::A1) == 0);

    const Board mixed = extra_disc_board(othello::test::bit("a1") |
                                             othello::test::bit("a2"),
                                         othello::test::bit("b1") |
                                             othello::test::bit("b2"));
    CHECK(othello::corner_2x3_pattern_index(mixed, Side::Black, Corner::A1) == 196);
    CHECK(othello::corner_2x3_pattern_index(mixed, Side::Black, Corner::A1) >= 0);
    CHECK(othello::corner_2x3_pattern_index(mixed, Side::Black, Corner::A1) < 729);

    for (const auto [corner, corner_square, c_square, x_square] : {
             std::tuple{Corner::A1, "a1", "b1", "b2"},
             std::tuple{Corner::H1, "h1", "g1", "g2"},
             std::tuple{Corner::A8, "a8", "b8", "b7"},
             std::tuple{Corner::H8, "h8", "g8", "g7"},
         }) {
        const Board board = extra_disc_board(othello::test::bit(corner_square) |
                                             othello::test::bit(c_square) |
                                             othello::test::bit(x_square));
        CHECK(othello::corner_2x3_pattern_index(board, Side::Black, corner) == 85);
    }
}

TEST_CASE("Corner 2x3 pattern table follows conservative corner-local rules",
          "[evaluation]") {
    const othello::EvaluationConfig config = corner_pattern_only_config(10);
    const Board own_x = extra_disc_board(othello::test::bit("b2"));
    const Board opponent_x = extra_disc_board(0, othello::test::bit("b2"));
    const Board own_c = extra_disc_board(othello::test::bit("b1"));
    const Board opponent_c = extra_disc_board(0, othello::test::bit("b1"));
    const Board owned_support =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));
    const Board opponent_owned_support =
        extra_disc_board(0, othello::test::bit("a1") | othello::test::bit("b1") |
                                othello::test::bit("a2"));

    CHECK(othello::corner_2x3_pattern_score(Board::initial(), Side::Black) == 0);
    CHECK(othello::corner_2x3_pattern_score(own_x, Side::Black) == -3);
    CHECK(othello::corner_2x3_pattern_score(opponent_x, Side::Black) == 3);
    CHECK(othello::corner_2x3_pattern_score(own_c, Side::Black) == -1);
    CHECK(othello::corner_2x3_pattern_score(opponent_c, Side::Black) == 1);
    CHECK(othello::corner_2x3_pattern_score(owned_support, Side::Black) == 6);
    CHECK(othello::corner_2x3_pattern_score(opponent_owned_support, Side::Black) == -6);

    const othello::EvaluationBreakdown own_x_breakdown =
        othello::evaluate_basic_breakdown(own_x, Side::Black, config);
    const othello::EvaluationBreakdown opponent_x_breakdown =
        othello::evaluate_basic_breakdown(opponent_x, Side::Black, config);
    const othello::EvaluationBreakdown own_c_breakdown =
        othello::evaluate_basic_breakdown(own_c, Side::Black, config);
    const othello::EvaluationBreakdown opponent_c_breakdown =
        othello::evaluate_basic_breakdown(opponent_c, Side::Black, config);

    CHECK(own_x_breakdown.corner_2x3_pattern == -3);
    CHECK(own_x_breakdown.corner_2x3_pattern_weight == 10);
    CHECK(own_x_breakdown.corner_2x3_pattern_score == -30);
    CHECK(opponent_x_breakdown.corner_2x3_pattern == 3);
    CHECK(opponent_x_breakdown.corner_2x3_pattern_score == 30);
    CHECK(own_c_breakdown.corner_2x3_pattern == -1);
    CHECK(own_c_breakdown.corner_2x3_pattern_score == -10);
    CHECK(opponent_c_breakdown.corner_2x3_pattern == 1);
    CHECK(opponent_c_breakdown.corner_2x3_pattern_score == 10);
}

TEST_CASE("Corner 2x3 pattern is symmetric across colors and contributes to total",
          "[evaluation]") {
    const othello::EvaluationConfig config = corner_pattern_only_config(5);
    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                             othello::test::bit("b2"),
                         othello::test::bit("a2"));
    const Board swapped = swapped_colors(board);

    CHECK(othello::corner_2x3_pattern_score(board, Side::Black) ==
          -othello::corner_2x3_pattern_score(swapped, Side::Black));
    CHECK(othello::evaluate_with_config(board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped, Side::Black, config));

    const Board supported =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));
    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(supported, Side::Black, config);

    CHECK(breakdown.corner_2x3_pattern == 6);
    CHECK(breakdown.corner_2x3_pattern_weight == 5);
    CHECK(breakdown.corner_2x3_pattern_score == 30);
    CHECK(breakdown.total == non_terminal_score_sum(breakdown));
    CHECK(breakdown.total == 30);
}

TEST_CASE("Edge stability lite counts only corner-anchored continuous edge discs",
          "[evaluation]") {
    const othello::EvaluationConfig config = edge_stability_only_config(3);
    const Board anchored =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("c1") | othello::test::bit("a2"));
    const Board unanchored =
        extra_disc_board(othello::test::bit("b1") | othello::test::bit("c1"));
    const Board empty_stop =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("c1"));
    const Board opponent_stop =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("c1"),
                         othello::test::bit("b1"));

    const othello::EvaluationBreakdown anchored_breakdown =
        othello::evaluate_basic_breakdown(anchored, Side::Black, config);
    CHECK(anchored_breakdown.edge_stability_lite == 3);
    CHECK(anchored_breakdown.edge_stability_lite_weight == 3);
    CHECK(anchored_breakdown.edge_stability_lite_score == 9);

    CHECK(othello::evaluate_basic_breakdown(unanchored, Side::Black, config)
              .edge_stability_lite == 0);
    CHECK(othello::evaluate_basic_breakdown(empty_stop, Side::Black, config)
              .edge_stability_lite == 0);
    CHECK(othello::evaluate_basic_breakdown(opponent_stop, Side::Black, config)
              .edge_stability_lite == 0);
}

TEST_CASE("Edge stability lite is symmetric across corners and colors", "[evaluation]") {
    const othello::EvaluationConfig config = edge_stability_only_config(3);

    for (const auto [corner, near, far] : {
             std::tuple{"a1", "b1", "c1"},
             std::tuple{"h1", "g1", "f1"},
             std::tuple{"a8", "b8", "c8"},
             std::tuple{"h8", "g8", "f8"},
         }) {
        const Board board =
            extra_disc_board(othello::test::bit(corner) | othello::test::bit(near) |
                             othello::test::bit(far));
        const othello::EvaluationBreakdown black =
            othello::evaluate_basic_breakdown(board, Side::Black, config);
        CHECK(black.edge_stability_lite == 2);
        CHECK(black.edge_stability_lite_score == 6);
    }

    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1"));
    const Board swapped = swapped_colors(board);
    CHECK(othello::evaluate_with_config(board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped, Side::Black, config));
}

TEST_CASE("Edge stability lite does not double-count full edges from both corners",
          "[evaluation]") {
    const othello::EvaluationConfig config = edge_stability_only_config(3);
    const Board full_top_edge = extra_disc_board(
        othello::test::bit("a1") | othello::test::bit("b1") | othello::test::bit("c1") |
        othello::test::bit("d1") | othello::test::bit("e1") | othello::test::bit("f1") |
        othello::test::bit("g1") | othello::test::bit("h1"));

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(full_top_edge, Side::Black, config);

    CHECK(breakdown.edge_stability_lite == 8);
    CHECK(breakdown.edge_stability_lite_score == 24);
}

TEST_CASE("Edge 8 pattern index is deterministic across edges", "[evaluation]") {
    using Edge = othello::Edge8PatternEdge;

    CHECK(othello::edge_8_pattern_table_size == 6561);
    CHECK(othello::edge_8_pattern_table_value(0) == 0);
    CHECK(othello::edge_8_pattern_table_value(-1) == 0);
    CHECK(othello::edge_8_pattern_table_value(6561) == 0);
    CHECK(othello::edge_8_pattern_index(Board::initial(), Side::Black, Edge::Top) == 0);

    const Board mixed =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("c1"),
                         othello::test::bit("b1"));
    CHECK(othello::edge_8_pattern_index(mixed, Side::Black, Edge::Top) == 16);
    CHECK(othello::edge_8_pattern_index(mixed, Side::Black, Edge::Top) >= 0);
    CHECK(othello::edge_8_pattern_index(mixed, Side::Black, Edge::Top) < 6561);

    for (const auto [edge, first, second, third] : {
             std::tuple{Edge::Top, "a1", "b1", "c1"},
             std::tuple{Edge::Bottom, "a8", "b8", "c8"},
             std::tuple{Edge::Left, "a1", "a2", "a3"},
             std::tuple{Edge::Right, "h1", "h2", "h3"},
         }) {
        const Board board =
            extra_disc_board(othello::test::bit(first) | othello::test::bit(second),
                             othello::test::bit(third));
        CHECK(othello::edge_8_pattern_index(board, Side::Black, edge) == 22);
    }
}

TEST_CASE("Classic broad pattern indexes are deterministic across symmetric lines",
          "[evaluation]") {
    using Corner3 = othello::Corner3x3PatternCorner;
    using Diagonal = othello::Diagonal8PatternDiagonal;
    using EdgeX = othello::EdgeX10PatternEdge;
    using Inner = othello::InnerRow8PatternLine;

    CHECK(othello::corner_3x3_pattern_table_size == 19683);
    CHECK(othello::edge_x_10_pattern_table_size == 59049);
    CHECK(othello::diagonal_8_pattern_table_size == 6561);
    CHECK(othello::inner_row_8_pattern_table_size == 6561);
    CHECK(othello::corner_3x3_pattern_index(pattern_disc_board(0), Side::Black,
                                            Corner3::A1) == 0);
    CHECK(othello::edge_x_10_pattern_index(pattern_disc_board(0), Side::Black,
                                           EdgeX::Top) == 0);
    CHECK(othello::diagonal_8_pattern_index(pattern_disc_board(0), Side::Black,
                                            Diagonal::A1H8) == 0);
    CHECK(othello::inner_row_8_pattern_index(pattern_disc_board(0), Side::Black,
                                             Inner::Top) == 0);

    const Board corner_mixed =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c1") |
                               othello::test::bit("b2"),
                           othello::test::bit("b1") | othello::test::bit("a2") |
                               othello::test::bit("c3"));
    CHECK(othello::corner_3x3_pattern_index(corner_mixed, Side::Black,
                                            Corner3::A1) == 13273);

    for (const auto [corner, corner_square, c_square, x_square] : {
             std::tuple{Corner3::A1, "a1", "b1", "b2"},
             std::tuple{Corner3::H1, "h1", "g1", "g2"},
             std::tuple{Corner3::A8, "a8", "b8", "b7"},
             std::tuple{Corner3::H8, "h8", "g8", "g7"},
         }) {
        const Board board = pattern_disc_board(othello::test::bit(corner_square) |
                                               othello::test::bit(c_square) |
                                               othello::test::bit(x_square));
        CHECK(othello::corner_3x3_pattern_index(board, Side::Black, corner) == 85);
    }

    const Board edge_context =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c1") |
                               othello::test::bit("b2"),
                           othello::test::bit("b1") | othello::test::bit("g2"));
    CHECK(othello::edge_x_10_pattern_index(edge_context, Side::Black,
                                           EdgeX::Top) == 45943);
    for (const auto [edge, first, second, third] : {
             std::tuple{EdgeX::Top, "a1", "b1", "c1"},
             std::tuple{EdgeX::Bottom, "a8", "b8", "c8"},
             std::tuple{EdgeX::Left, "a1", "a2", "a3"},
             std::tuple{EdgeX::Right, "h1", "h2", "h3"},
         }) {
        const Board board =
            pattern_disc_board(othello::test::bit(first) | othello::test::bit(second),
                               othello::test::bit(third));
        CHECK(othello::edge_x_10_pattern_index(board, Side::Black, edge) == 22);
    }

    const Board diagonal =
        pattern_disc_board(othello::test::bit("a1") | othello::test::bit("c3"),
                           othello::test::bit("b2"));
    CHECK(othello::diagonal_8_pattern_index(diagonal, Side::Black,
                                            Diagonal::A1H8) == 16);

    const Board anti_diagonal =
        pattern_disc_board(othello::test::bit("h1") | othello::test::bit("f3"),
                           othello::test::bit("g2"));
    CHECK(othello::diagonal_8_pattern_index(anti_diagonal, Side::Black,
                                            Diagonal::H1A8) == 16);

    for (const auto [line, first, second, third] : {
             std::tuple{Inner::Top, "a2", "b2", "c2"},
             std::tuple{Inner::Bottom, "a7", "b7", "c7"},
             std::tuple{Inner::Left, "b1", "b2", "b3"},
             std::tuple{Inner::Right, "g1", "g2", "g3"},
         }) {
        const Board board =
            pattern_disc_board(othello::test::bit(first) | othello::test::bit(third),
                               othello::test::bit(second));
        CHECK(othello::inner_row_8_pattern_index(board, Side::Black, line) == 16);
    }
}

TEST_CASE("Edge 8 pattern table follows conservative edge rules", "[evaluation]") {
    const othello::EvaluationConfig config = edge_pattern_only_config(5);
    const Board own_full_edge = extra_disc_board(
        othello::test::bit("a1") | othello::test::bit("b1") | othello::test::bit("c1") |
        othello::test::bit("d1") | othello::test::bit("e1") | othello::test::bit("f1") |
        othello::test::bit("g1") | othello::test::bit("h1"));
    const Board opponent_full_edge = extra_disc_board(
        0, othello::test::bit("a1") | othello::test::bit("b1") |
               othello::test::bit("c1") | othello::test::bit("d1") |
               othello::test::bit("e1") | othello::test::bit("f1") |
               othello::test::bit("g1") | othello::test::bit("h1"));
    const Board own_anchor =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("c1"));
    const Board opponent_anchor =
        extra_disc_board(0, othello::test::bit("a1") | othello::test::bit("b1") |
                                othello::test::bit("c1"));
    const Board own_c = extra_disc_board(othello::test::bit("b1"));
    const Board opponent_c = extra_disc_board(0, othello::test::bit("b1"));
    const Board unanchored =
        extra_disc_board(othello::test::bit("b1") | othello::test::bit("c1"));

    CHECK(othello::edge_8_pattern_score(Board::initial(), Side::Black) == 0);
    CHECK(othello::edge_8_pattern_score(own_full_edge, Side::Black) > 0);
    CHECK(othello::edge_8_pattern_score(opponent_full_edge, Side::Black) < 0);
    CHECK(othello::edge_8_pattern_score(own_anchor, Side::Black) > 0);
    CHECK(othello::edge_8_pattern_score(opponent_anchor, Side::Black) < 0);
    CHECK(othello::edge_8_pattern_score(own_c, Side::Black) < 0);
    CHECK(othello::edge_8_pattern_score(opponent_c, Side::Black) > 0);
    CHECK(othello::edge_8_pattern_score(unanchored, Side::Black) <= 0);

    const othello::EvaluationBreakdown own_anchor_breakdown =
        othello::evaluate_basic_breakdown(own_anchor, Side::Black, config);
    CHECK(own_anchor_breakdown.edge_8_pattern > 0);
    CHECK(own_anchor_breakdown.edge_8_pattern_weight == 5);
    CHECK(own_anchor_breakdown.edge_8_pattern_score ==
          own_anchor_breakdown.edge_8_pattern * own_anchor_breakdown.edge_8_pattern_weight);
    CHECK(own_anchor_breakdown.total == non_terminal_score_sum(own_anchor_breakdown));
    CHECK(own_anchor_breakdown.total == own_anchor_breakdown.edge_8_pattern_score);
}

TEST_CASE("Edge 8 pattern is symmetric across colors and separable from edge stability",
          "[evaluation]") {
    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                             othello::test::bit("c1"),
                         othello::test::bit("e1"));
    const Board swapped = swapped_colors(board);

    CHECK(othello::edge_8_pattern_score(board, Side::Black) ==
          -othello::edge_8_pattern_score(swapped, Side::Black));
    CHECK(othello::evaluate_with_config(board, Side::Black, edge_pattern_only_config(3)) ==
          -othello::evaluate_with_config(swapped, Side::Black, edge_pattern_only_config(3)));

    const othello::EvaluationBreakdown combined = othello::evaluate_basic_breakdown(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::DefaultEdgePattern8V1));
    const othello::EvaluationBreakdown no_edge_lite = othello::evaluate_basic_breakdown(
        board, Side::Black,
        othello::evaluation_config_for_preset(
            othello::EvaluationPreset::DefaultEdgePattern8NoEdgeLite));

    CHECK(combined.edge_stability_lite_weight > 0);
    CHECK(combined.edge_8_pattern_weight > 0);
    CHECK(no_edge_lite.edge_stability_lite_weight == 0);
    CHECK(no_edge_lite.edge_8_pattern_weight > 0);
}

TEST_CASE("External pattern table is deterministic and config gated", "[evaluation]") {
    othello::EvaluationConfig config = pattern_table_only_config(7);
    config.pattern_tables.corner_2x3[1] = 3;
    config.pattern_tables.corner_2x3[2] = -3;
    config.pattern_tables.edge_8[1] = 2;
    config.pattern_tables.edge_8[2] = -2;

    const Board board = extra_disc_board(othello::test::bit("a1"));
    const othello::EvaluationBreakdown first =
        othello::evaluate_basic_breakdown(board, Side::Black, config);
    const othello::EvaluationBreakdown second =
        othello::evaluate_basic_breakdown(board, Side::Black, config);

    CHECK(first.pattern_table == 7);
    CHECK(first.pattern_table_weight == 7);
    CHECK(first.pattern_table_score == 49);
    CHECK(first.total == 49);
    CHECK(second.pattern_table == first.pattern_table);
    CHECK(second.total == first.total);

    othello::EvaluationConfig disabled = config;
    disabled.pattern_tables.enabled = false;
    const othello::EvaluationBreakdown disabled_breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, disabled);
    CHECK(disabled_breakdown.pattern_table == 0);
    CHECK(disabled_breakdown.pattern_table_score == 0);
    CHECK(disabled_breakdown.total == 0);

    othello::EvaluationConfig zero_weight = config;
    zero_weight.opening.pattern_table = 0;
    const othello::EvaluationBreakdown zero_weight_breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, zero_weight);
    CHECK(zero_weight_breakdown.pattern_table == 0);
    CHECK(zero_weight_breakdown.pattern_table_score == 0);
    CHECK(zero_weight_breakdown.total == 0);
}

TEST_CASE("External broad pattern table is antisymmetric under color swap",
          "[evaluation]") {
    othello::EvaluationConfig config = pattern_table_only_config(1);
    config.pattern_tables.corner_3x3[1] = 3;
    config.pattern_tables.corner_3x3[2] = -3;
    config.pattern_tables.edge_x_10[1] = 5;
    config.pattern_tables.edge_x_10[2] = -5;
    config.pattern_tables.diagonal_8[1] = 7;
    config.pattern_tables.diagonal_8[2] = -7;
    config.pattern_tables.inner_row_8[1] = 11;
    config.pattern_tables.inner_row_8[2] = -11;

    const Board corner_board = pattern_disc_board(othello::test::bit("a1"));
    const Board swapped_corner = swapped_colors(corner_board);
    CHECK(othello::evaluation_pattern_table_value(corner_board, Side::Black,
                                                  config.pattern_tables) == 20);
    CHECK(othello::evaluation_pattern_table_value(corner_board, Side::Black,
                                                  config.pattern_tables) ==
          -othello::evaluation_pattern_table_value(swapped_corner, Side::Black,
                                                   config.pattern_tables));

    const Board inner_board = pattern_disc_board(othello::test::bit("b1"));
    const Board swapped_inner = swapped_colors(inner_board);
    CHECK(othello::evaluation_pattern_table_value(inner_board, Side::Black,
                                                  config.pattern_tables) == 11);
    CHECK(othello::evaluate_with_config(inner_board, Side::Black, config) ==
          -othello::evaluate_with_config(swapped_inner, Side::Black, config));
}

TEST_CASE("Classic lite scores participate in configured total math", "[evaluation]") {
    othello::EvaluationConfig config{
        .opening = othello::EvaluationFeatureWeights{
            .corner_local_2x3 = 10,
            .edge_stability_lite = 3,
        },
        .midgame = othello::EvaluationFeatureWeights{
            .corner_local_2x3 = 10,
            .edge_stability_lite = 3,
        },
        .late = othello::EvaluationFeatureWeights{
            .corner_local_2x3 = 10,
            .edge_stability_lite = 3,
        },
    };
    const Board board =
        extra_disc_board(othello::test::bit("a1") | othello::test::bit("b1") |
                         othello::test::bit("a2"));

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, config);

    CHECK(breakdown.corner_local_2x3 == 2);
    CHECK(breakdown.edge_stability_lite == 2);
    CHECK(breakdown.corner_local_2x3_score == 20);
    CHECK(breakdown.edge_stability_lite_score == 6);
    CHECK(breakdown.total == non_terminal_score_sum(breakdown));
    CHECK(breakdown.total == 26);
}

TEST_CASE("Evaluation is symmetric under color swap", "[evaluation]") {
    const Board board = midgame_board();
    const Board swapped = swapped_colors(board);

    CHECK(othello::evaluate_basic(board, Side::Black) ==
          -othello::evaluate_basic(swapped, Side::Black));
    CHECK(othello::evaluate_basic(board, Side::White) ==
          -othello::evaluate_basic(swapped, Side::White));
}

TEST_CASE("Terminal board evaluation is strongly scaled", "[evaluation]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    CHECK(othello::evaluate_basic(board, Side::Black) > 10'000);
    CHECK(othello::evaluate_basic(board, Side::White) < -10'000);
}

TEST_CASE("Terminal evaluation breakdown uses terminal score only", "[evaluation]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };

    const othello::EvaluationBreakdown black =
        othello::evaluate_basic_breakdown(board, Side::Black);
    const othello::EvaluationBreakdown white =
        othello::evaluate_basic_breakdown(board, Side::White);

    CHECK(black.terminal);
    CHECK(black.terminal_disc_difference == othello::score(board, Side::Black));
    CHECK(black.terminal_score_weight == 1000);
    CHECK(black.terminal_score == black.terminal_disc_difference * black.terminal_score_weight);
    CHECK(black.total == black.terminal_score);
    CHECK(black.disc_difference_score == 0);
    CHECK(black.mobility_score == 0);
    CHECK(black.corner_occupancy_score == 0);
    CHECK(black.potential_mobility_score == 0);
    CHECK(black.corner_access_score == 0);
    CHECK(black.x_square_danger_score == 0);
    CHECK(black.frontier_score == 0);
    CHECK(black.corner_local_2x3_score == 0);
    CHECK(black.corner_2x3_pattern_score == 0);
    CHECK(black.edge_stability_lite_score == 0);
    CHECK(black.edge_8_pattern_score == 0);

    CHECK(white.terminal);
    CHECK(white.terminal_disc_difference == othello::score(board, Side::White));
    CHECK(white.terminal_score == white.terminal_disc_difference * white.terminal_score_weight);
    CHECK(white.total == white.terminal_score);
}

TEST_CASE("Terminal evaluation keeps fixed score scale across configs", "[evaluation]") {
    const Board board{
        .black = ~Bitboard{0},
        .white = 0,
        .side_to_move = Side::Black,
    };
    othello::EvaluationConfig config = othello::default_evaluation_config();
    config.opening.disc_difference = 99;
    config.midgame.mobility = 99;
    config.late.corner_occupancy = 99;
    config.opening.corner_local_2x3 = 99;
    config.opening.corner_2x3_pattern = 99;
    config.midgame.edge_stability_lite = 99;
    config.late.edge_8_pattern = 99;

    const othello::EvaluationBreakdown breakdown =
        othello::evaluate_basic_breakdown(board, Side::Black, config);

    CHECK(breakdown.terminal);
    CHECK(breakdown.terminal_score_weight == 1000);
    CHECK(breakdown.total == othello::score(board, Side::Black) * 1000);
}

TEST_CASE("Evaluation does not mutate the board", "[evaluation]") {
    const Board board = Board::initial();
    const Board before = board;

    static_cast<void>(othello::evaluate_disc_difference(board, Side::Black));
    static_cast<void>(othello::evaluate_mobility(board, Side::Black));
    static_cast<void>(othello::evaluate_basic_breakdown(board, Side::Black));
    static_cast<void>(othello::evaluate_basic_breakdown(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::MobilityPlusSmoke)));
    static_cast<void>(othello::evaluate_basic_breakdown(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::ClassicFeaturesLiteV1)));
    static_cast<void>(othello::evaluate_basic_breakdown(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::CornerPattern2x3V1)));
    static_cast<void>(othello::evaluate_basic_breakdown(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::EdgePattern8V1)));
    static_cast<void>(othello::evaluate_with_config(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::MobilityPlusSmoke)));
    static_cast<void>(othello::evaluate_with_config(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::ClassicFeaturesLiteV1)));
    static_cast<void>(othello::evaluate_with_config(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::CornerPattern2x3V1)));
    static_cast<void>(othello::evaluate_with_config(
        board, Side::Black,
        othello::evaluation_config_for_preset(othello::EvaluationPreset::EdgePattern8V1)));
    static_cast<void>(othello::evaluate_basic(board, Side::Black));

    CHECK(othello::test::same_board(board, before));
}
