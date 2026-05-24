from __future__ import annotations

import sys
import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
