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

enable_testing()

othello_add_executable(othello_tests
    tests/endgame_tests.cpp
    tests/evaluation_tests.cpp
    tests/hash_tests.cpp
    tests/notation_tests.cpp
    tests/player_tests.cpp
    tests/rules_tests.cpp
    tests/search_tests.cpp
    tests/square_tests.cpp
)

target_link_libraries(othello_tests PRIVATE Catch2::Catch2WithMain)

othello_add_executable(othello_rule_core_bench
    tools/benchmarks/bench_rule_core.cpp
)

othello_add_executable(othello_search_bench
    tools/benchmarks/bench_search.cpp
)

othello_add_executable(othello_endgame_bench
    tools/benchmarks/bench_endgame.cpp
)

othello_add_executable(othello_analyze_position
    tools/analyze_position.cpp
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
