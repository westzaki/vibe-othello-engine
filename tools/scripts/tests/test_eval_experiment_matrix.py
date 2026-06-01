from __future__ import annotations

import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_experiment_matrix  # noqa: E402
import match_summary  # noqa: E402
from common import ScriptError  # noqa: E402


def experiment_config(temp_dir: Path) -> eval_experiment_matrix.EvalExperimentConfig:
    candidate = eval_experiment_matrix.EvalTarget(
        kind="config",
        value=str(temp_dir / "classic_features_lite_v1.eval"),
        label="classic_features_lite_v1",
    )
    frontier_candidate = eval_experiment_matrix.EvalTarget(
        kind="config",
        value=str(temp_dir / "frontier_classic_features_lite_v1.eval"),
        label="frontier_classic_features_lite_v1",
    )
    reference = eval_experiment_matrix.EvalTarget(
        kind="config",
        value=str(temp_dir / "frontier_open2_mid2_late_plus1.eval"),
        label="frontier_open2_mid2_late_plus1",
    )
    return eval_experiment_matrix.EvalExperimentConfig(
        small_depths=[5, 6],
        extended_depths=[7, 8],
        small_games=48,
        extended_games=96,
        promote_top=1,
        max_node_ratio=1.15,
        min_avg_diff=0.0,
        require_nonnegative_diff=False,
        openings=Path("data/openings/eval_regression_openings.txt"),
        seed=20260530,
        build_dir=temp_dir / "build",
        out_dir=temp_dir / "runs" / "eval" / "example",
        search_bench=temp_dir / "build" / "othello_search_bench",
        match_runner=temp_dir / "build" / "othello_match_runner",
        exact_endgame_threshold=0,
        configs=[candidate, frontier_candidate],
        reference_config=reference,
        positions="smoke",
        by_position=False,
        allow_errors=False,
    )


def fake_summary(
    *,
    games: int = 48,
    wins: int = 28,
    losses: int = 18,
    draws: int = 2,
    avg_diff: float = 4.0,
    errors: int = 0,
    nodes_a: float = 110.0,
    nodes_b: float = 100.0,
    time_a: float = 12.0,
    time_b: float = 10.0,
) -> match_summary.Summary:
    valid = games - errors
    return match_summary.Summary(
        games=games,
        valid_games=valid,
        error_games=errors,
        player_a_wins=wins,
        player_b_wins=losses,
        draws=draws,
        total_diff=int(avg_diff * valid),
        total_plies=60 * valid,
        total_passes=valid,
        optional_totals={
            "nodes_player_a": nodes_a * valid,
            "nodes_player_b": nodes_b * valid,
            "time_ms_player_a": time_a * valid,
            "time_ms_player_b": time_b * valid,
        },
        optional_counts={
            "nodes_player_a": valid,
            "nodes_player_b": valid,
            "time_ms_player_a": valid,
            "time_ms_player_b": valid,
        },
    )


def fake_match_run(
    config: eval_experiment_matrix.EvalExperimentConfig,
    label: str,
    depth: int,
    summary: match_summary.Summary,
) -> eval_experiment_matrix.MatchRun:
    target = next(target for target in config.candidate_targets if target.label == label)
    run = eval_experiment_matrix.build_match_command(config, target, depth)
    return replace(run, summary=summary, summary_text="summary\n")


