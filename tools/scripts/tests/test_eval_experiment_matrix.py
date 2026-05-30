from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_experiment_matrix  # noqa: E402
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


if __name__ == "__main__":
    unittest.main()
