#include "common/eval_config_io.hpp"
#include "common/evaluator_selection.hpp"
#include "evaluation_test_helpers.hpp"
#include "positions/metrics.hpp"
#include "positions/search_positions.hpp"
#include "positions/tags.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

using othello::Bitboard;
using othello::Board;
using othello::Side;
using namespace othello::test::evaluation;

TEST_CASE("Sample eval configs round-trip to expected evaluator configs", "[evaluation]") {
    const othello::tools::EvaluationConfigLoadResult current_default =
        othello::tools::load_evaluation_config_file(sample_eval_config_path("current_default.eval"));
    REQUIRE(current_default.ok());
    REQUIRE(current_default.name.has_value());
    CHECK(*current_default.name == "current_default");
    CHECK(current_default.config == othello::default_evaluation_config());
    CHECK(current_default.config.pattern_tables == nullptr);
    CHECK(current_default.config.opening_pattern_tables == nullptr);
    CHECK(current_default.config.midgame_pattern_tables == nullptr);
    CHECK(current_default.config.late_pattern_tables == nullptr);
    CHECK(current_default.config.opening.pattern_table == 0);
    CHECK(current_default.config.midgame.pattern_table == 0);
    CHECK(current_default.config.late.pattern_table == 0);

    const othello::tools::EvaluationConfigLoadResult pattern =
        othello::tools::load_evaluation_config_file(
            sample_eval_config_path("pattern_teacher_v0.eval"));
    REQUIRE(pattern.ok());
    REQUIRE(pattern.name.has_value());
    CHECK(*pattern.name == "pattern_teacher_v0");
    CHECK(pattern.pattern_table_path == "patterns/pattern_teacher_v0.tsv");
    CHECK_FALSE(pattern.opening_pattern_table_path.has_value());
    CHECK_FALSE(pattern.midgame_pattern_table_path.has_value());
    CHECK_FALSE(pattern.late_pattern_table_path.has_value());
    REQUIRE(pattern.config.pattern_tables != nullptr);
    CHECK(pattern.config.opening_pattern_tables == nullptr);
    CHECK(pattern.config.midgame_pattern_tables == nullptr);
    CHECK(pattern.config.late_pattern_tables == nullptr);
    CHECK(pattern.config.opening.pattern_table == 10);
    CHECK(pattern.config.midgame.pattern_table == 10);
    CHECK(pattern.config.late.pattern_table == 10);
    CHECK(std::ranges::count_if(pattern.config.pattern_tables->corner_2x3,
                                [](std::int16_t value) { return value != 0; }) == 64);
    CHECK(std::ranges::count_if(pattern.config.pattern_tables->edge_8,
                                [](std::int16_t value) { return value != 0; }) == 64);

    const othello::EvaluationConfig pattern_copy = pattern.config;
    CHECK(pattern_copy == pattern.config);
    CHECK(pattern_copy.pattern_tables == pattern.config.pattern_tables);

    const othello::tools::EvaluationConfigLoadResult pattern_reloaded =
        othello::tools::load_evaluation_config_file(
            sample_eval_config_path("pattern_teacher_v0.eval"));
    REQUIRE(pattern_reloaded.ok());
    REQUIRE(pattern_reloaded.config.pattern_tables != nullptr);
    CHECK(pattern_reloaded.config.pattern_tables != pattern.config.pattern_tables);
    CHECK(pattern_reloaded.config == pattern.config);

    const othello::tools::EvaluationConfigLoadResult reboot =
        othello::tools::load_evaluation_config_file(
            sample_eval_config_path("pattern_reboot_v0.eval"));
    REQUIRE(reboot.ok());
    REQUIRE(reboot.name.has_value());
    CHECK(*reboot.name == "pattern_reboot_v0");
    CHECK(reboot.pattern_table_path == "patterns/pattern_teacher_v0.tsv");
    CHECK(reboot.config != othello::default_evaluation_config());
    REQUIRE(reboot.config.pattern_tables != nullptr);
    check_scalar_weights_zero(reboot.config.opening);
    check_scalar_weights_zero(reboot.config.midgame);
    check_scalar_weights_zero(reboot.config.late);
    CHECK(reboot.config.opening.pattern_table == 4);
    CHECK(reboot.config.midgame.pattern_table == 4);
    CHECK(reboot.config.late.pattern_table == 4);
    CHECK(reboot.config.opening_max_occupied == 20);
    CHECK(reboot.config.midgame_max_occupied == 44);
    CHECK(std::ranges::count_if(reboot.config.pattern_tables->corner_2x3,
                                [](std::int16_t value) { return value != 0; }) == 64);
    CHECK(std::ranges::count_if(reboot.config.pattern_tables->edge_8,
                                [](std::int16_t value) { return value != 0; }) == 64);

    const othello::tools::EvaluationConfigLoadResult ntest_pairwise =
        othello::tools::load_evaluation_config_file(
            sample_eval_config_path("ntest_pairwise_full_v2.eval"));
    REQUIRE(ntest_pairwise.ok());
    REQUIRE(ntest_pairwise.name.has_value());
    CHECK(*ntest_pairwise.name == "ntest_pairwise_full_v2");
    CHECK_FALSE(ntest_pairwise.pattern_table_path.has_value());
    CHECK(ntest_pairwise.opening_pattern_table_path ==
          "patterns/ntest_pairwise_full_v2_opening.tsv");
    CHECK(ntest_pairwise.midgame_pattern_table_path ==
          "patterns/ntest_pairwise_full_v2_midgame.tsv");
    CHECK(ntest_pairwise.late_pattern_table_path ==
          "patterns/ntest_pairwise_full_v2_late.tsv");
    CHECK(ntest_pairwise.config != othello::default_evaluation_config());
    CHECK(ntest_pairwise.config.pattern_tables == nullptr);
    REQUIRE(ntest_pairwise.config.opening_pattern_tables != nullptr);
    REQUIRE(ntest_pairwise.config.midgame_pattern_tables != nullptr);
    REQUIRE(ntest_pairwise.config.late_pattern_tables != nullptr);
    CHECK(ntest_pairwise.config.opening.pattern_table == 1);
    CHECK(ntest_pairwise.config.midgame.pattern_table == 1);
    CHECK(ntest_pairwise.config.late.pattern_table == 1);
    CHECK(ntest_pairwise.config.opening.mobility ==
          othello::default_evaluation_config().opening.mobility);
    CHECK(ntest_pairwise.config.midgame.mobility ==
          othello::default_evaluation_config().midgame.mobility);
    CHECK(ntest_pairwise.config.late.mobility ==
          othello::default_evaluation_config().late.mobility);
    CHECK(std::ranges::any_of(ntest_pairwise.config.opening_pattern_tables->corner_3x3,
                              [](std::int16_t value) { return value != 0; }));
    CHECK(std::ranges::any_of(ntest_pairwise.config.midgame_pattern_tables->corner_3x3,
                              [](std::int16_t value) { return value != 0; }));
    CHECK(std::ranges::any_of(ntest_pairwise.config.late_pattern_tables->corner_3x3,
                              [](std::int16_t value) { return value != 0; }));

    const othello::tools::EvaluationConfigLoadResult pattern_only_smoke =
        othello::tools::load_evaluation_config_file(
            test_eval_config_fixture_path("pattern_only_smoke.eval"));
    REQUIRE(pattern_only_smoke.ok());
    REQUIRE(pattern_only_smoke.name.has_value());
    CHECK(*pattern_only_smoke.name == "pattern_only_smoke");
    CHECK_FALSE(pattern_only_smoke.pattern_table_path.has_value());
    CHECK(pattern_only_smoke.config.pattern_tables == nullptr);
    check_scalar_weights_zero(pattern_only_smoke.config.opening);
    check_scalar_weights_zero(pattern_only_smoke.config.midgame);
    check_scalar_weights_zero(pattern_only_smoke.config.late);
    CHECK(pattern_only_smoke.config.opening.pattern_table == 1);
    CHECK(pattern_only_smoke.config.midgame.pattern_table == 1);
    CHECK(pattern_only_smoke.config.late.pattern_table == 1);
    CHECK(pattern_only_smoke.config.opening_max_occupied == 20);
    CHECK(pattern_only_smoke.config.midgame_max_occupied == 44);
}

