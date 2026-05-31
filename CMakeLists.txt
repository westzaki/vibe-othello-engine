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

othello_add_executable(othello_tests
    tests/endgame_tests.cpp
    tests/evaluation_tests.cpp
    tests/hash_tests.cpp
    tests/analyze_tests.cpp
    tests/match_runner_tests.cpp
    tests/nboard_protocol_tests.cpp
    tests/notation_tests.cpp
    tests/player_tests.cpp
    tests/reference_rules.cpp
    tests/reference_rules_tests.cpp
    tests/rules_tests.cpp
    tests/search_tests.cpp
    tests/square_tests.cpp
    $<TARGET_OBJECTS:othello_analyze_core>
    $<TARGET_OBJECTS:othello_match_runner_core>
    $<TARGET_OBJECTS:othello_nboard_protocol>
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

add_test(
    NAME othello_nboard_engine_help_includes_eval_preset
    COMMAND $<TARGET_FILE:othello_nboard_engine> --help
)
set_tests_properties(
    othello_nboard_engine_help_includes_eval_preset
    PROPERTIES PASS_REGULAR_EXPRESSION "--eval-preset"
)

add_test(
    NAME othello_nboard_engine_help_includes_exact_threshold
    COMMAND $<TARGET_FILE:othello_nboard_engine> --help
)
set_tests_properties(
    othello_nboard_engine_help_includes_exact_threshold
    PROPERTIES PASS_REGULAR_EXPRESSION "--exact-endgame-threshold"
)

add_test(
    NAME othello_search_bench_help_includes_exact_threshold_matrix
    COMMAND $<TARGET_FILE:othello_search_bench> --help
)
set_tests_properties(
    othello_search_bench_help_includes_exact_threshold_matrix
    PROPERTIES PASS_REGULAR_EXPRESSION "--exact-endgame-thresholds"
)

add_test(
    NAME othello_search_bench_help_includes_eval_config
    COMMAND $<TARGET_FILE:othello_search_bench> --help
)
set_tests_properties(
    othello_search_bench_help_includes_eval_config
    PROPERTIES PASS_REGULAR_EXPRESSION "--eval-config"
)

add_test(
    NAME othello_search_bench_help_includes_format
    COMMAND $<TARGET_FILE:othello_search_bench> --help
)
set_tests_properties(
    othello_search_bench_help_includes_format
    PROPERTIES PASS_REGULAR_EXPRESSION "--format"
)

add_test(
    NAME othello_search_bench_jsonl_includes_score_kind
    COMMAND $<TARGET_FILE:othello_search_bench>
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

add_test(
    NAME othello_analyze_position_help_includes_eval_config
    COMMAND $<TARGET_FILE:othello_analyze_position> --help
)
set_tests_properties(
    othello_analyze_position_help_includes_eval_config
    PROPERTIES PASS_REGULAR_EXPRESSION "--eval-config"
)

set(OTHELLO_SAMPLE_EVAL_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/data/eval/current_default.eval"
)
set(OTHELLO_EXPECT_FAILURE_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/expect_failure.cmake"
)

function(othello_add_expect_failure_test name target pattern)
    string(JOIN "|" OTHELLO_EXPECT_FAILURE_ARGS ${ARGN})
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            "-DEXPECT_FAILURE_COMMAND=$<TARGET_FILE:${target}>"
            "-DEXPECT_FAILURE_ARGS=${OTHELLO_EXPECT_FAILURE_ARGS}"
            "-DEXPECT_FAILURE_PATTERN=${pattern}"
            -P "${OTHELLO_EXPECT_FAILURE_SCRIPT}"
    )
endfunction()

othello_add_expect_failure_test(
    othello_search_bench_rejects_eval_preset_with_eval_config
    othello_search_bench
    "cannot combine --eval-preset and --eval-config"
    --eval-preset default
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

othello_add_expect_failure_test(
    othello_search_bench_rejects_invalid_format
    othello_search_bench
    "invalid --format value"
    --format yaml
)

othello_add_expect_failure_test(
    othello_analyze_position_rejects_eval_preset_with_eval_config
    othello_analyze_position
    "cannot combine --eval-preset and --eval-config"
    --stdin
    --eval-preset default
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

add_test(
    NAME othello_nboard_engine_help_includes_eval_config
    COMMAND $<TARGET_FILE:othello_nboard_engine> --help
)
set_tests_properties(
    othello_nboard_engine_help_includes_eval_config
    PROPERTIES PASS_REGULAR_EXPRESSION "--eval-config"
)

othello_add_expect_failure_test(
    othello_nboard_engine_rejects_eval_preset_with_eval_config
    othello_nboard_engine
    "cannot combine --eval-preset and --eval-config"
    --eval-preset default
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

add_test(
    NAME othello_endgame_bench_help_includes_exact_tt_entries
    COMMAND $<TARGET_FILE:othello_endgame_bench> --help
)
set_tests_properties(
    othello_endgame_bench_help_includes_exact_tt_entries
    PROPERTIES PASS_REGULAR_EXPRESSION "--exact-tt-entries"
)

add_test(
    NAME othello_endgame_bench_help_includes_format
    COMMAND $<TARGET_FILE:othello_endgame_bench> --help
)
set_tests_properties(
    othello_endgame_bench_help_includes_format
    PROPERTIES PASS_REGULAR_EXPRESSION "--format"
)

add_test(
    NAME othello_endgame_bench_jsonl_includes_disc_margin
    COMMAND $<TARGET_FILE:othello_endgame_bench>
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
