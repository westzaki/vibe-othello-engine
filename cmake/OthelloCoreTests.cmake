set(OTHELLO_CORE_TEST_SOURCES
    tests/endgame_tests.cpp
    tests/eval_config_io_tests.cpp
    tests/evaluation_tests.cpp
    tests/evaluation_pattern_table_tests.cpp
    tests/hash_tests.cpp
    tests/jsonl_tests.cpp
    tests/analyze_tests.cpp
    tests/match_runner_tests.cpp
    tests/nboard_protocol_tests.cpp
    tests/notation_tests.cpp
    tests/pattern_table_io_tests.cpp
    tests/pattern_index_fixture_tests.cpp
    tests/player_tests.cpp
    tests/reference_rules.cpp
    tests/reference_rules_tests.cpp
    tests/replay_tests.cpp
    tests/rules_tests.cpp
    tests/search_tests.cpp
    tests/square_tests.cpp
    $<TARGET_OBJECTS:othello_analyze_core>
    $<TARGET_OBJECTS:othello_match_runner_core>
    $<TARGET_OBJECTS:othello_nboard_protocol>
    $<TARGET_OBJECTS:othello_tools_positions>
    $<TARGET_OBJECTS:othello_replay_core>
    $<TARGET_OBJECTS:othello_tools_common>
)

