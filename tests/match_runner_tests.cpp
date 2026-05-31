#include "../tools/match_runner/core.hpp"
#include "../tools/match_runner/engine_config.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace runner = othello::match_runner;

namespace {

[[nodiscard]] bool same_board(const othello::Board& lhs, const othello::Board& rhs) noexcept {
    return lhs.black == rhs.black && lhs.white == rhs.white && lhs.side_to_move == rhs.side_to_move;
}

[[nodiscard]] othello::Board one_empty_forced_board() {
    return othello::test::board_from_text(R"(BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBBB
BBBBBBW.
side=B)");
}

[[nodiscard]] std::string sample_eval_config_path(std::string_view file_name) {
    return (std::filesystem::path{OTHELLO_SOURCE_DIR} / "data" / "eval" / file_name).string();
}

[[nodiscard]] runner::PlayerSpec require_player_spec(const std::string& spec_text) {
    const std::optional<runner::PlayerSpec> spec = runner::parse_player_spec(spec_text);
    REQUIRE(spec.has_value());
    return *spec;
}

[[nodiscard]] runner::PlayerSpec require_player_spec(std::string_view spec_text) {
    return require_player_spec(std::string{spec_text});
}

[[nodiscard]] othello::SearchOptions require_search_options(std::string_view spec_text) {
    return runner::make_search_options(require_player_spec(spec_text));
}

void check_eval_preset_spec(std::string_view spec_text, othello::EvaluationPreset expected) {
    const runner::PlayerSpec spec = require_player_spec(spec_text);
    CHECK(spec.search_options.evaluator.preset == expected);
    CHECK(runner::make_search_options(spec).evaluation_preset == expected);
}

void check_eval_preset_config(std::string_view spec_text, othello::EvaluationPreset expected) {
    const othello::SearchOptions options = require_search_options(spec_text);
    CHECK(options.evaluation_preset == expected);
    CHECK(othello::resolve_evaluation_config(options) ==
          othello::evaluation_config_for_preset(expected));
}

} // namespace

