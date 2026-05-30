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
    return eval_experiment_matrix.EvalExperimentConfig(
        presets=["default", "mobility_plus_smoke"],
        depths=[4, 6],
        games=6,
        openings=Path("data/openings/smoke_openings.txt"),
        seed=20260530,
        build_dir=temp_dir / "build",
        out_dir=temp_dir / "runs" / "eval" / "example",
        search_bench=temp_dir / "build" / "othello_search_bench",
        match_runner=temp_dir / "build" / "othello_match_runner",
        exact_endgame_threshold=0,
        positions="smoke",
        by_position=False,
        allow_errors=False,
    )


class EvalExperimentMatrixTests(unittest.TestCase):
    def test_parse_csv_names_rejects_empty_entries(self) -> None:
        self.assertEqual(
            eval_experiment_matrix.parse_csv_names("default, mobility_plus_smoke"),
            ["default", "mobility_plus_smoke"],
        )
        for value in ("", "default,,mobility_plus_smoke"):
            with self.subTest(value=value):
                with self.assertRaises(ScriptError):
                    eval_experiment_matrix.parse_csv_names(value)

    def test_parse_depth_list_requires_positive_depths(self) -> None:
        self.assertEqual(eval_experiment_matrix.parse_depth_list("4, 6"), [4, 6])
        for value in ("", "0", "-1", "4,x"):
            with self.subTest(value=value):
                with self.assertRaises(ScriptError):
                    eval_experiment_matrix.parse_depth_list(value)

    def test_search_bench_command_includes_eval_preset(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            command = eval_experiment_matrix.build_search_bench_command(
                config, "mobility_plus_smoke"
            )

        self.assertIn("--eval-preset", command)
        self.assertIn("mobility_plus_smoke", command)
        self.assertIn("--exact-endgame-threshold", command)

    def test_search_bench_command_can_use_suite_by_position(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(experiment_config(Path(temp)), positions="suite", by_position=True)
            command = eval_experiment_matrix.build_search_bench_command(
                config, "mobility_plus_smoke"
            )

        self.assertIn("--positions", command)
        self.assertEqual(command[command.index("--positions") + 1], "suite")
        self.assertIn("--by-position", command)

    def test_search_bench_command_omits_by_position_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            command = eval_experiment_matrix.build_search_bench_command(config, "default")

        self.assertNotIn("--by-position", command)

    def test_match_command_compares_candidate_to_default(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            run = eval_experiment_matrix.build_match_command(config, "mobility_plus_smoke", 4)

        self.assertIn("search:depth=4,tt=on,pvs=on,exact=off,eval=mobility_plus_smoke",
                      run.command)
        self.assertIn("search:depth=4,tt=on,pvs=on,exact=off,eval=default", run.command)
        self.assertEqual(run.output_path.name, "mobility_plus_smoke-depth-4.jsonl")

    def test_dry_run_writes_report_without_running_binaries(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))

            exit_code = eval_experiment_matrix.run_matrix(config, dry_run=True)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: dry run.", report)
        self.assertIn("--eval-preset mobility_plus_smoke", report)
        self.assertIn("eval=mobility_plus_smoke", report)
        self.assertIn("Search positions: `smoke`", report)
        self.assertIn("Search by-position: `off`", report)

    def test_dry_run_report_records_suite_by_position(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(experiment_config(Path(temp)), positions="suite", by_position=True)

            exit_code = eval_experiment_matrix.run_matrix(config, dry_run=True)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("--positions suite", report)
        self.assertIn("--by-position", report)
        self.assertIn("Search positions: `suite`", report)
        self.assertIn("Search by-position: `on`", report)

    def test_error_games_fail_without_allow_errors(self) -> None:
        summary = match_summary.Summary(games=1, valid_games=0, error_games=1)
        run = eval_experiment_matrix.MatchRun(
            preset="mobility_plus_smoke",
            depth=4,
            output_path=Path("match.jsonl"),
            command=["othello_match_runner"],
            summary=summary,
            summary_text="error games: 1\n",
        )

        self.assertTrue(
            eval_experiment_matrix.matrix_has_failures([], [run], allow_errors=False)
        )
        self.assertFalse(
            eval_experiment_matrix.matrix_has_failures([], [run], allow_errors=True)
        )

    def test_error_games_are_reported_as_failures(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = experiment_config(Path(temp))
            summary = match_summary.Summary(games=1, valid_games=0, error_games=1)
            run = eval_experiment_matrix.MatchRun(
                preset="mobility_plus_smoke",
                depth=4,
                output_path=Path("match.jsonl"),
                command=["othello_match_runner"],
                summary=summary,
                summary_text="error games: 1\n",
            )

            report = eval_experiment_matrix.render_report(
                config, [], [run], dry_run=False
            )

        self.assertIn("Error games: `1`", report)
        self.assertIn("Failure: match summary contains error games.", report)
        self.assertIn("Failure: 1 error game(s) were reported.", report)

    def test_allow_errors_report_notes_error_games(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(experiment_config(Path(temp)), allow_errors=True)
            summary = match_summary.Summary(games=1, valid_games=0, error_games=1)
            run = eval_experiment_matrix.MatchRun(
                preset="mobility_plus_smoke",
                depth=4,
                output_path=Path("match.jsonl"),
                command=["othello_match_runner"],
                summary=summary,
                summary_text="error games: 1\n",
            )

            report = eval_experiment_matrix.render_report(
                config, [], [run], dry_run=False
            )

        self.assertIn("Allow error games: `true`", report)
        self.assertIn("Error games allowed: 1 error game(s) were reported.", report)


if __name__ == "__main__":
    unittest.main()
