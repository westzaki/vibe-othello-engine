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

TEST_CASE("Pattern-only eval config loader resolves compact pattern table paths",
          "[evaluation]") {
    const std::filesystem::path temp_dir = unique_temp_dir("pattern-only-eval-config");
    write_text_file(temp_dir / "patterns" / "tiny.tsv", "corner_2x3\t1\t6\n");
    const std::filesystem::path eval_path = temp_dir / "pattern_only.eval";
    write_text_file(eval_path, R"(schema_version=eval.v1
mode=pattern_only
name=pattern_only_tiny
pattern_table=patterns/tiny.tsv
opening.pattern_table=2
midgame.pattern_table=3
late.pattern_table=4
opening_max_occupied=16
midgame_max_occupied=40
)");

    const othello::tools::EvaluationConfigLoadResult loaded =
        othello::tools::load_evaluation_config_file(eval_path);

    REQUIRE(loaded.ok());
    CHECK(loaded.pattern_table_path == "patterns/tiny.tsv");
    REQUIRE(loaded.config.pattern_tables != nullptr);
    CHECK(loaded.config.pattern_tables->corner_2x3[1] == 6);
    check_scalar_weights_zero(loaded.config.opening);
    check_scalar_weights_zero(loaded.config.midgame);
    check_scalar_weights_zero(loaded.config.late);
    CHECK(loaded.config.opening.pattern_table == 2);
    CHECK(loaded.config.midgame.pattern_table == 3);
    CHECK(loaded.config.late.pattern_table == 4);
    CHECK(loaded.config.opening_max_occupied == 16);
    CHECK(loaded.config.midgame_max_occupied == 40);
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

    const std::filesystem::path missing_phase_eval = temp_dir / "missing_phase.eval";
    write_text_file(missing_phase_eval,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.opening"},
                          std::string{"patterns/missing_phase.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult missing_phase =
        othello::tools::load_evaluation_config_file(missing_phase_eval);
    CHECK_FALSE(missing_phase.ok());
    CHECK(missing_phase.error.find("failed to open pattern table") != std::string::npos);

    write_text_file(temp_dir / "patterns" / "malformed_phase.tsv", "corner_2x3\t1\n");
    const std::filesystem::path malformed_phase_eval = temp_dir / "malformed_phase.eval";
    write_text_file(malformed_phase_eval,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.midgame"},
                          std::string{"patterns/malformed_phase.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult malformed_phase =
        othello::tools::load_evaluation_config_file(malformed_phase_eval);
    CHECK_FALSE(malformed_phase.ok());
    CHECK(malformed_phase.error.find("expected '<family> <index> <value>'") !=
          std::string::npos);

    const std::filesystem::path duplicate_phase_key_eval = temp_dir / "duplicate_phase_key.eval";
    write_text_file(duplicate_phase_key_eval,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.opening"},
                          std::string{"patterns/missing_a.tsv"}},
                         {std::string{"pattern_table.opening"},
                          std::string{"patterns/missing_b.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult duplicate_phase_key =
        othello::tools::load_evaluation_config_file(duplicate_phase_key_eval);
    CHECK_FALSE(duplicate_phase_key.ok());
    CHECK(duplicate_phase_key.error.find("duplicate key: pattern_table.opening") !=
          std::string::npos);

    write_text_file(temp_dir / "patterns" / "out_of_range_phase.tsv",
                    "corner_2x3\t729\t1\n");
    const std::filesystem::path out_of_range_phase_eval =
        temp_dir / "out_of_range_phase.eval";
    write_text_file(out_of_range_phase_eval,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.late"},
                          std::string{"patterns/out_of_range_phase.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult out_of_range_phase =
        othello::tools::load_evaluation_config_file(out_of_range_phase_eval);
    CHECK_FALSE(out_of_range_phase.ok());
    CHECK(out_of_range_phase.error.find("corner_2x3 index out of range") !=
          std::string::npos);
}

TEST_CASE("Phase-specific pattern tables select the table for each phase",
          "[evaluation]") {
    const Board opening = extra_disc_board(othello::test::bit("a1"));
    const Board midgame = phase_midgame_board();
    const Board late = phase_late_board();
    REQUIRE_FALSE(othello::is_game_over(opening));
    REQUIRE_FALSE(othello::is_game_over(midgame));
    REQUIRE_FALSE(othello::is_game_over(late));

    const std::vector<Board> boards{opening, midgame, late};
    const std::vector<int> indexes = corner_2x3_indexes_for_boards(boards, Side::Black);
    const std::filesystem::path temp_dir = unique_temp_dir("phase-pattern-table-select");
    write_phase_table_fixture(temp_dir, "opening.tsv", indexes, 3);
    write_phase_table_fixture(temp_dir, "midgame.tsv", indexes, 5);
    write_phase_table_fixture(temp_dir, "late.tsv", indexes, 7);

    const std::filesystem::path eval_path = temp_dir / "phase.eval";
    write_text_file(eval_path,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.opening"},
                          std::string{"patterns/opening.tsv"}},
                         {std::string{"pattern_table.midgame"},
                          std::string{"patterns/midgame.tsv"}},
                         {std::string{"pattern_table.late"},
                          std::string{"patterns/late.tsv"}}}));

    const othello::tools::EvaluationConfigLoadResult loaded =
        othello::tools::load_evaluation_config_file(eval_path);
    REQUIRE(loaded.ok());
    CHECK_FALSE(loaded.pattern_table_path.has_value());
    CHECK(loaded.opening_pattern_table_path == "patterns/opening.tsv");
    CHECK(loaded.midgame_pattern_table_path == "patterns/midgame.tsv");
    CHECK(loaded.late_pattern_table_path == "patterns/late.tsv");
    REQUIRE(loaded.config.pattern_tables == nullptr);
    REQUIRE(loaded.config.opening_pattern_tables != nullptr);
    REQUIRE(loaded.config.midgame_pattern_tables != nullptr);
    REQUIRE(loaded.config.late_pattern_tables != nullptr);

    const othello::EvaluationBreakdown opening_breakdown =
        othello::evaluate_basic_breakdown(opening, Side::Black, loaded.config);
    const othello::EvaluationBreakdown midgame_breakdown =
        othello::evaluate_basic_breakdown(midgame, Side::Black, loaded.config);
    const othello::EvaluationBreakdown late_breakdown =
        othello::evaluate_basic_breakdown(late, Side::Black, loaded.config);

    CHECK(opening_breakdown.phase == othello::EvaluationPhase::Opening);
    CHECK(midgame_breakdown.phase == othello::EvaluationPhase::Midgame);
    CHECK(late_breakdown.phase == othello::EvaluationPhase::Late);
    CHECK(opening_breakdown.pattern_table == 12);
    CHECK(midgame_breakdown.pattern_table == 20);
    CHECK(late_breakdown.pattern_table == 28);
}

TEST_CASE("Phase-specific pattern tables fall back to the global table",
          "[evaluation]") {
    const Board opening = extra_disc_board(othello::test::bit("a1"));
    const Board midgame = phase_midgame_board();
    const Board late = phase_late_board();
    const std::vector<Board> boards{opening, midgame, late};
    const std::vector<int> indexes = corner_2x3_indexes_for_boards(boards, Side::Black);
    const std::filesystem::path temp_dir = unique_temp_dir("phase-pattern-table-fallback");
    write_phase_table_fixture(temp_dir, "global.tsv", indexes, 2);
    write_phase_table_fixture(temp_dir, "opening.tsv", indexes, 3);

    const std::filesystem::path global_eval_path = temp_dir / "global.eval";
    write_text_file(global_eval_path,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table"},
                          std::string{"patterns/global.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult global =
        othello::tools::load_evaluation_config_file(global_eval_path);
    REQUIRE(global.ok());
    CHECK(othello::evaluate_basic_breakdown(opening, Side::Black, global.config)
              .pattern_table == 8);
    CHECK(othello::evaluate_basic_breakdown(midgame, Side::Black, global.config)
              .pattern_table == 8);
    CHECK(othello::evaluate_basic_breakdown(late, Side::Black, global.config)
              .pattern_table == 8);

    const std::filesystem::path mixed_eval_path = temp_dir / "mixed.eval";
    write_text_file(mixed_eval_path,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table"},
                          std::string{"patterns/global.tsv"}},
                         {std::string{"pattern_table.opening"},
                          std::string{"patterns/opening.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult mixed =
        othello::tools::load_evaluation_config_file(mixed_eval_path);
    REQUIRE(mixed.ok());
    CHECK(othello::evaluate_basic_breakdown(opening, Side::Black, mixed.config)
              .pattern_table == 12);
    CHECK(othello::evaluate_basic_breakdown(midgame, Side::Black, mixed.config)
              .pattern_table == 8);
    CHECK(othello::evaluate_basic_breakdown(late, Side::Black, mixed.config)
              .pattern_table == 8);

    const std::filesystem::path opening_only_eval_path = temp_dir / "opening_only.eval";
    write_text_file(opening_only_eval_path,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.opening"},
                          std::string{"patterns/opening.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult opening_only =
        othello::tools::load_evaluation_config_file(opening_only_eval_path);
    REQUIRE(opening_only.ok());
    CHECK(othello::evaluate_basic_breakdown(opening, Side::Black, opening_only.config)
              .pattern_table == 12);
    CHECK(othello::evaluate_basic_breakdown(midgame, Side::Black, opening_only.config)
              .pattern_table == 0);
    CHECK(othello::evaluate_basic_breakdown(late, Side::Black, opening_only.config)
              .pattern_table == 0);
}

TEST_CASE("Phase-specific zero sentinel pattern tables parse and contribute zero",
          "[evaluation]") {
    const std::filesystem::path temp_dir =
        unique_temp_dir("phase-pattern-table-zero-sentinel");
    constexpr std::string_view sentinel_table =
        "# empty_phase_sentinel: true\n"
        "# zero_effect: true\n"
        "corner_2x3\t0\t0\n";
    write_text_file(temp_dir / "tables" / "opening.tsv", sentinel_table);
    write_text_file(temp_dir / "tables" / "midgame.tsv", sentinel_table);
    write_text_file(temp_dir / "tables" / "late.tsv", sentinel_table);

    const std::filesystem::path eval_path = temp_dir / "candidate.eval";
    write_text_file(eval_path,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.opening"},
                          std::string{"tables/opening.tsv"}},
                         {std::string{"pattern_table.midgame"},
                          std::string{"tables/midgame.tsv"}},
                         {std::string{"pattern_table.late"},
                          std::string{"tables/late.tsv"}}}));

    const othello::tools::EvaluationConfigLoadResult loaded =
        othello::tools::load_evaluation_config_file(eval_path);
    REQUIRE(loaded.ok());
    REQUIRE(loaded.config.opening_pattern_tables != nullptr);
    REQUIRE(loaded.config.midgame_pattern_tables != nullptr);
    REQUIRE(loaded.config.late_pattern_tables != nullptr);

    const Board opening = extra_disc_board(othello::test::bit("a1"));
    const Board midgame = phase_midgame_board();
    const Board late = phase_late_board();
    CHECK(othello::evaluate_basic_breakdown(opening, Side::Black, loaded.config)
              .pattern_table == 0);
    CHECK(othello::evaluate_basic_breakdown(midgame, Side::Black, loaded.config)
              .pattern_table == 0);
    CHECK(othello::evaluate_basic_breakdown(late, Side::Black, loaded.config)
              .pattern_table == 0);
}

TEST_CASE("Phase-specific pattern table ownership is shared and equality is semantic",
          "[evaluation]") {
    const Board opening = extra_disc_board(othello::test::bit("a1"));
    const std::vector<Board> boards{opening, phase_midgame_board(), phase_late_board()};
    const std::vector<int> indexes = corner_2x3_indexes_for_boards(boards, Side::Black);
    const std::filesystem::path temp_dir = unique_temp_dir("phase-pattern-table-ownership");
    write_phase_table_fixture(temp_dir, "opening.tsv", indexes, 3);

    const std::filesystem::path eval_path = temp_dir / "phase.eval";
    write_text_file(eval_path,
                    eval_config_with_pattern_table_paths(
                        {{std::string{"pattern_table.opening"},
                          std::string{"patterns/opening.tsv"}}}));
    const othello::tools::EvaluationConfigLoadResult first =
        othello::tools::load_evaluation_config_file(eval_path);
    const othello::tools::EvaluationConfigLoadResult second =
        othello::tools::load_evaluation_config_file(eval_path);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.config.opening_pattern_tables != nullptr);
    REQUIRE(second.config.opening_pattern_tables != nullptr);

    const othello::EvaluationConfig copied = first.config;
    CHECK(copied.opening_pattern_tables == first.config.opening_pattern_tables);
    CHECK(second.config.opening_pattern_tables != first.config.opening_pattern_tables);
    CHECK(second.config == first.config);
}
