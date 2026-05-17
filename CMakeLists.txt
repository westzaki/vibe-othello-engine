cmake_minimum_required(VERSION 3.20)

project(vibe_othello_engine LANGUAGES CXX)

option(OTHELLO_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
option(OTHELLO_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(OTHELLO_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "OTHELLO_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()
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

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_library(othello STATIC
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

enable_testing()

add_executable(othello_tests
    tests/evaluation_tests.cpp
    tests/hash_tests.cpp
    tests/notation_tests.cpp
    tests/player_tests.cpp
    tests/rules_tests.cpp
    tests/search_tests.cpp
    tests/square_tests.cpp
)

target_link_libraries(othello_tests PRIVATE othello)
target_link_libraries(othello_tests PRIVATE Catch2::Catch2WithMain)
othello_enable_sanitizers(othello_tests)

add_executable(othello_rule_core_bench
    tools/benchmarks/bench_rule_core.cpp
)

target_link_libraries(othello_rule_core_bench PRIVATE othello)
othello_enable_sanitizers(othello_rule_core_bench)

add_executable(othello_search_bench
    tools/benchmarks/bench_search.cpp
)

target_link_libraries(othello_search_bench PRIVATE othello)
othello_enable_sanitizers(othello_search_bench)

add_executable(othello_analyze_position
    tools/analyze_position.cpp
)

target_link_libraries(othello_analyze_position PRIVATE othello)
othello_enable_sanitizers(othello_analyze_position)

if(OTHELLO_ENABLE_CLANG_TIDY)
    set_target_properties(othello othello_tests othello_rule_core_bench othello_search_bench
        othello_analyze_position
        PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
    )
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(othello_tests)
