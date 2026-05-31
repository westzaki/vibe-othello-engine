function(othello_add_tool_smoke_test name target)
    add_test(
        NAME ${name}
        COMMAND $<TARGET_FILE:${target}> ${ARGN}
    )
endfunction()

function(othello_add_help_contains_test name target pattern)
    othello_add_tool_smoke_test(${name} ${target} --help)
    set_tests_properties(
        ${name}
        PROPERTIES PASS_REGULAR_EXPRESSION "${pattern}"
    )
endfunction()

function(othello_add_expect_failure_test name target pattern)
    set(expect_failure_script
        "${PROJECT_SOURCE_DIR}/tools/cmake/expect_failure.cmake"
    )
    string(JOIN "|" expect_failure_args ${ARGN})
    add_test(
        NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            "-DEXPECT_FAILURE_COMMAND=$<TARGET_FILE:${target}>"
            "-DEXPECT_FAILURE_ARGS=${expect_failure_args}"
            "-DEXPECT_FAILURE_PATTERN=${pattern}"
            -P "${expect_failure_script}"
    )
endfunction()
