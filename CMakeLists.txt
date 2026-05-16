cmake_minimum_required(VERSION 3.20)

project(vibe_othello_engine LANGUAGES CXX)

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

add_test(NAME othello_rule_core_tests COMMAND othello_rule_core_tests)
