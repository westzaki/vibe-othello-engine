cmake_minimum_required(VERSION 3.20)

project(vibe_othello_engine LANGUAGES CXX)

option(OTHELLO_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
option(OTHELLO_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(OTHELLO_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "OTHELLO_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()

    # Third-party FetchContent headers are intentionally excluded from clang-tidy diagnostics.
    set(OTHELLO_CLANG_TIDY_COMMAND
        "${CLANG_TIDY_EXE}"
        "--header-filter=^${CMAKE_CURRENT_SOURCE_DIR}/(include|src|tests|tools)/"
    )
endif()

if(OTHELLO_ENABLE_SANITIZERS AND NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    message(FATAL_ERROR "OTHELLO_ENABLE_SANITIZERS is ON, but the compiler is not Clang or GCC")
endif()

function(othello_enable_sanitizers target)
    if(NOT OTHELLO_ENABLE_SANITIZERS)
        return()
    endif()

    set(sanitizer_options
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )

    target_compile_options(${target} PRIVATE ${sanitizer_options})

    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "STATIC_LIBRARY")
        target_link_options(${target} PRIVATE ${sanitizer_options})
    endif()
endfunction()

function(othello_add_executable target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE othello)
    set_target_properties(${target}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    othello_enable_sanitizers(${target})

    list(APPEND OTHELLO_CLANG_TIDY_TARGETS ${target})
    set(OTHELLO_CLANG_TIDY_TARGETS ${OTHELLO_CLANG_TIDY_TARGETS} PARENT_SCOPE)
endfunction()

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_library(othello STATIC
    src/endgame.cpp
    src/evaluation.cpp
    src/evaluation_features.cpp
    src/evaluation_patterns.cpp
    src/hash.cpp
    src/notation.cpp
    src/player.cpp
    src/rules.cpp
    src/search.cpp
    src/square.cpp
)

target_compile_features(othello PUBLIC cxx_std_23)
target_include_directories(othello
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
othello_enable_sanitizers(othello)
set(OTHELLO_CLANG_TIDY_TARGETS othello)

add_subdirectory(tools)

enable_testing()
find_package(Python3 COMPONENTS Interpreter)

othello_add_executable(othello_tests
    tests/endgame_tests.cpp
    tests/evaluation_tests.cpp
    tests/eval_vs_exact_tests.cpp
    tests/exact_label_dump_tests.cpp
    tests/hash_tests.cpp
    tests/jsonl_tests.cpp
    tests/analyze_tests.cpp
    tests/match_runner_tests.cpp
    tests/nboard_protocol_tests.cpp
    tests/notation_tests.cpp
    tests/pattern_index_fixture_tests.cpp
    tests/player_tests.cpp
    tests/position_sampler_tests.cpp
    tests/reference_rules.cpp
    tests/reference_rules_tests.cpp
    tests/rules_tests.cpp
    tests/search_tests.cpp
    tests/square_tests.cpp
    $<TARGET_OBJECTS:othello_analyze_core>
    $<TARGET_OBJECTS:othello_eval_vs_exact_core>
    $<TARGET_OBJECTS:othello_exact_label_dump_core>
    $<TARGET_OBJECTS:othello_match_runner_core>
    $<TARGET_OBJECTS:othello_nboard_protocol>
    $<TARGET_OBJECTS:othello_tools_positions>
    $<TARGET_OBJECTS:othello_position_sampler_core>
    $<TARGET_OBJECTS:othello_tools_common>
)

target_link_libraries(othello_tests PRIVATE Catch2::Catch2WithMain)
target_include_directories(othello_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_compile_definitions(othello_tests
    PRIVATE
        OTHELLO_NBOARD_ENGINE_PATH="$<TARGET_FILE:othello_nboard_engine>"
        OTHELLO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(OTHELLO_ENABLE_CLANG_TIDY)
    set_target_properties(${OTHELLO_CLANG_TIDY_TARGETS}
        PROPERTIES
            CXX_CLANG_TIDY "${OTHELLO_CLANG_TIDY_COMMAND}"
    )
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(othello_tests)
include(cmake/OthelloToolTests.cmake)

othello_add_help_contains_test(
    othello_nboard_engine_help_includes_exact_threshold
    othello_nboard_engine
    "--exact-endgame-threshold"
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
    PROPERTIES PASS_REGULAR_EXPRESSION "\"tool\":\"othello_search_bench\".*\"score_kind\""
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
    othello_exact_label_dump_help_includes_move_scores
    othello_exact_label_dump
    "--include-move-scores"
)

othello_add_help_contains_test(
    othello_eval_vs_exact_help_includes_labels
    othello_eval_vs_exact
    "--labels"
)

othello_add_help_contains_test(
    othello_eval_vs_exact_help_includes_eval_config
    othello_eval_vs_exact
    "--eval-config"
)

othello_add_help_contains_test(
    othello_eval_vs_exact_help_includes_high_confidence_threshold
    othello_eval_vs_exact
    "--high-confidence-threshold"
)

othello_add_help_contains_test(
    othello_eval_vs_exact_help_includes_move_rank_analysis
    othello_eval_vs_exact
    "--move-rank-analysis"
)

othello_add_help_contains_test(
    othello_position_sampler_help_includes_target_empties
    othello_position_sampler
    "--target-empties"
)

othello_add_help_contains_test(
    othello_position_sampler_help_includes_seed
    othello_position_sampler
    "--seed"
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

othello_add_tool_smoke_test(
    othello_position_sampler_smoke
    othello_position_sampler
    --output "${CMAKE_CURRENT_BINARY_DIR}/position_sampler_smoke.txt"
    --count 3
    --target-empties 56,58
    --seed 20260531
)
set_tests_properties(
    othello_position_sampler_smoke
    PROPERTIES PASS_REGULAR_EXPRESSION "sampled=3"
)

othello_add_expect_failure_test(
    othello_position_sampler_rejects_invalid_target_empties
    othello_position_sampler
    "target-empties"
    --output "${CMAKE_CURRENT_BINARY_DIR}/bad_position_sample.txt"
    --count 1
    --target-empties "8,,10"
    --seed 20260531
)

othello_add_tool_smoke_test(
    othello_exact_label_dump_smoke
    othello_exact_label_dump
    --input "${CMAKE_CURRENT_SOURCE_DIR}/data/positions/exact_label_smoke.txt"
    --output "${CMAKE_CURRENT_BINARY_DIR}/exact_label_smoke.jsonl"
    --max-empties 14
    --limit 10
    --include-move-scores
)
set_tests_properties(
    othello_exact_label_dump_smoke
    PROPERTIES PASS_REGULAR_EXPRESSION "labeled=3"
)

set(OTHELLO_SAMPLE_EVAL_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/data/eval/current_default.eval"
)

set(OTHELLO_TINY_EXACT_LABELS
    "${CMAKE_CURRENT_SOURCE_DIR}/data/labels/exact_label_tiny.jsonl"
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
    othello_eval_vs_exact_rejects_unknown_option
    othello_eval_vs_exact
    "unknown option: --unknown-option"
    --labels "${OTHELLO_TINY_EXACT_LABELS}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_rejects_combo.md"
    --unknown-option
)

othello_add_expect_failure_test(
    othello_eval_vs_exact_rejects_unsupported_schema
    othello_eval_vs_exact
    "unsupported schema"
    --labels "${CMAKE_CURRENT_SOURCE_DIR}/data/labels/exact_label_unsupported_schema.jsonl"
    --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_bad_schema.md"
)

othello_add_expect_failure_test(
    othello_eval_vs_exact_rejects_missing_required_field
    othello_eval_vs_exact
    "missing required field"
    --labels "${CMAKE_CURRENT_SOURCE_DIR}/data/labels/exact_label_missing_required.jsonl"
    --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_missing_required.md"
)

othello_add_expect_failure_test(
    othello_eval_vs_exact_rejects_invalid_high_confidence_threshold
    othello_eval_vs_exact
    "high-confidence-threshold must be a non-negative integer"
    --labels "${OTHELLO_TINY_EXACT_LABELS}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_bad_threshold.md"
    --high-confidence-threshold -1
)

othello_add_tool_smoke_test(
    othello_eval_vs_exact_smoke_with_default_evaluator
    othello_eval_vs_exact
    --labels "${OTHELLO_TINY_EXACT_LABELS}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_default_evaluator.md"
    --high-confidence-threshold 123
    --phase-breakdown
)
set_tests_properties(
    othello_eval_vs_exact_smoke_with_default_evaluator
    PROPERTIES PASS_REGULAR_EXPRESSION "records_read=3"
)

othello_add_tool_smoke_test(
    othello_eval_vs_exact_smoke_with_eval_config
    othello_eval_vs_exact
    --labels "${OTHELLO_TINY_EXACT_LABELS}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_current_default.md"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)
set_tests_properties(
    othello_eval_vs_exact_smoke_with_eval_config
    PROPERTIES PASS_REGULAR_EXPRESSION "analyzed=3"
)

othello_add_tool_smoke_test(
    othello_eval_vs_exact_smoke_move_rank_stdout
    othello_eval_vs_exact
    --labels "${OTHELLO_TINY_EXACT_LABELS}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_move_rank.md"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
    --move-rank-analysis
)
set_tests_properties(
    othello_eval_vs_exact_smoke_move_rank_stdout
    PROPERTIES PASS_REGULAR_EXPRESSION "move_rank_records_with_scores=1.*move_rank_records_missing_scores=2.*move_rank_analyzed=1"
)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME balanced_position_pool_py
        COMMAND "${Python3_EXECUTABLE}"
            -m unittest
            tools/scripts/tests/test_balanced_position_pool.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME pattern_teacher_v0_train_py
        COMMAND "${Python3_EXECUTABLE}"
            -m unittest
            tools/scripts/tests/test_pattern_teacher_v0_train.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME eval_candidate_matrix_dry_run
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/eval_candidate_matrix.py"
            --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
            --baseline-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
            --candidates "${OTHELLO_SAMPLE_EVAL_CONFIG}"
            --out "${CMAKE_CURRENT_BINARY_DIR}/eval_candidate_matrix_dry_run"
            --dry-run
    )
    set_tests_properties(
        eval_candidate_matrix_dry_run
        PROPERTIES PASS_REGULAR_EXPRESSION "report: .*report.md"
    )
endif()

othello_add_expect_failure_test(
    othello_analyze_position_rejects_unknown_option
    othello_analyze_position
    "unknown option: --unknown-option"
    --stdin
    --unknown-option
)

othello_add_expect_failure_test(
    othello_analyze_position_rejects_duplicate_eval_config
    othello_analyze_position
    "--eval-config may only be specified once"
    --stdin
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

othello_add_expect_failure_test(
    othello_exact_label_dump_rejects_invalid_input
    othello_exact_label_dump
    "invalid input"
    --input "${CMAKE_CURRENT_SOURCE_DIR}/data/positions/exact_label_invalid.txt"
    --output "${CMAKE_CURRENT_BINARY_DIR}/exact_label_invalid.jsonl"
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