TEST_CASE("Search player specs require positive depth", "[match-runner]") {
    CHECK_FALSE(runner::parse_player_spec("search:depth=0").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=-1").has_value());
    CHECK(runner::parse_player_spec("search:depth=1").has_value());
}

TEST_CASE("Search player specs parse options", "[match-runner]") {
    const othello::SearchOptions depth_only_options = require_search_options("search:depth=4");
    CHECK(depth_only_options.max_depth == 4);
    CHECK_FALSE(depth_only_options.use_transposition_table);
    CHECK_FALSE(depth_only_options.use_pvs);
    CHECK(depth_only_options.exact_endgame_empty_threshold == 12);
    CHECK(depth_only_options.transposition_table_entries == (1U << 18));

    const othello::SearchOptions tt_on_options = require_search_options("search:depth=4,tt=on");
    CHECK(tt_on_options.use_transposition_table);

    const othello::SearchOptions pvs_exact_off_options =
        require_search_options("search:depth=4,tt=off,pvs=on,exact=off");
    CHECK_FALSE(pvs_exact_off_options.use_transposition_table);
    CHECK(pvs_exact_off_options.use_pvs);
    CHECK(pvs_exact_off_options.exact_endgame_empty_threshold == 0);
    CHECK(pvs_exact_off_options.exact_endgame_root_policy ==
          othello::ExactEndgameRootPolicy::FixedThreshold);

    const othello::SearchOptions exact_options = require_search_options("search:depth=4,exact=12");
    CHECK(exact_options.exact_endgame_empty_threshold == 12);
    CHECK(exact_options.exact_endgame_root_policy ==
          othello::ExactEndgameRootPolicy::FixedThreshold);
    const othello::SearchOptions adaptive_exact_options =
        require_search_options("search:depth=4,exact=adaptive16");
    CHECK(adaptive_exact_options.exact_endgame_empty_threshold == 16);
    CHECK(adaptive_exact_options.exact_endgame_root_policy ==
          othello::ExactEndgameRootPolicy::Adaptive16);
    CHECK(require_search_options("search:depth=4,tt_entries=262144")
              .transposition_table_entries == 262144U);

    check_eval_preset_config("search:depth=4,eval=default", othello::EvaluationPreset::Default);
    check_eval_preset_config("search:depth=4,eval=phase_aware_v1",
                             othello::EvaluationPreset::PhaseAwareV1);
    check_eval_preset_config("search:depth=4,eval=mobility_plus_smoke",
                             othello::EvaluationPreset::MobilityPlusSmoke);
    check_eval_preset_config("search:depth=4,eval=frontier_open2_mid2_late_plus1",
                             othello::EvaluationPreset::FrontierOpen2Mid2LatePlus1);
    check_eval_preset_spec("search:depth=4,eval=classic_corner_lite_v1",
                           othello::EvaluationPreset::ClassicCornerLiteV1);
    check_eval_preset_spec("search:depth=4,eval=classic_edge_lite_v1",
                           othello::EvaluationPreset::ClassicEdgeLiteV1);
    check_eval_preset_spec("search:depth=4,eval=classic_features_lite_v1",
                           othello::EvaluationPreset::ClassicFeaturesLiteV1);
    check_eval_preset_spec("search:depth=4,eval=classic_features_lite_aggressive",
                           othello::EvaluationPreset::ClassicFeaturesLiteAggressive);
    check_eval_preset_spec("search:depth=4,eval=frontier_classic_features_lite_v1",
                           othello::EvaluationPreset::FrontierClassicFeaturesLiteV1);
    check_eval_preset_spec("search:depth=4,eval=corner_pattern_2x3_v1",
                           othello::EvaluationPreset::CornerPattern2x3V1);
    check_eval_preset_spec("search:depth=4,eval=frontier_corner_pattern_2x3_v1",
                           othello::EvaluationPreset::FrontierCornerPattern2x3V1);
    check_eval_preset_spec("search:depth=4,eval=frontier_corner_pattern_edge_lite_v1",
                           othello::EvaluationPreset::FrontierCornerPatternEdgeLiteV1);
    check_eval_preset_spec("search:depth=4,eval=edge_pattern_8_v1",
                           othello::EvaluationPreset::EdgePattern8V1);
    check_eval_preset_spec("search:depth=4,eval=default_edge_pattern_8_v1",
                           othello::EvaluationPreset::DefaultEdgePattern8V1);
    check_eval_preset_spec("search:depth=4,eval=default_edge_pattern_8_no_edge_lite",
                           othello::EvaluationPreset::DefaultEdgePattern8NoEdgeLite);
    check_eval_preset_spec("search:depth=4,eval=default_edge_pattern_8_aggressive",
                           othello::EvaluationPreset::DefaultEdgePattern8Aggressive);

    const othello::SearchOptions eval_config_options = require_search_options(
        "search:depth=4,eval_config=" +
        sample_eval_config_path("default_edge_pattern_8_soft.eval"));
    othello::EvaluationConfig expected_soft = othello::default_evaluation_config();
    expected_soft.opening.edge_8_pattern = 1;
    expected_soft.midgame.edge_8_pattern = 3;
    expected_soft.late.edge_8_pattern = 5;
    CHECK(eval_config_options.evaluation_config_override.has_value());
    CHECK(othello::resolve_evaluation_config(eval_config_options) == expected_soft);
}

TEST_CASE("Search player specs reject invalid options", "[match-runner]") {
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,tt=maybe").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,unknown=1").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,tt=on,tt=off").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,pvs=on,pvs=off").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,exact=1,exact=off").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,tt_entries=1,tt_entries=2")
                    .has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,exact=-1").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,exact=adaptive17").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,tt_entries=-1").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,eval=unknown").has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,eval=default,eval=default")
                    .has_value());
    CHECK_FALSE(runner::parse_player_spec("search:depth=4,eval_config=missing.eval").has_value());
    CHECK_FALSE(runner::parse_player_spec(
                    "search:depth=4,eval=default,eval_config=" +
                    sample_eval_config_path("current_default.eval"))
                    .has_value());
    CHECK_FALSE(runner::parse_player_spec(
                    "search:depth=4,eval_config=" +
                    sample_eval_config_path("current_default.eval") +
                    ",eval=default")
                    .has_value());
}