class EvalExperimentMatrixTests(unittest.TestCase):
    def test_parse_csv_paths_rejects_empty_entries(self) -> None:
        self.assertEqual(
            eval_experiment_matrix.parse_csv_paths("a.eval, b.eval"),
            [Path("a.eval"), Path("b.eval")],
        )
        for value in ("", "a.eval,,b.eval", ",a.eval", "a.eval, "):
            with self.subTest(value=value):
                with self.assertRaises(ScriptError):
                    eval_experiment_matrix.parse_csv_paths(value)

    def test_parse_depth_list_requires_positive_depths(self) -> None:
        self.assertEqual(eval_experiment_matrix.parse_depth_list("4, 6"), [4, 6])
        for value in ("", "0", "-1", "4,x"):
            with self.subTest(value=value):
                with self.assertRaises(ScriptError):
                    eval_experiment_matrix.parse_depth_list(value)

    def test_parse_reference_config(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = temp_path / "candidate.eval"
            reference = temp_path / "reference.eval"
            candidate.write_text("name=candidate_config\n", encoding="utf-8")
            reference.write_text("name=reference_config\n", encoding="utf-8")
            args = eval_experiment_matrix.parse_args(
                [
                    "--configs",
                    str(candidate),
                    "--reference-config",
                    str(reference),
                    "--small-depths",
                    "5,6",
                    "--small-games",
                    "48",
                    "--openings",
                    "data/openings/eval_regression_openings.txt",
                    "--seed",
                    "20260530",
                    "--build-dir",
                    "build",
                    "--out",
                    "runs/eval/example",
                ]
            )
            config = eval_experiment_matrix.config_from_args(args)

        self.assertEqual(config.reference_target.label, "reference_config")

    def test_default_reference_uses_built_in_evaluator(self) -> None:
        args = eval_experiment_matrix.parse_args(
            [
                "--configs",
                "classic_features_lite_v1.eval",
                "--small-depths",
                "5,6",
                "--small-games",
                "48",
                "--openings",
                "data/openings/eval_regression_openings.txt",
                "--seed",
                "20260530",
                "--build-dir",
                "build",
                "--out",
                "runs/eval/example",
            ]
        )
        config = eval_experiment_matrix.config_from_args(args)

        self.assertEqual(config.reference_target.kind, "default")
        self.assertEqual(config.reference_target.label, "built-in default")

    def test_depths_and_games_remain_small_stage_aliases(self) -> None:
        args = eval_experiment_matrix.parse_args(
                [
                    "--configs",
                    "classic_features_lite_v1.eval",
                    "--depths",
                    "5,6",
                "--games",
                "48",
                "--openings",
                "data/openings/eval_regression_openings.txt",
                "--seed",
                "20260530",
                "--build-dir",
                "build",
                "--out",
                "runs/eval/example",
            ]
        )
        config = eval_experiment_matrix.config_from_args(args)

        self.assertEqual(config.small_depths, [5, 6])
        self.assertEqual(config.small_games, 48)
        self.assertEqual(config.extended_depths, [])
        self.assertEqual(config.extended_games, 48)

    def test_search_bench_command_includes_all_staged_depths(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            command = eval_experiment_matrix.build_search_bench_command(
                config, config.configs[0]
            )

        self.assertIn("--eval-config", command)
        self.assertIn(config.configs[0].value, command)
        self.assertEqual(command[command.index("--depths") + 1], "5,6,7,8")

    def test_search_bench_command_can_use_evaluation_by_position(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(
                experiment_config(Path(temp)), positions="evaluation", by_position=True
            )
            command = eval_experiment_matrix.build_search_bench_command(
                config, config.configs[0]
            )

        self.assertIn("--positions", command)
        self.assertEqual(command[command.index("--positions") + 1], "evaluation")
        self.assertIn("--by-position", command)

    def test_match_command_uses_reference_config(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            run = eval_experiment_matrix.build_match_command(
                config, config.configs[0], 5
            )

        self.assertIn(
            f"search:depth=5,tt=on,pvs=on,exact=off,eval_config={config.configs[0].value}",
            run.command,
        )
        self.assertIn(
            "search:depth=5,tt=on,pvs=on,exact=off,"
            f"eval_config={config.reference_target.value}",
            run.command,
        )
        self.assertEqual(
            run.output_path.name,
            "classic_features_lite_v1-vs-frontier_open2_mid2_late_plus1-depth-5.jsonl",
        )

    def test_staged_small_and_extended_commands_are_shaped(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            small = eval_experiment_matrix.build_match_command(
                config, config.configs[0], 5, stage="small", games=48
            )
            extended = eval_experiment_matrix.build_match_command(
                config, config.configs[0], 8, stage="extended", games=96
            )

        self.assertIn("small", small.output_path.parts)
        self.assertIn("--games", small.command)
        self.assertEqual(small.command[small.command.index("--games") + 1], "48")
        self.assertIn("extended", extended.output_path.parts)
        self.assertEqual(extended.command[extended.command.index("--games") + 1], "96")
        self.assertIn(
            f"search:depth=8,tt=on,pvs=on,exact=off,eval_config={config.configs[0].value}",
            extended.command,
        )

    def test_promotion_helper_promotes_positive_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            runs = [
                fake_match_run(config, "classic_features_lite_v1", 5, fake_summary(avg_diff=3.0)),
                fake_match_run(config, "classic_features_lite_v1", 6, fake_summary(avg_diff=2.0)),
            ]

            decision = eval_experiment_matrix.decide_candidate(
                "classic_features_lite_v1", runs, [], config
            )

        self.assertEqual(decision.status, "promoted_to_extended")
        self.assertTrue(decision.promoted_to_extended)

    def test_promotion_helper_rejects_error_games(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            runs = [
                fake_match_run(
                    config,
                    "classic_features_lite_v1",
                    5,
                    fake_summary(games=1, wins=0, losses=0, draws=0, avg_diff=0, errors=1),
                )
            ]

            decision = eval_experiment_matrix.decide_candidate(
                "classic_features_lite_v1", runs, [], config
            )

        self.assertEqual(decision.status, "failed")
        self.assertIn("error game", decision.reasons[0])

    def test_promotion_helper_handles_mixed_wins_and_average_diff(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            runs = [
                fake_match_run(
                    config,
                    "classic_features_lite_v1",
                    5,
                    fake_summary(wins=20, losses=26, draws=2, avg_diff=2.0),
                )
            ]

            decision = eval_experiment_matrix.decide_candidate(
                "classic_features_lite_v1", runs, [], config
            )

        self.assertEqual(decision.status, "needs_retune")
        self.assertIn("positive average diff", "; ".join(decision.reasons))

    def test_promotion_helper_demotes_speed_regression(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(experiment_config(Path(temp)), max_node_ratio=1.10)
            runs = [
                fake_match_run(
                    config,
                    "classic_features_lite_v1",
                    5,
                    fake_summary(wins=30, losses=16, draws=2, avg_diff=3.0, nodes_a=130.0),
                )
            ]

            decision = eval_experiment_matrix.decide_candidate(
                "classic_features_lite_v1", runs, [], config
            )

        self.assertEqual(decision.status, "needs_retune")
        self.assertIn("needs_speed_check", "; ".join(decision.reasons))

    def test_dry_run_writes_report_with_reference_and_extended_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))

            exit_code = eval_experiment_matrix.run_matrix(config, dry_run=True)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: dry run.", report)
        self.assertIn("Reference config: `frontier_open2_mid2_late_plus1`", report)
        self.assertIn(f"eval_config={config.reference_target.value}", report)
        self.assertIn("extended", report)
        self.assertIn("--games 96", report)
        self.assertIn("Promotion Table", report)

    def test_dry_run_report_records_evaluation_by_position(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(
                experiment_config(Path(temp)), positions="evaluation", by_position=True
            )

            exit_code = eval_experiment_matrix.run_matrix(config, dry_run=True)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("--positions evaluation", report)
        self.assertIn("--by-position", report)
        self.assertIn("Search positions: `evaluation`", report)
        self.assertIn("Search positions apply to search screening only", report)
        self.assertIn("Search by-position: `on`", report)

    def test_dry_run_with_configs_emits_eval_config_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = temp_path / "candidate.eval"
            reference = temp_path / "reference.eval"
            candidate.write_text("name=candidate_config\n", encoding="utf-8")
            reference.write_text("name=reference_config\n", encoding="utf-8")
            args = eval_experiment_matrix.parse_args(
                [
                    "--configs",
                    str(candidate),
                    "--reference-config",
                    str(reference),
                    "--small-depths",
                    "5",
                    "--small-games",
                    "4",
                    "--openings",
                    "data/openings/eval_regression_openings.txt",
                    "--seed",
                    "20260530",
                    "--build-dir",
                    str(temp_path / "build"),
                    "--out",
                    str(temp_path / "runs"),
                    "--dry-run",
                ]
            )
            config = eval_experiment_matrix.config_from_args(args)

            exit_code = eval_experiment_matrix.run_matrix(config, dry_run=True)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("--eval-config", report)
        self.assertIn(f"eval_config={candidate}", report)
        self.assertIn(f"eval_config={reference}", report)
        self.assertIn("Candidate configs: `candidate_config`", report)
        self.assertIn("Reference config: `reference_config`", report)

    def test_config_from_args_rejects_duplicate_config_labels(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            first = temp_path / "first.eval"
            second = temp_path / "second.eval"
            first.write_text("name=duplicate\n", encoding="utf-8")
            second.write_text("name=duplicate\n", encoding="utf-8")
            args = eval_experiment_matrix.parse_args(
                [
                    "--configs",
                    f"{first},{second}",
                    "--small-depths",
                    "5",
                    "--small-games",
                    "4",
                    "--openings",
                    "data/openings/eval_regression_openings.txt",
                    "--seed",
                    "20260530",
                    "--build-dir",
                    str(temp_path / "build"),
                    "--out",
                    str(temp_path / "runs"),
                ]
            )

            with self.assertRaisesRegex(ScriptError, "duplicate evaluator label"):
                eval_experiment_matrix.config_from_args(args)

    def test_config_from_args_rejects_config_label_colliding_with_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = temp_path / "candidate.eval"
            reference = temp_path / "reference.eval"
            candidate.write_text("name=reference\n", encoding="utf-8")
            reference.write_text("name=reference\n", encoding="utf-8")
            args = eval_experiment_matrix.parse_args(
                [
                    "--configs",
                    str(candidate),
                    "--reference-config",
                    str(reference),
                    "--small-depths",
                    "5",
                    "--small-games",
                    "4",
                    "--openings",
                    "data/openings/eval_regression_openings.txt",
                    "--seed",
                    "20260530",
                    "--build-dir",
                    str(temp_path / "build"),
                    "--out",
                    str(temp_path / "runs"),
                ]
            )

            with self.assertRaisesRegex(ScriptError, "duplicate evaluator label"):
                eval_experiment_matrix.config_from_args(args)

    def test_config_from_args_rejects_duplicate_slugs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            first = temp_path / "first.eval"
            second = temp_path / "second.eval"
            first.write_text("name=foo bar\n", encoding="utf-8")
            second.write_text("name=foo-bar\n", encoding="utf-8")
            args = eval_experiment_matrix.parse_args(
                [
                    "--configs",
                    f"{first},{second}",
                    "--small-depths",
                    "5",
                    "--small-games",
                    "4",
                    "--openings",
                    "data/openings/eval_regression_openings.txt",
                    "--seed",
                    "20260530",
                    "--build-dir",
                    str(temp_path / "build"),
                    "--out",
                    str(temp_path / "runs"),
                ]
            )

            with self.assertRaisesRegex(ScriptError, "duplicate evaluator slug"):
                eval_experiment_matrix.config_from_args(args)

    def test_ntest_sanity_skipped_when_config_unavailable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(experiment_config(Path(temp)), run_ntest_sanity=True)

            exit_code = eval_experiment_matrix.run_matrix(config, dry_run=True)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("NTest sanity skipped: config unavailable", report)

    def test_ntest_command_generation_when_configured(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = replace(
                experiment_config(temp_path),
                run_ntest_sanity=True,
                ntest_engine="ntest8",
                engines=temp_path / "engines.txt",
                ntest_depth=6,
                ntest_games=12,
                ntest_openings=Path("data/openings/smoke_openings.txt"),
            )

            run = eval_experiment_matrix.build_ntest_command(
                config, config.configs[1]
            )

        self.assertIn("external:ntest8", run.command)
        self.assertIn("--engines", run.command)
        self.assertIn(
            f"search:depth=6,tt=on,pvs=on,exact=off,eval_config={config.configs[1].value}",
            run.command,
        )
        self.assertEqual(run.command[run.command.index("--games") + 1], "12")

    def test_ntest_report_shows_error_reason(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(
                experiment_config(Path(temp)),
                run_ntest_sanity=True,
                ntest_engine="ntest8",
                engines=Path(temp) / "engines.txt",
            )
            run = eval_experiment_matrix.NTestRun(
                preset="frontier_classic_features_lite_v1",
                depth=6,
                output_path=Path(temp) / "ntest.jsonl",
                command=["othello_match_runner"],
                exit_code=2,
                error="external engine timed out",
            )

            table = eval_experiment_matrix.render_ntest_table([run])
            report = eval_experiment_matrix.render_report(
                config,
                search_runs=[],
                small_runs=[],
                extended_runs=[],
                decisions=[],
                ntest_runs=[run],
                ntest_skip_reason=None,
                dry_run=False,
            )

        self.assertIn("external engine timed out", table)
        self.assertIn("| error | JSONL |", table)
        self.assertIn("external engine timed out", report)
        self.assertIn("NTest sanity issue found: 1 run(s)", report)

    def test_error_games_fail_without_allow_errors(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            summary = match_summary.Summary(games=1, valid_games=0, error_games=1)
            run = fake_match_run(config, "classic_features_lite_v1", 5, summary)

        self.assertTrue(
            eval_experiment_matrix.matrix_has_failures([], [run], allow_errors=False)
        )
        self.assertFalse(
            eval_experiment_matrix.matrix_has_failures([], [run], allow_errors=True)
        )


if __name__ == "__main__":
    unittest.main()
