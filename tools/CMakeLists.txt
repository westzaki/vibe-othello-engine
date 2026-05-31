add_library(othello_tools_common OBJECT
    common/board_io.cpp
    common/cli.cpp
    common/eval_config_io.cpp
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
    match_runner/engine_config.cpp
    match_runner/external_player.cpp
    match_runner/opening_suite.cpp
    match_runner/player_spec.cpp
)

target_link_libraries(othello_match_runner_core PRIVATE othello)
target_include_directories(othello_match_runner_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
othello_enable_sanitizers(othello_match_runner_core)
list(APPEND OTHELLO_CLANG_TIDY_TARGETS othello_match_runner_core)

add_library(othello_nboard_protocol OBJECT
    protocols/nboard/game_codec.cpp
    protocols/nboard/parser.cpp
    protocols/nboard/protocol.cpp
    protocols/nboard/process_client.cpp
)

target_link_libraries(othello_nboard_protocol PRIVATE othello)
target_include_directories(othello_nboard_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
othello_enable_sanitizers(othello_nboard_protocol)
list(APPEND OTHELLO_CLANG_TIDY_TARGETS othello_nboard_protocol)

add_library(othello_analyze_core OBJECT
    analyze/analysis.cpp
)

target_link_libraries(othello_analyze_core PRIVATE othello)
target_include_directories(othello_analyze_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
othello_enable_sanitizers(othello_analyze_core)
list(APPEND OTHELLO_CLANG_TIDY_TARGETS othello_analyze_core)

add_library(othello_exact_label_dump_core OBJECT
    exact_labels/exact_label_dump.cpp
)

target_link_libraries(othello_exact_label_dump_core PRIVATE othello)
target_include_directories(othello_exact_label_dump_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
othello_enable_sanitizers(othello_exact_label_dump_core)
list(APPEND OTHELLO_CLANG_TIDY_TARGETS othello_exact_label_dump_core)

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
    $<TARGET_OBJECTS:othello_analyze_core>
    $<TARGET_OBJECTS:othello_tools_common>
)
target_include_directories(othello_analyze_position PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_exact_label_dump
    exact_labels/exact_label_dump_main.cpp
    $<TARGET_OBJECTS:othello_exact_label_dump_core>
    $<TARGET_OBJECTS:othello_tools_common>
)
target_include_directories(othello_exact_label_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_match_runner
    match_runner/match_runner_main.cpp
    match_runner/jsonl_writer.cpp
    $<TARGET_OBJECTS:othello_match_runner_core>
    $<TARGET_OBJECTS:othello_nboard_protocol>
    $<TARGET_OBJECTS:othello_tools_common>
)
target_include_directories(othello_match_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_probe_nboard
    protocols/nboard/probe_main.cpp
    $<TARGET_OBJECTS:othello_nboard_protocol>
    $<TARGET_OBJECTS:othello_tools_common>
)
target_include_directories(othello_probe_nboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

othello_add_executable(othello_nboard_engine
    protocols/nboard/engine_main.cpp
    $<TARGET_OBJECTS:othello_nboard_protocol>
    $<TARGET_OBJECTS:othello_tools_common>
)
target_include_directories(othello_nboard_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

set(OTHELLO_CLANG_TIDY_TARGETS ${OTHELLO_CLANG_TIDY_TARGETS} PARENT_SCOPE)
