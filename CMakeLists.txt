cmake_minimum_required(VERSION 3.20)

project(vibe_othello_engine LANGUAGES CXX)

option(OTHELLO_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)

if(OTHELLO_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(FATAL_ERROR "OTHELLO_ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
    endif()
endif()

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_library(othello STATIC
    src/board.cpp
)

target_compile_features(othello PUBLIC cxx_std_23)
target_include_directories(othello
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

enable_testing()

add_executable(othello_rule_core_tests
    tests/rule_core_tests.cpp
)

target_link_libraries(othello_rule_core_tests PRIVATE othello)
target_link_libraries(othello_rule_core_tests PRIVATE Catch2::Catch2WithMain)

if(OTHELLO_ENABLE_CLANG_TIDY)
    set_target_properties(othello othello_rule_core_tests
        PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
    )
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(othello_rule_core_tests)
