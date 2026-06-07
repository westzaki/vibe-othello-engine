set(OTHELLO_SAMPLE_EVAL_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/data/eval/current_default.eval"
)

set(OTHELLO_TINY_EXACT_LABELS
    "${CMAKE_CURRENT_SOURCE_DIR}/data/labels/exact_label_tiny.jsonl"
)

if(OTHELLO_BUILD_EXPERIMENT_TOOLS)
    othello_add_executable(othello_experiment_tests
        tests/eval_vs_exact_tests.cpp
        tests/exact_label_dump_tests.cpp
        tests/position_sampler_tests.cpp
        $<TARGET_OBJECTS:othello_eval_vs_exact_core>
        $<TARGET_OBJECTS:othello_exact_label_dump_core>
        $<TARGET_OBJECTS:othello_position_sampler_core>
        $<TARGET_OBJECTS:othello_tools_common>
    )

    target_link_libraries(othello_experiment_tests PRIVATE Catch2::Catch2WithMain)
    target_include_directories(othello_experiment_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
    target_compile_definitions(othello_experiment_tests
        PRIVATE
            OTHELLO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    catch_discover_tests(othello_experiment_tests PROPERTIES LABELS experiment)
endif()

if(OTHELLO_BUILD_EXPERIMENT_TOOLS)
    othello_add_help_contains_test(
        othello_exact_label_dump_help_includes_move_scores
        othello_exact_label_dump
        "--include-move-scores"
    )

    othello_add_help_contains_test(
        othello_eval_vs_exact_help_includes_labels
        othello_eval_vs_exact
        "--labels"
    )

    othello_add_help_contains_test(
        othello_eval_vs_exact_help_includes_eval_config
        othello_eval_vs_exact
        "--eval-config"
    )

    othello_add_help_contains_test(
        othello_eval_vs_exact_help_includes_high_confidence_threshold
        othello_eval_vs_exact
        "--high-confidence-threshold"
    )

    othello_add_help_contains_test(
        othello_eval_vs_exact_help_includes_move_rank_analysis
        othello_eval_vs_exact
        "--move-rank-analysis"
    )

    othello_add_help_contains_test(
        othello_position_sampler_help_includes_target_empties
        othello_position_sampler
        "--target-empties"
    )

    othello_add_help_contains_test(
        othello_position_sampler_help_includes_seed
        othello_position_sampler
        "--seed"
    )

    othello_add_tool_smoke_test(
        othello_position_sampler_smoke
        othello_position_sampler
        --output "${CMAKE_CURRENT_BINARY_DIR}/position_sampler_smoke.txt"
        --count 3
        --target-empties 56,58
        --seed 20260531
    )
    set_tests_properties(
        othello_position_sampler_smoke
        PROPERTIES PASS_REGULAR_EXPRESSION "sampled=3"
    )

    othello_add_expect_failure_test(
        othello_position_sampler_rejects_invalid_target_empties
        othello_position_sampler
        "target-empties"
        --output "${CMAKE_CURRENT_BINARY_DIR}/bad_position_sample.txt"
        --count 1
        --target-empties "8,,10"
        --seed 20260531
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

    othello_add_expect_failure_test(
        othello_eval_vs_exact_rejects_unknown_option
        othello_eval_vs_exact
        "unknown option: --unknown-option"
        --labels "${OTHELLO_TINY_EXACT_LABELS}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_rejects_combo.md"
        --unknown-option
    )

    othello_add_expect_failure_test(
        othello_eval_vs_exact_rejects_unsupported_schema
        othello_eval_vs_exact
        "unsupported schema"
        --labels "${CMAKE_CURRENT_SOURCE_DIR}/data/labels/exact_label_unsupported_schema.jsonl"
        --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_bad_schema.md"
    )

    othello_add_expect_failure_test(
        othello_eval_vs_exact_rejects_missing_required_field
        othello_eval_vs_exact
        "missing required field"
        --labels "${CMAKE_CURRENT_SOURCE_DIR}/data/labels/exact_label_missing_required.jsonl"
        --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_missing_required.md"
    )

    othello_add_expect_failure_test(
        othello_eval_vs_exact_rejects_invalid_high_confidence_threshold
        othello_eval_vs_exact
        "high-confidence-threshold must be a non-negative integer"
        --labels "${OTHELLO_TINY_EXACT_LABELS}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_bad_threshold.md"
        --high-confidence-threshold -1
    )

    othello_add_tool_smoke_test(
        othello_eval_vs_exact_smoke_with_default_evaluator
        othello_eval_vs_exact
        --labels "${OTHELLO_TINY_EXACT_LABELS}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_default_evaluator.md"
        --high-confidence-threshold 123
        --phase-breakdown
    )
    set_tests_properties(
        othello_eval_vs_exact_smoke_with_default_evaluator
        PROPERTIES PASS_REGULAR_EXPRESSION "records_read=3"
    )

    othello_add_tool_smoke_test(
        othello_eval_vs_exact_smoke_with_eval_config
        othello_eval_vs_exact
        --labels "${OTHELLO_TINY_EXACT_LABELS}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_current_default.md"
        --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
    )
    set_tests_properties(
        othello_eval_vs_exact_smoke_with_eval_config
        PROPERTIES PASS_REGULAR_EXPRESSION "analyzed=3"
    )

    othello_add_tool_smoke_test(
        othello_eval_vs_exact_smoke_move_rank_stdout
        othello_eval_vs_exact
        --labels "${OTHELLO_TINY_EXACT_LABELS}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/eval_vs_exact_move_rank.md"
        --eval-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
        --move-rank-analysis
    )
    set_tests_properties(
        othello_eval_vs_exact_smoke_move_rank_stdout
        PROPERTIES PASS_REGULAR_EXPRESSION "move_rank_records_with_scores=1.*move_rank_records_missing_scores=2.*move_rank_analyzed=1"
    )

    othello_add_expect_failure_test(
        othello_exact_label_dump_rejects_invalid_input
        othello_exact_label_dump
        "invalid input"
        --input "${CMAKE_CURRENT_SOURCE_DIR}/data/positions/exact_label_invalid.txt"
        --output "${CMAKE_CURRENT_BINARY_DIR}/exact_label_invalid.jsonl"
    )

    othello_label_tests(experiment
        othello_exact_label_dump_help_includes_move_scores
        othello_eval_vs_exact_help_includes_labels
        othello_eval_vs_exact_help_includes_eval_config
        othello_eval_vs_exact_help_includes_high_confidence_threshold
        othello_eval_vs_exact_help_includes_move_rank_analysis
        othello_position_sampler_help_includes_target_empties
        othello_position_sampler_help_includes_seed
        othello_position_sampler_smoke
        othello_position_sampler_rejects_invalid_target_empties
        othello_exact_label_dump_smoke
        othello_eval_vs_exact_rejects_unknown_option
        othello_eval_vs_exact_rejects_unsupported_schema
        othello_eval_vs_exact_rejects_missing_required_field
        othello_eval_vs_exact_rejects_invalid_high_confidence_threshold
        othello_eval_vs_exact_smoke_with_default_evaluator
        othello_eval_vs_exact_smoke_with_eval_config
        othello_eval_vs_exact_smoke_move_rank_stdout
        othello_exact_label_dump_rejects_invalid_input
    )
endif()

find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND AND OTHELLO_BUILD_EXPERIMENT_TOOLS)
    add_test(
        NAME balanced_position_pool_py
        COMMAND "${Python3_EXECUTABLE}"
            -m unittest
            tools/scripts/tests/test_balanced_position_pool.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME ntest_teacher_smoke_py
        COMMAND "${Python3_EXECUTABLE}"
            -m unittest
            tools/scripts/tests/test_ntest_teacher_smoke.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME pattern_training_analysis_cache_py
        COMMAND "${Python3_EXECUTABLE}"
            -m unittest
            tools/scripts/tests/test_pattern_training_analysis_cache.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME pattern_training_helpers_py
        COMMAND "${Python3_EXECUTABLE}"
            -m unittest
            tools/scripts/tests/test_pattern_training_helpers.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_test(
        NAME pattern_only_train_py
        COMMAND "${Python3_EXECUTABLE}"
            -m unittest
            tools/scripts/tests/test_pattern_only_train.py
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    set_tests_properties(
        pattern_only_train_py
        PROPERTIES
            ENVIRONMENT "OTHELLO_ANALYZE_POSITION=$<TARGET_FILE:othello_analyze_position>"
    )

    add_test(
        NAME eval_candidate_matrix_dry_run
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/scripts/eval_candidate_matrix.py"
            --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
            --baseline-config "${OTHELLO_SAMPLE_EVAL_CONFIG}"
            --candidates "${OTHELLO_SAMPLE_EVAL_CONFIG}"
            --out "${CMAKE_CURRENT_BINARY_DIR}/eval_candidate_matrix_dry_run"
            --dry-run
    )
    set_tests_properties(
        eval_candidate_matrix_dry_run
        PROPERTIES PASS_REGULAR_EXPRESSION "report: .*report.md"
    )

    othello_label_tests(experiment
        balanced_position_pool_py
        ntest_teacher_smoke_py
        pattern_training_analysis_cache_py
        pattern_training_helpers_py
        pattern_only_train_py
        eval_candidate_matrix_dry_run
    )
endif()
