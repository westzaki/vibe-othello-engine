cmake_minimum_required(VERSION 3.20)

project(vibe_othello_engine LANGUAGES CXX)

option(OTHELLO_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
option(OTHELLO_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(OTHELLO_BUILD_TOOLS "Build developer tools" ON)
option(OTHELLO_BUILD_TESTS "Build tests" ON)
option(OTHELLO_BUILD_EXPERIMENT_TOOLS "Build research/label/training helper tools" ON)

if(OTHELLO_BUILD_TESTS AND NOT OTHELLO_BUILD_TOOLS)
    message(FATAL_ERROR "OTHELLO_BUILD_TESTS requires OTHELLO_BUILD_TOOLS because the test suite covers tool cores")
endif()

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

if(OTHELLO_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

add_library(othello STATIC
    src/endgame.cpp
    src/evaluation.cpp
    src/evaluation_features.cpp
    src/evaluation_patterns.cpp
    src/hash.cpp
    src/notation.cpp
    src/player.cpp
    src/rules.cpp
    src/search_aspiration.cpp
    src/search.cpp
    src/search_root_policy.cpp
    src/search_session.cpp
    src/square.cpp
)

target_compile_features(othello PUBLIC cxx_std_23)
target_include_directories(othello
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
othello_enable_sanitizers(othello)
set(OTHELLO_CLANG_TIDY_TARGETS othello)

if(OTHELLO_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(OTHELLO_BUILD_TESTS)
    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    include(cmake/OthelloToolTests.cmake)
    include(cmake/OthelloCoreTests.cmake)
    include(cmake/OthelloExperimentTests.cmake)
endif()

if(OTHELLO_ENABLE_CLANG_TIDY)
    set_target_properties(${OTHELLO_CLANG_TIDY_TARGETS}
        PROPERTIES
            CXX_CLANG_TIDY "${OTHELLO_CLANG_TIDY_COMMAND}"
    )
endif()
