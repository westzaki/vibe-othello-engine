add_library(othello_tools_common OBJECT
    common/board_io.cpp
    common/cli.cpp
    common/formatting.cpp
    common/jsonl.cpp
    common/stats.cpp
)

target_link_libraries(othello_tools_common PRIVATE othello)
target_include_directories(othello_tools_common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
othello_enable_sanitizers(othello_tools_common)
list(APPEND OTHELLO_CLANG_TIDY_TARGETS othello_tools_common)

add_library(othello_tools_positions OBJECT
    positions/endgame_fixtures.cpp
    positions/fixtures.cpp
    positions/metrics.cpp
    positions/tags.cpp
)

target_link_libraries(othello_tools_positions PRIVATE othello)
target_include_directories(othello_tools_positions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
othello_enable_sanitizers(othello_tools_positions)
list(APPEND OTHELLO_CLANG_TIDY_TARGETS othello_tools_positions)

add_library(othello_match_runner_core OBJECT
    match_runner/core.cpp
    match_runner/opening_suite.cpp
    match_runner/player_spec.cpp
)

target_link_libraries(othello_match_runner_core PRIVATE othello)
target_include_directories(othello_match_runner_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
othello_enable_sanitizers(othello_match_runner_core)
list(APPEND OTHELLO_CLANG_TIDY_TARGETS othello_match_runner_core)

othello_add_executable(othello_rule_core_bench
    benchmarks/rule_core_main.cpp
    benchmarks/reporters.cpp
    $<TARGET_OBJECTS:othello_tools_common>
    $<TARGET_OBJECTS:othello_tools_positions>
)
target_include_directories(othello_rule_core_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_search_bench
    benchmarks/search_main.cpp
    benchmarks/reporters.cpp
    $<TARGET_OBJECTS:othello_tools_common>
    $<TARGET_OBJECTS:othello_tools_positions>
)
target_include_directories(othello_search_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_endgame_bench
    benchmarks/endgame_main.cpp
    benchmarks/reporters.cpp
    $<TARGET_OBJECTS:othello_tools_common>
    $<TARGET_OBJECTS:othello_tools_positions>
)
target_include_directories(othello_endgame_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_analyze_position
    analyze/analyze_position_main.cpp
    analyze/analysis.cpp
    $<TARGET_OBJECTS:othello_tools_common>
)
target_include_directories(othello_analyze_position PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_match_runner
    match_runner/match_runner_main.cpp
    match_runner/jsonl_writer.cpp
    $<TARGET_OBJECTS:othello_match_runner_core>
    $<TARGET_OBJECTS:othello_tools_common>
)
target_include_directories(othello_match_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(OTHELLO_CLANG_TIDY_TARGETS ${OTHELLO_CLANG_TIDY_TARGETS} PARENT_SCOPE)
