from __future__ import annotations

import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import base_head_match_matrix  # noqa: E402
import match_summary  # noqa: E402
from common import ScriptError  # noqa: E402


def matrix_config(temp_dir: Path) -> base_head_match_matrix.MatrixConfig:
    return base_head_match_matrix.MatrixConfig(
        base_build=temp_dir / "base-build",
        head_build=temp_dir / "head-build",
        base_repo=temp_dir / "base",
        head_repo=temp_dir / "head",
        openings=Path("data/openings/smoke_openings.txt"),
        depths=[4, 8],
        games=12,
        seed=20260524,
        out_dir=temp_dir / "runs" / "base-head" / "example",
        runner=temp_dir / "head-build" / "othello_match_runner",
        base_binary=temp_dir / "base-build" / "othello_nboard_engine",
        head_binary=temp_dir / "head-build" / "othello_nboard_engine",
        summary_script=Path("tools/scripts/match_summary.py"),
        external_timeout_ms=10000,
        allow_errors=False,
        base_engine_args=[],
        head_engine_args=[],
    )


class BaseHeadMatchMatrixTests(unittest.TestCase):
    def test_parse_depth_list_accepts_comma_separated_depths(self) -> None:
        self.assertEqual(base_head_match_matrix.parse_depth_list("4,6, 8,10"), [4, 6, 8, 10])

    def test_parse_depth_list_rejects_invalid_values(self) -> None:
        for value in ("", "4,,8", "4,x", "0", "-1"):
            with self.subTest(value=value):
                with self.assertRaises(ScriptError):
                    base_head_match_matrix.parse_depth_list(value)

    def test_generated_engine_config_uses_external_head_and_base(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = matrix_config(Path(temp))

            text = base_head_match_matrix.render_engine_config(config, 8)

        self.assertIn("head|8|", text)
        self.assertIn("base|8|", text)
        self.assertIn("othello_nboard_engine", text)
        self.assertIn("Format: name|depth|cwd|command|arg...", text)

    def test_generated_engine_config_includes_head_args_only_when_specified(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(
                matrix_config(Path(temp)),
                head_engine_args=[
                    "--eval-preset",
                    "mobility_plus_smoke",
                    "--exact-endgame-threshold",
                    "0",
                ],
            )

            text = base_head_match_matrix.render_engine_config(config, 4)

        lines = text.splitlines()
        head_line = next(line for line in lines if line.startswith("head|"))
        base_line = next(line for line in lines if line.startswith("base|"))
        self.assertIn(
            "|--eval-preset|mobility_plus_smoke|--exact-endgame-threshold|0",
            head_line,
        )
        self.assertNotIn("--eval-preset", base_line)
        self.assertNotIn("--exact-endgame-threshold", base_line)

    def test_engine_args_reject_pipe_delimiter(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = replace(matrix_config(Path(temp)), head_engine_args=["bad|arg"])

            with self.assertRaises(ScriptError):
                base_head_match_matrix.render_engine_config(config, 4)

    def test_build_depth_run_shapes_paths_and_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = matrix_config(Path(temp))
            run = base_head_match_matrix.build_depth_run(config, 10)

        self.assertEqual(run.run_dir.name, "depth-10")
        self.assertEqual(run.engines_path.name, "engines.generated.txt")
        self.assertEqual(run.match_path.name, "match.jsonl")
        self.assertIn("--black", run.runner_command)
        self.assertIn("external:head", run.runner_command)
        self.assertIn("--white", run.runner_command)
        self.assertIn("external:base", run.runner_command)
        self.assertIn("--external-timeout-ms", run.runner_command)

    def test_dry_run_writes_report_and_returns_success(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = matrix_config(Path(temp))

            exit_code = base_head_match_matrix.run_matrix(config, dry_run=True)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Not executed in dry-run mode.", report)

    def test_summary_table_renders_head_base_metrics(self) -> None:
        summary = match_summary.Summary(
            games=12,
            valid_games=12,
            player_a_wins=7,
            player_b_wins=4,
            draws=1,
            total_diff=36,
            total_plies=720,
            total_passes=12,
            optional_totals={"time_ms_player_a": 1200.0, "time_ms_player_b": 900.0},
            optional_counts={"time_ms_player_a": 12, "time_ms_player_b": 12},
        )
        with tempfile.TemporaryDirectory() as temp:
            config = matrix_config(Path(temp))
            run = base_head_match_matrix.build_depth_run(config, 4)
            result = base_head_match_matrix.DepthResult(
                run=run,
                summary=summary,
                runner_exit_code=0,
            )

            table = base_head_match_matrix.render_summary_table([result])

        self.assertIn("| 4 | 12 | 7 | 4 | 1 | 58.33% | 3.00 | 60.00 | 1.00 | 0 | 100.00 | 75.00 |", table)
        self.assertIn("match.jsonl", table)
        self.assertIn("summary.txt", table)

    def test_report_records_engine_args(self) -> None:
        summary = match_summary.Summary(games=0)
        with tempfile.TemporaryDirectory() as temp:
            config = replace(
                matrix_config(Path(temp)),
                head_engine_args=[
                    "--eval-preset",
                    "mobility_plus_smoke",
                    "--exact-endgame-threshold",
                    "0",
                ],
            )
            run = base_head_match_matrix.build_depth_run(config, 4)
            result = base_head_match_matrix.DepthResult(
                run=run,
                summary=summary,
                runner_exit_code=0,
            )

            report = base_head_match_matrix.render_report(config, [result])

        self.assertIn("Base engine args: `(none)`", report)
        self.assertIn(
            "Head engine args: `--eval-preset mobility_plus_smoke --exact-endgame-threshold 0`",
            report,
        )

    def test_parse_args_accepts_option_like_engine_args(self) -> None:
        args = base_head_match_matrix.parse_args(
            [
                "--base-build",
                "../build-base",
                "--head-build",
                "../build-head",
                "--base-repo",
                "../vibe-othello-base",
                "--head-repo",
                "../vibe-othello-head",
                "--openings",
                "data/openings/smoke_openings.txt",
                "--depths",
                "4",
                "--games",
                "6",
                "--seed",
                "20260530",
                "--out",
                "runs/base-head/example",
                "--head-engine-arg",
                "--eval-preset",
                "--head-engine-arg",
                "mobility_plus_smoke",
                "--head-engine-arg",
                "--exact-endgame-threshold",
                "--head-engine-arg",
                "0",
                "--dry-run",
            ]
        )

        self.assertEqual(
            args.head_engine_arg,
            ["--eval-preset", "mobility_plus_smoke", "--exact-endgame-threshold", "0"],
        )
        self.assertEqual(args.base_engine_arg, [])


if __name__ == "__main__":
    unittest.main()
