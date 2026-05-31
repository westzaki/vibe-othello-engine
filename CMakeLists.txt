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
    tests/exact_label_dump_tests.cpp
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
    $<TARGET_OBJECTS:othello_exact_label_dump_core>
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
include(cmake/OthelloToolTests.cmake)

othello_add_help_contains_test(
    othello_nboard_engine_help_includes_eval_preset
    othello_nboard_engine
    "--eval-preset"
)

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
    othello_analyze_position_help_includes_eval_config
    othello_analyze_position
    "--eval-config"
)

othello_add_help_contains_test(
    othello_exact_label_dump_help_includes_move_scores
    othello_exact_label_dump
    "--include-move-scores"
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

othello_add_expect_failure_test(
    othello_search_bench_rejects_eval_preset_with_eval_config
    othello_search_bench
    "cannot combine --eval-preset and --eval-config"
    --eval-preset default
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

othello_add_expect_failure_test(
    othello_analyze_position_rejects_eval_preset_with_eval_config
    othello_analyze_position
    "cannot combine --eval-preset and --eval-config"
    --stdin
    --eval-preset default
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
    othello_nboard_engine_rejects_eval_preset_with_eval_config
    othello_nboard_engine
    "cannot combine --eval-preset and --eval-config"
    --eval-preset default
    --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
)

othello_add_help_contains_test(
    othello_endgame_bench_help_includes_exact_tt_entries
    othello_endgame_bench
    "--exact-tt-entries"
)