othello_add_executable(othello_tests ${OTHELLO_CORE_TEST_SOURCES})
target_link_libraries(othello_tests PRIVATE Catch2::Catch2WithMain)
target_include_directories(othello_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_compile_definitions(othello_tests
    PRIVATE
        OTHELLO_NBOARD_ENGINE_PATH="$<TARGET_FILE:othello_nboard_engine>"
        OTHELLO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

catch_discover_tests(othello_tests PROPERTIES LABELS core)

othello_add_help_contains_test(
    othello_nboard_engine_help_includes_exact_threshold
    othello_nboard_engine
    "--exact-endgame-threshold"
)

othello_add_help_contains_test(
    othello_nboard_engine_help_includes_preset
    othello_nboard_engine
    "--preset"
)

othello_add_help_contains_test(
    othello_match_runner_help_includes_strong_v1_preset
    othello_match_runner
    "preset=default|strong-v1"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_exact_threshold_matrix
    othello_search_bench
    "--exact-endgame-thresholds"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_eval_config
    othello_search_bench
    "--eval-config"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_format
    othello_search_bench
    "--format"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_tt_store_leaf
    othello_search_bench
    "--tt-store-leaf"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_tt_min_probe_depth
    othello_search_bench
    "--tt-min-probe-depth"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_tt_min_store_depth
    othello_search_bench
    "--tt-min-store-depth"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_lazy_first_move_ordering
    othello_search_bench
    "--lazy-first-move-ordering"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_iterative_depth_rows
    othello_search_bench
    "--emit-iterative-depth-rows"
)

othello_add_help_contains_test(
    othello_search_bench_help_includes_aspiration_profile
    othello_search_bench
    "--aspiration-profile"
)

othello_add_tool_smoke_test(
    othello_search_bench_jsonl_includes_score_kind
    othello_search_bench
    --mode fixed
    --depths 1
    --positions smoke
    --repetitions 1
    --exact-endgame-threshold 0
    --format jsonl
)
set_tests_properties(
    othello_search_bench_jsonl_includes_score_kind
    PROPERTIES PASS_REGULAR_EXPRESSION "\"tool\":\"othello_search_bench\".*\"aspiration_profile\":\"fixed\".*\"score_kind\""
)

othello_add_tool_smoke_test(
    othello_search_bench_jsonl_score_delta_aware_aspiration_profile
    othello_search_bench
    --mode iterative
    --depths 2
    --positions smoke
    --repetitions 1
    --tt on
    --tt-min-probe-depth 1
    --tt-min-store-depth 1
    --lazy-first-move-ordering on
    --pvs on
    --aspiration on
    --aspiration-profile score-delta-aware
    --exact-endgame-threshold 0
    --format jsonl
)
set_tests_properties(
    othello_search_bench_jsonl_score_delta_aware_aspiration_profile
    PROPERTIES PASS_REGULAR_EXPRESSION "\"tt_min_probe_depth\":1.*\"tt_min_store_depth\":1.*\"lazy_first_move_ordering\":true.*\"aspiration_profile\":\"score-delta-aware\".*\"tt_probe_skipped_by_depth\".*\"ordering_full_builds\""
)

othello_add_tool_smoke_test(
    othello_search_bench_jsonl_iterative_depth_rows
    othello_search_bench
    --mode iterative
    --depths 2
    --positions smoke
    --repetitions 1
    --tt on
    --pvs on
    --aspiration on
    --exact-endgame-threshold 0
    --emit-iterative-depth-rows
    --format jsonl
)
set_tests_properties(
    othello_search_bench_jsonl_iterative_depth_rows
    PROPERTIES PASS_REGULAR_EXPRESSION "\"row\":\"iterative_depth\".*\"completed_depth\".*\"previous_score_delta\".*\"pvs_research_ratio\""
)

othello_add_tool_smoke_test(
    othello_search_bench_jsonl_root_ordering_diagnostics
    othello_search_bench
    --mode iterative
    --depths 2
    --positions smoke
    --repetitions 1
    --by-position
    --tt on
    --pvs on
    --aspiration on
    --exact-endgame-threshold 0
    --emit-iterative-depth-rows
    --format jsonl
)
set_tests_properties(
    othello_search_bench_jsonl_root_ordering_diagnostics
    PROPERTIES PASS_REGULAR_EXPRESSION "\"row\":\"position\".*\"root_move_count\".*\"best_move_initial_order_rank\".*\"ordered_root_moves\""
)

othello_add_tool_smoke_test(
    othello_search_bench_evaluation_positions_describe
    othello_search_bench
    --positions evaluation
    --describe-positions
)
set_tests_properties(
    othello_search_bench_evaluation_positions_describe
    PROPERTIES PASS_REGULAR_EXPRESSION "eval-late-dense-mobility"
)

othello_add_tool_smoke_test(
    othello_search_bench_evaluation_positions_jsonl
    othello_search_bench
    --positions evaluation
    --mode iterative
    --depths 1
    --repetitions 1
    --tt on
    --pvs on
    --exact-endgame-threshold 0
    --format jsonl
)
set_tests_properties(
    othello_search_bench_evaluation_positions_jsonl
    PROPERTIES PASS_REGULAR_EXPRESSION "\"positions\":\"evaluation\".*\"position_count\":8"
)

othello_add_help_contains_test(
    othello_analyze_position_help_includes_eval_config
    othello_analyze_position
    "--eval-config"
)

othello_add_help_contains_test(
    othello_analyze_position_help_includes_tt_store_leaf
    othello_analyze_position
    "--tt-store-leaf"
)

othello_add_help_contains_test(
    othello_analyze_position_help_includes_tt_min_probe_depth
    othello_analyze_position
    "--tt-min-probe-depth"
)

othello_add_help_contains_test(
    othello_analyze_position_help_includes_tt_min_store_depth
    othello_analyze_position
    "--tt-min-store-depth"
)

othello_add_help_contains_test(
    othello_analyze_position_help_includes_lazy_first_move_ordering
    othello_analyze_position
    "--lazy-first-move-ordering"
)

othello_add_help_contains_test(
    othello_analyze_position_help_includes_aspiration_profile
    othello_analyze_position
    "--aspiration-profile"
)

othello_add_help_contains_test(
    othello_match_runner_help_includes_tt_store_leaf
    othello_match_runner
    "tt_store_leaf=on|off"
)

othello_add_help_contains_test(
    othello_match_runner_help_includes_tt_min_probe_depth
    othello_match_runner
    "tt_min_probe_depth=N"
)

othello_add_help_contains_test(
    othello_match_runner_help_includes_tt_min_store_depth
    othello_match_runner
    "tt_min_store_depth=N"
)

othello_add_help_contains_test(
    othello_match_runner_help_includes_lazy_first_move_ordering
    othello_match_runner
    "lazy_first_move_ordering=on|off"
)

othello_add_help_contains_test(
    othello_match_runner_help_includes_aspiration_profile
    othello_match_runner
    "aspiration_profile=fixed|score-delta-aware"
)

othello_add_help_contains_test(
    othello_replay_game_help_includes_match_jsonl
    othello_replay_game
    "--match-jsonl"
)

othello_add_help_contains_test(
    othello_validate_move_help_includes_move
    othello_validate_move
    "--move"
)

othello_add_tool_smoke_test(
    othello_validate_move_accepts_legal_move
    othello_validate_move
    --board-file
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/fixtures/pr115_divergence_board.txt"
    --move
    a1
)
set_tests_properties(
    othello_validate_move_accepts_legal_move
    PROPERTIES PASS_REGULAR_EXPRESSION "legal_move_valid=true"
)

othello_add_expect_failure_test(
    othello_validate_move_rejects_illegal_move
    othello_validate_move
    "legal_move_valid=false"
    --board-file
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/fixtures/pr115_divergence_board.txt"
    --move
    h8
)

set(OTHELLO_SAMPLE_EVAL_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/data/eval/current_default.eval"
)

othello_add_tool_smoke_test(
    othello_search_bench_smoke_with_default_evaluator
    othello_search_bench
    --mode fixed
    --depths 1
    --positions smoke
    --repetitions 1
    --exact-endgame-threshold 0
    --format jsonl
)
set_tests_properties(
    othello_search_bench_smoke_with_default_evaluator
    PROPERTIES PASS_REGULAR_EXPRESSION "\"positions\":\"smoke\".*\"position_count\""
)

othello_add_tool_smoke_test(
    othello_search_bench_smoke_with_eval_config
    othello_search_bench
    --mode fixed
    --depths 1
    --positions smoke
    --repetitions 1
    --exact-endgame-threshold 0
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
    --format jsonl
)
set_tests_properties(
    othello_search_bench_smoke_with_eval_config
    PROPERTIES PASS_REGULAR_EXPRESSION "\"positions\":\"smoke\".*\"position_count\""
)

othello_add_expect_failure_test(
    othello_search_bench_rejects_unknown_option
    othello_search_bench
    "unknown option: --unknown-option"
    --unknown-option
)

othello_add_expect_failure_test(
    othello_search_bench_rejects_duplicate_eval_config
    othello_search_bench
    "--eval-config may only be specified once"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

othello_add_expect_failure_test(
    othello_search_bench_rejects_invalid_format
    othello_search_bench
    "invalid --format value"
    --format yaml
)

othello_add_expect_failure_test(
    othello_search_bench_rejects_invalid_aspiration_profile
    othello_search_bench
    "--aspiration-profile must be fixed or score-delta-aware"
    --aspiration-profile wide
)

othello_add_expect_failure_test(
    othello_analyze_position_rejects_unknown_option
    othello_analyze_position
    "unknown option: --unknown-option"
    --stdin
    --unknown-option
)

othello_add_expect_failure_test(
    othello_analyze_position_rejects_invalid_aspiration_profile
    othello_analyze_position
    "invalid --aspiration-profile value"
    --stdin
    --aspiration-profile wide
)

othello_add_expect_failure_test(
    othello_analyze_position_rejects_duplicate_eval_config
    othello_analyze_position
    "--eval-config may only be specified once"
    --stdin
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

othello_add_help_contains_test(
    othello_nboard_engine_help_includes_eval_config
    othello_nboard_engine
    "--eval-config"
)

othello_add_expect_failure_test(
    othello_nboard_engine_rejects_unknown_option
    othello_nboard_engine
    "unknown option: --unknown-option"
    --unknown-option
)

othello_add_expect_failure_test(
    othello_nboard_engine_rejects_duplicate_eval_config
    othello_nboard_engine
    "--eval-config may only be specified once"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

othello_add_help_contains_test(
    othello_endgame_bench_help_includes_exact_tt_entries
    othello_endgame_bench
    "--exact-tt-entries"
)

othello_add_help_contains_test(
    othello_endgame_bench_help_includes_format
    othello_endgame_bench
    "--format"
)

othello_add_help_contains_test(
    othello_rule_core_bench_help_includes_format
    othello_rule_core_bench
    "--format"
)

othello_add_help_contains_test(
    othello_rule_core_bench_help_includes_perft_depth
    othello_rule_core_bench
    "--perft-depth"
)

othello_add_tool_smoke_test(
    othello_rule_core_bench_jsonl_includes_perft
    othello_rule_core_bench
    --positions smoke
    --iterations 1
    --perft-depth 2
    --format jsonl
)
set_tests_properties(
    othello_rule_core_bench_jsonl_includes_perft
    PROPERTIES PASS_REGULAR_EXPRESSION "\"tool\":\"othello_rule_core_bench\".*\"operation\":\"perft\".*\"nodes\""
)

othello_add_tool_smoke_test(
    othello_endgame_bench_jsonl_includes_disc_margin
    othello_endgame_bench
    --positions smoke
    --repetitions 1
    --format jsonl
)
set_tests_properties(
    othello_endgame_bench_jsonl_includes_disc_margin
    PROPERTIES PASS_REGULAR_EXPRESSION "\"tool\":\"othello_endgame_bench\".*\"disc_margin\""
)

othello_add_expect_failure_test(
    othello_endgame_bench_rejects_invalid_format
    othello_endgame_bench
    "invalid --format value"
    --format yaml
)