TEST_CASE("External NBoard player specs parse engine names", "[match-runner]") {
    const auto external = runner::parse_player_spec("external:ntest8");

    REQUIRE(external.has_value());
    CHECK(external->kind == runner::PlayerKind::ExternalNBoard);
    CHECK(external->external_engine_name == "ntest8");
    CHECK(external->text == "external:ntest8");
    CHECK_FALSE(runner::parse_player_spec("external:").has_value());
}

TEST_CASE("External engine config parser accepts line-based configs", "[match-runner]") {
    const runner::EngineConfigParseResult result =
        runner::parse_engine_config_line("head|5|../head|../build-head/othello_nboard_engine");

    REQUIRE(result.ok);
    REQUIRE(result.has_config);
    CHECK(result.config.name == "head");
    CHECK(result.config.depth == 5);
    REQUIRE(result.config.cwd.has_value());
    CHECK(*result.config.cwd == "../head");
    REQUIRE(result.config.command.size() == 1);
    CHECK(result.config.command[0] == "../build-head/othello_nboard_engine");
}

TEST_CASE("External engine config parser handles comments, empty cwd, and args",
          "[match-runner]") {
    const runner::EngineConfigParseResult comment = runner::parse_engine_config_line("# comment");
    const runner::EngineConfigParseResult blank = runner::parse_engine_config_line("   ");
    const runner::EngineConfigParseResult with_args =
        runner::parse_engine_config_line("ntest8|8||ntest|x");

    CHECK(comment.ok);
    CHECK_FALSE(comment.has_config);
    CHECK(blank.ok);
    CHECK_FALSE(blank.has_config);
    REQUIRE(with_args.ok);
    REQUIRE(with_args.has_config);
    CHECK(with_args.config.name == "ntest8");
    CHECK(with_args.config.depth == 8);
    CHECK_FALSE(with_args.config.cwd.has_value());
    REQUIRE(with_args.config.command.size() == 2);
    CHECK(with_args.config.command[0] == "ntest");
    CHECK(with_args.config.command[1] == "x");

    CHECK_FALSE(runner::parse_engine_config_line("bad|0||cmd").ok);
    CHECK_FALSE(runner::parse_engine_config_line("bad|1|cwd|").ok);
    CHECK_FALSE(runner::parse_engine_config_line("bad|1|cwd").ok);
}

TEST_CASE("Opening parser accepts initial positions", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("initial:");

    REQUIRE(result.ok);
    REQUIRE(result.has_opening);
    CHECK(result.opening.name == "initial");
    CHECK(result.opening.moves.empty());
    CHECK(same_board(result.opening.start_board, othello::Board::initial()));
}

TEST_CASE("Opening parser accepts legal move sequences", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("c4-c3: c4 c3");

    REQUIRE(result.ok);
    REQUIRE(result.has_opening);
    CHECK(result.opening.name == "c4-c3");
    REQUIRE(result.opening.moves.size() == 2);
    CHECK(result.opening.moves[0] == "c4");
    CHECK(result.opening.moves[1] == "c3");
    CHECK_FALSE(same_board(result.opening.start_board, othello::Board::initial()));
    CHECK(result.opening.start_board.side_to_move == othello::Side::Black);
}