TEST_CASE("Committed eval artifact surface contains only active fixtures",
          "[evaluation]") {
    constexpr std::array allowed_eval_configs{
        std::string_view{"current_default.eval"},
        std::string_view{"ntest_pairwise_full_v2.eval"},
        std::string_view{"pattern_reboot_v0.eval"},
        std::string_view{"pattern_teacher_v0.eval"},
    };
    constexpr std::array allowed_pattern_tables{
        std::string_view{"ntest_pairwise_full_v2_late.tsv"},
        std::string_view{"ntest_pairwise_full_v2_midgame.tsv"},
        std::string_view{"ntest_pairwise_full_v2_opening.tsv"},
        std::string_view{"pattern_teacher_v0.tsv"},
    };

    CHECK(committed_artifact_names(sample_eval_config_dir(), ".eval") ==
          sorted_names(allowed_eval_configs));
    CHECK(committed_artifact_names(sample_eval_config_path("patterns"), ".tsv") ==
          sorted_names(allowed_pattern_tables));
}

TEST_CASE("Committed eval config fixtures parse and preserve intended identities",
          "[evaluation]") {
    const std::vector<std::filesystem::path> paths = committed_eval_config_paths();
    REQUIRE_FALSE(paths.empty());

    const othello::EvaluationConfig default_config = othello::default_evaluation_config();

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

TEST_CASE("Tool evaluator selection resolves the default evaluator and config files",
          "[evaluation]") {
    std::string error;

    const std::optional<othello::tools::EvaluatorSelection> default_selection =
        othello::tools::parse_evaluator_selection(
            {.project_default_config_path = sample_eval_config_path("current_default.eval")},
            error);
    REQUIRE(default_selection.has_value());
    CHECK(default_selection->config_path ==
          sample_eval_config_path("current_default.eval").string());
    CHECK(default_selection->config_override.has_value());
    CHECK_FALSE(othello::tools::has_custom_eval_config(*default_selection));
    CHECK(othello::tools::uses_project_default_eval_config(*default_selection));
    CHECK_FALSE(othello::tools::uses_built_in_fallback_eval_config(*default_selection));
    CHECK(othello::tools::resolve_evaluator_selection(*default_selection) ==
          othello::default_evaluation_config());

    const othello::tools::EvaluatorSelection built_in_fallback_selection;
    CHECK_FALSE(built_in_fallback_selection.config_override.has_value());
    CHECK_FALSE(othello::tools::has_custom_eval_config(built_in_fallback_selection));
    CHECK_FALSE(othello::tools::uses_project_default_eval_config(
        built_in_fallback_selection));
    CHECK(othello::tools::uses_built_in_fallback_eval_config(
        built_in_fallback_selection));
    CHECK(othello::tools::resolve_evaluator_selection(built_in_fallback_selection) ==
          othello::default_evaluation_config());

    const std::string config_path =
        sample_eval_config_path("pattern_teacher_v0.eval").string();
    const std::optional<othello::tools::EvaluatorSelection> config_selection =
        othello::tools::parse_evaluator_selection({.config_path = config_path}, error);
    REQUIRE(config_selection.has_value());
    CHECK(config_selection->config_path == config_path);
    CHECK(othello::tools::has_custom_eval_config(*config_selection));
    CHECK_FALSE(othello::tools::uses_project_default_eval_config(*config_selection));
    CHECK_FALSE(othello::tools::uses_built_in_fallback_eval_config(*config_selection));
    CHECK(config_selection->config_override.has_value());
    CHECK(config_selection->config_override->pattern_tables != nullptr);
    CHECK(config_selection->config_override->opening.pattern_table == 10);
    CHECK(othello::tools::resolve_evaluator_selection(*config_selection) ==
          *config_selection->config_override);
}

TEST_CASE("Tool evaluator selection rejects invalid config input", "[evaluation]") {
    std::string error;

    const std::optional<othello::tools::EvaluatorSelection> empty =
        othello::tools::parse_evaluator_selection({.config_path = std::string{}}, error);
    CHECK_FALSE(empty.has_value());
    CHECK(error.find("invalid --eval-config value") != std::string::npos);

    const std::optional<othello::tools::EvaluatorSelection> missing =
        othello::tools::parse_evaluator_selection(
            {.config_path = sample_eval_config_path("missing.eval").string()}, error);
    CHECK_FALSE(missing.has_value());
    CHECK(error.find("failed to open evaluation config") != std::string::npos);

    const std::optional<othello::tools::EvaluatorSelection> missing_project_default =
        othello::tools::parse_evaluator_selection(
            {.project_default_config_path = sample_eval_config_path("missing.eval")},
            error);
    CHECK_FALSE(missing_project_default.has_value());
    CHECK(error.find("invalid project default eval config") != std::string::npos);
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

    const auto unknown_mode = othello::tools::parse_evaluation_config("mode=unknown\n");
    CHECK_FALSE(unknown_mode.ok());
    CHECK(unknown_mode.error.find("unknown mode") != std::string::npos);

    const auto unsupported_schema =
        othello::tools::parse_evaluation_config("schema_version=eval.v2\n");
    CHECK_FALSE(unsupported_schema.ok());
    CHECK(unsupported_schema.error.find("unsupported schema_version") !=
          std::string::npos);
}

TEST_CASE("Eval config parser accepts legacy rule aliases for handcrafted pattern features",
          "[evaluation]") {
    const std::string legacy_text =
        read_text_file(sample_eval_config_path("pattern_teacher_v0.eval"));
    std::string alias_text = legacy_text;
    replace_all(alias_text, "opening.corner_2x3_pattern",
                "opening.legacy_corner_2x3_rule");
    replace_all(alias_text, "opening.edge_8_pattern", "opening.legacy_edge_8_rule");
    replace_all(alias_text, "midgame.corner_2x3_pattern",
                "midgame.legacy_corner_2x3_rule");
    replace_all(alias_text, "midgame.edge_8_pattern", "midgame.legacy_edge_8_rule");
    replace_all(alias_text, "late.corner_2x3_pattern", "late.legacy_corner_2x3_rule");
    replace_all(alias_text, "late.edge_8_pattern", "late.legacy_edge_8_rule");

    const othello::tools::EvaluationConfigLoadResult legacy =
        othello::tools::parse_evaluation_config(legacy_text);
    const othello::tools::EvaluationConfigLoadResult alias =
        othello::tools::parse_evaluation_config(alias_text);

    REQUIRE(legacy.ok());
    REQUIRE(alias.ok());
    CHECK(alias.config == legacy.config);
    CHECK(alias.pattern_table_path == legacy.pattern_table_path);
    CHECK(alias.config.opening.corner_2x3_pattern == 19);
    CHECK(alias.config.midgame.edge_8_pattern == 4);
    CHECK(alias.config.opening.pattern_table == 10);
    CHECK(alias.config.midgame.pattern_table == 10);
    CHECK(alias.config.late.pattern_table == 10);
}

TEST_CASE("Eval config parser rejects canonical and alias keys for the same feature",
          "[evaluation]") {
    const othello::tools::EvaluationConfigLoadResult duplicate_corner =
        othello::tools::parse_evaluation_config(R"(mode=pattern_only
opening.corner_2x3_pattern=1
opening.legacy_corner_2x3_rule=2
)");
    CHECK_FALSE(duplicate_corner.ok());
    CHECK(duplicate_corner.error.find("duplicate feature key: opening.corner_2x3_pattern") !=
          std::string::npos);

    const othello::tools::EvaluationConfigLoadResult duplicate_edge =
        othello::tools::parse_evaluation_config(R"(mode=pattern_only
midgame.edge_8_pattern=1
midgame.legacy_edge_8_rule=2
)");
    CHECK_FALSE(duplicate_edge.ok());
    CHECK(duplicate_edge.error.find("duplicate feature key: midgame.edge_8_pattern") !=
          std::string::npos);
}

TEST_CASE("Pattern-only eval config parser defaults omitted feature weights to zero",
          "[evaluation]") {
    const othello::tools::EvaluationConfigLoadResult loaded =
        othello::tools::parse_evaluation_config(R"(schema_version=eval.v1
mode=pattern_only
name=compact_pattern
opening.pattern_table=2
midgame.pattern_table=3
late.pattern_table=4
opening_max_occupied=18
midgame_max_occupied=42
)");

    REQUIRE(loaded.ok());
    REQUIRE(loaded.name.has_value());
    CHECK(*loaded.name == "compact_pattern");
    CHECK_FALSE(loaded.pattern_table_path.has_value());
    check_scalar_weights_zero(loaded.config.opening);
    check_scalar_weights_zero(loaded.config.midgame);
    check_scalar_weights_zero(loaded.config.late);
    CHECK(loaded.config.opening.pattern_table == 2);
    CHECK(loaded.config.midgame.pattern_table == 3);
    CHECK(loaded.config.late.pattern_table == 4);
    CHECK(loaded.config.opening_max_occupied == 18);
    CHECK(loaded.config.midgame_max_occupied == 42);
}

TEST_CASE("Pattern-only eval config accepts explicit scalar overrides",
          "[evaluation]") {
    const othello::tools::EvaluationConfigLoadResult loaded =
        othello::tools::parse_evaluation_config(R"(mode=pattern_only
opening.mobility=5
midgame.frontier=-2
late.edge_8_pattern=3
opening.pattern_table=2
)");

    REQUIRE(loaded.ok());
    CHECK(loaded.config.opening.mobility == 5);
    CHECK(loaded.config.opening.disc_difference == 0);
    CHECK(loaded.config.midgame.frontier == -2);
    CHECK(loaded.config.midgame.mobility == 0);
    CHECK(loaded.config.late.edge_8_pattern == 3);
    CHECK(loaded.config.late.mobility == 0);
    CHECK(loaded.config.opening.pattern_table == 2);
    CHECK(loaded.config.midgame.pattern_table == 0);
    CHECK(loaded.config.late.pattern_table == 0);
    CHECK(loaded.config.opening_max_occupied == 20);
    CHECK(loaded.config.midgame_max_occupied == 44);
}