TEST_CASE("Opening parser rejects invalid coordinates", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("bad: z9");

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("Opening parser rejects illegal moves", "[match-runner]") {
    const runner::OpeningParseResult result = runner::parse_opening_line("bad: a1");

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("First versus first reaches a legal terminal result", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    REQUIRE(first.has_value());

    const runner::GameRecord record = runner::run_game(0, *first, *first, true, 7);

    CHECK_FALSE(record.illegal_or_error);
    CHECK(record.plies > 0);
    CHECK(record.black_score + record.white_score == 64);
    CHECK(record.score_diff_from_black == record.black_score - record.white_score);
    CHECK(record.score_diff_from_player_a == record.score_diff_from_black);
    CHECK(static_cast<int>(record.moves.size()) == record.plies);
}

TEST_CASE("Search player records nodes and time", "[match-runner]") {
    const auto search = runner::parse_player_spec("search:depth=1,tt=on,pvs=on,exact=off");
    const auto first = runner::parse_player_spec("first");
    REQUIRE(search.has_value());
    REQUIRE(first.has_value());

    const runner::GameRecord record = runner::run_game(0, *search, *first, true, 7);

    CHECK_FALSE(record.illegal_or_error);
    CHECK(record.black_score + record.white_score == 64);
    CHECK(record.nodes_black > 0);
    CHECK(record.nodes_white == 0);
    CHECK(record.nodes_player_a == record.nodes_black);
    CHECK(record.nodes_player_b == record.nodes_white);
    CHECK(record.exact_roots_black == 0);
    CHECK(record.exact_roots_white == 0);
    CHECK(record.exact_roots_player_a == record.exact_roots_black);
    CHECK(record.exact_roots_player_b == record.exact_roots_white);
    CHECK(record.time_ms_black >= 0.0);
    CHECK(record.time_ms_white == 0.0);
    CHECK(record.time_ms_player_a == record.time_ms_black);
    CHECK(record.time_ms_player_b == record.time_ms_white);
}

TEST_CASE("Search player records exact root searches", "[match-runner]") {
    const auto search = runner::parse_player_spec("search:depth=1,tt=on,pvs=on,exact=1");
    const auto first = runner::parse_player_spec("first");
    REQUIRE(search.has_value());
    REQUIRE(first.has_value());

    const runner::Opening opening{
        .name = "one-empty",
        .moves = {},
        .start_board = one_empty_forced_board(),
    };
    const runner::GameRecord record = runner::run_game(0, *search, *first, true, 7, 0, opening);

    CHECK_FALSE(record.illegal_or_error);
    CHECK(record.exact_roots_black == 1);
    CHECK(record.exact_roots_white == 0);
    CHECK(record.exact_roots_player_a == record.exact_roots_black);
    CHECK(record.exact_roots_player_b == record.exact_roots_white);
    REQUIRE(record.exact_root_events.size() == 1);
    const runner::ExactRootTrace& event = record.exact_root_events.front();
    CHECK(event.ply == 0);
    CHECK(event.side == "black");
    CHECK(event.player == "A");
    CHECK(event.board == othello::to_string(opening.start_board));
    CHECK(event.empties == 1);
    CHECK(event.legal_moves_current ==
          static_cast<int>(std::popcount(othello::legal_moves(opening.start_board))));
    CHECK(event.legal_moves_opponent == 0);
    CHECK(event.best_move == othello::test::square("h1"));
    CHECK(event.depth == 1);
    CHECK(event.nodes > 0);
    CHECK(event.stats.nodes == event.nodes);
    REQUIRE_FALSE(event.principal_variation.empty());
    CHECK(event.principal_variation.front() == *event.best_move);
}

TEST_CASE("Non-search players record zero search nodes", "[match-runner]") {
    const auto eval = runner::parse_player_spec("eval");
    const auto first = runner::parse_player_spec("first");
    REQUIRE(eval.has_value());
    REQUIRE(first.has_value());

    const runner::GameRecord record = runner::run_game(0, *eval, *first, true, 7);

    CHECK_FALSE(record.illegal_or_error);
    CHECK(record.nodes_black == 0);
    CHECK(record.nodes_white == 0);
    CHECK(record.nodes_player_a == 0);
    CHECK(record.nodes_player_b == 0);
    CHECK(record.exact_roots_black == 0);
    CHECK(record.exact_roots_white == 0);
    CHECK(record.exact_roots_player_a == 0);
    CHECK(record.exact_roots_player_b == 0);
    CHECK(record.time_ms_black == 0.0);
    CHECK(record.time_ms_white == 0.0);
    CHECK(record.time_ms_player_a == 0.0);
    CHECK(record.time_ms_player_b == 0.0);
}

TEST_CASE("Random versus random is reproducible with a fixed seed", "[match-runner]") {
    const auto random = runner::parse_player_spec("random");
    REQUIRE(random.has_value());

    const runner::GameRecord first_run = runner::run_game(0, *random, *random, true, 1234);
    const runner::GameRecord second_run = runner::run_game(0, *random, *random, true, 1234);

    CHECK(first_run == second_run);
    CHECK_FALSE(first_run.illegal_or_error);
    CHECK(first_run.black_score + first_run.white_score == 64);
}

TEST_CASE("Swap sides alternates black and white specs", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    const auto random = runner::parse_player_spec("random");
    REQUIRE(first.has_value());
    REQUIRE(random.has_value());

    const runner::MatchConfig config{
        .player_a = *first,
        .player_b = *random,
        .games = 2,
        .swap_sides = true,
        .seed = 99,
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 2);
    CHECK(records[0].black_spec == "first");
    CHECK(records[0].white_spec == "random");
    CHECK(records[0].black_is_player_a);
    CHECK(records[0].player_a_spec == "first");
    CHECK(records[0].player_b_spec == "random");
    CHECK(records[0].score_diff_from_player_a == records[0].score_diff_from_black);
    CHECK(records[1].black_spec == "random");
    CHECK(records[1].white_spec == "first");
    CHECK_FALSE(records[1].black_is_player_a);
    CHECK(records[1].player_a_spec == "first");
    CHECK(records[1].player_b_spec == "random");
    CHECK(records[1].score_diff_from_player_a == -records[1].score_diff_from_black);
}

TEST_CASE("Swap sides maps search stats to player A and B", "[match-runner]") {
    const auto search = runner::parse_player_spec("search:depth=1,exact=off");
    const auto first = runner::parse_player_spec("first");
    REQUIRE(search.has_value());
    REQUIRE(first.has_value());

    const runner::MatchConfig config{
        .player_a = *search,
        .player_b = *first,
        .games = 2,
        .swap_sides = true,
        .seed = 11,
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 2);
    CHECK(records[0].black_is_player_a);
    CHECK(records[0].nodes_player_a == records[0].nodes_black);
    CHECK(records[0].nodes_player_b == records[0].nodes_white);
    CHECK(records[0].exact_roots_player_a == records[0].exact_roots_black);
    CHECK(records[0].exact_roots_player_b == records[0].exact_roots_white);
    CHECK_FALSE(records[1].black_is_player_a);
    CHECK(records[1].nodes_player_a == records[1].nodes_white);
    CHECK(records[1].nodes_player_b == records[1].nodes_black);
    CHECK(records[1].exact_roots_player_a == records[1].exact_roots_white);
    CHECK(records[1].exact_roots_player_b == records[1].exact_roots_black);
    CHECK(records[0].nodes_player_a > 0);
    CHECK(records[1].nodes_player_a > 0);
    CHECK(records[0].nodes_player_b == 0);
    CHECK(records[1].nodes_player_b == 0);
}

TEST_CASE("Swap-side match pairs games on the same opening", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    const auto random = runner::parse_player_spec("random");
    const runner::OpeningParseResult initial = runner::parse_opening_line("initial:");
    const runner::OpeningParseResult c4_c3 = runner::parse_opening_line("c4-c3: c4 c3");
    REQUIRE(first.has_value());
    REQUIRE(random.has_value());
    REQUIRE(initial.ok);
    REQUIRE(initial.has_opening);
    REQUIRE(c4_c3.ok);
    REQUIRE(c4_c3.has_opening);

    const runner::MatchConfig config{
        .player_a = *first,
        .player_b = *random,
        .games = 4,
        .swap_sides = true,
        .seed = 99,
        .openings = {initial.opening, c4_c3.opening},
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 4);
    CHECK(records[0].opening_index == 0);
    CHECK(records[1].opening_index == 0);
    CHECK(records[0].opening_name == "initial");
    CHECK(records[1].opening_name == "initial");
    CHECK(records[2].opening_index == 1);
    CHECK(records[3].opening_index == 1);
    CHECK(records[2].opening_name == "c4-c3");
    CHECK(records[3].opening_name == "c4-c3");
}

TEST_CASE("Games can start from an opening and finish legally", "[match-runner]") {
    const auto first = runner::parse_player_spec("first");
    const auto random = runner::parse_player_spec("random");
    const runner::OpeningParseResult opening = runner::parse_opening_line("c4-c3: c4 c3");
    REQUIRE(first.has_value());
    REQUIRE(random.has_value());
    REQUIRE(opening.ok);
    REQUIRE(opening.has_opening);

    const runner::GameRecord record =
        runner::run_game(0, *first, *random, true, 7, 0, opening.opening);

    CHECK_FALSE(record.illegal_or_error);
    CHECK(record.opening_index == 0);
    CHECK(record.opening_name == "c4-c3");
    REQUIRE(record.opening_moves.size() == 2);
    CHECK(record.opening_moves[0] == "c4");
    CHECK(record.opening_moves[1] == "c3");
    CHECK(record.start_board == othello::to_string(opening.opening.start_board));
    CHECK(record.black_score + record.white_score == 64);
}

TEST_CASE("Run match emits the requested number of games", "[match-runner]") {
    const auto eval = runner::parse_player_spec("eval");
    const auto first = runner::parse_player_spec("first");
    REQUIRE(eval.has_value());
    REQUIRE(first.has_value());

    const runner::MatchConfig config{
        .player_a = *eval,
        .player_b = *first,
        .games = 3,
        .swap_sides = false,
        .seed = 1,
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 3);
    for (const runner::GameRecord& record : records) {
        CHECK_FALSE(record.illegal_or_error);
        CHECK(record.black_score + record.white_score == 64);
    }
}

TEST_CASE("Match summary separates valid and error games", "[match-runner]") {
    std::vector<runner::GameRecord> records(3);
    records[0].score_diff_from_player_a = 10;
    records[1].score_diff_from_player_a = -8;
    records[1].illegal_or_error = true;
    records[2].score_diff_from_player_a = 0;

    const runner::MatchSummary summary = runner::summarize(records);

    CHECK(summary.games == 3);
    CHECK(summary.valid_games == 2);
    CHECK(summary.error_games == 1);
    CHECK(summary.player_a_wins == 1);
    CHECK(summary.player_b_wins == 0);
    CHECK(summary.draws == 1);
    CHECK(summary.average_disc_diff_from_player_a == 5.0);
}

TEST_CASE("Internal NBoard engine can be used as an external player", "[match-runner]") {
    const auto external = runner::parse_player_spec("external:local");
    const auto first = runner::parse_player_spec("first");
    REQUIRE(external.has_value());
    REQUIRE(first.has_value());

    const runner::ExternalEngineConfig engine{
        .name = "local",
        .depth = 1,
        .cwd = std::nullopt,
        .command = {OTHELLO_NBOARD_ENGINE_PATH, "--depth", "1"},
    };
    const runner::MatchConfig config{
        .player_a = *external,
        .player_b = *first,
        .games = 1,
        .swap_sides = false,
        .seed = 1,
        .external_engines = {engine},
        .external_timeout_ms = 5000,
    };

    const std::vector<runner::GameRecord> records = runner::run_match(config);

    REQUIRE(records.size() == 1);
    CHECK_FALSE(records[0].illegal_or_error);
    CHECK_FALSE(records[0].error_reason.has_value());
    CHECK(records[0].black_score + records[0].white_score == 64);
    CHECK(records[0].black_spec == "external:local");
}
