from __future__ import annotations

import contextlib
import io
import json
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import run_experiment_matrix  # noqa: E402
from common import ScriptError  # noqa: E402


def base_config() -> dict[str, object]:
    return {
        "runner": "./build/othello_match_runner",
        "summary_script": "tools/scripts/match_summary.py",
        "output_dir": "build/matches",
        "openings": "data/openings/smoke_openings.txt",
        "defaults": {
            "games": 8,
            "swap_sides": True,
            "seed": 1,
            "summary": True,
            "by_opening": True,
            "allow_errors": False,
        },
        "experiments": [
            {
                "name": "depth4_tt_pvs_vs_plain",
                "black": "search:depth=4,tt=on,pvs=on,exact=off",
                "white": "search:depth=4,tt=off,pvs=off,exact=off",
            }
        ],
    }


class RunExperimentMatrixTests(unittest.TestCase):
    def write_config(self, value: dict[str, object]) -> Path:
        temp = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        with temp:
            json.dump(value, temp)
        return Path(temp.name)

    def test_valid_config_builds_experiment_list(self) -> None:
        path = self.write_config(base_config())
        self.addCleanup(path.unlink)

        experiments = run_experiment_matrix.load_experiments(path)

        self.assertEqual(len(experiments), 1)
        self.assertEqual(experiments[0].name, "depth4_tt_pvs_vs_plain")
        self.assertEqual(experiments[0].runner, "./build/othello_match_runner")
        self.assertEqual(experiments[0].output, "build/matches/depth4_tt_pvs_vs_plain.jsonl")

    def test_defaults_are_applied(self) -> None:
        path = self.write_config(base_config())
        self.addCleanup(path.unlink)

        experiment = run_experiment_matrix.load_experiments(path)[0]

        self.assertEqual(experiment.games, 8)
        self.assertTrue(experiment.swap_sides)
        self.assertEqual(experiment.seed, 1)
        self.assertEqual(experiment.openings, "data/openings/smoke_openings.txt")
        self.assertTrue(experiment.summary)
        self.assertTrue(experiment.by_opening)

    def test_experiment_values_override_defaults(self) -> None:
        config = base_config()
        experiments = config["experiments"]
        assert isinstance(experiments, list)
        experiment = experiments[0]
        assert isinstance(experiment, dict)
        experiment["games"] = 4
        experiment["seed"] = 2
        experiment["swap_sides"] = False
        experiment["output"] = "custom/out.jsonl"

        path = self.write_config(config)
        self.addCleanup(path.unlink)

        parsed = run_experiment_matrix.load_experiments(path)[0]

        self.assertEqual(parsed.games, 4)
        self.assertEqual(parsed.seed, 2)
        self.assertFalse(parsed.swap_sides)
        self.assertEqual(parsed.output, "custom/out.jsonl")

    def test_path_like_names_are_rejected(self) -> None:
        for name in ("bad/name", "bad\\name", "../bad", "bad..name"):
            config = base_config()
            experiments = config["experiments"]
            assert isinstance(experiments, list)
            experiment = experiments[0]
            assert isinstance(experiment, dict)
            experiment["name"] = name
            path = self.write_config(config)
            self.addCleanup(path.unlink)

            with self.assertRaises(ScriptError):
                run_experiment_matrix.load_experiments(path)

    def test_missing_required_field_is_error(self) -> None:
        config = base_config()
        experiments = config["experiments"]
        assert isinstance(experiments, list)
        experiment = experiments[0]
        assert isinstance(experiment, dict)
        del experiment["black"]
        path = self.write_config(config)
        self.addCleanup(path.unlink)

        with self.assertRaises(ScriptError):
            run_experiment_matrix.load_experiments(path)

    def test_dry_run_prints_runner_and_summary_commands(self) -> None:
        path = self.write_config(base_config())
        self.addCleanup(path.unlink)
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            exit_code = run_experiment_matrix.main(["--config", str(path), "--dry-run"])

        output = stdout.getvalue()
        self.assertEqual(exit_code, 0)
        self.assertIn("experiment: depth4_tt_pvs_vs_plain", output)
        self.assertIn("runner: ./build/othello_match_runner", output)
        self.assertIn("--output build/matches/depth4_tt_pvs_vs_plain.jsonl", output)
        self.assertIn("summary:", output)
        self.assertIn("--by-opening", output)

    def test_allow_errors_is_passed_to_summary_command(self) -> None:
        config = base_config()
        defaults = config["defaults"]
        assert isinstance(defaults, dict)
        defaults["allow_errors"] = True
        path = self.write_config(config)
        self.addCleanup(path.unlink)

        experiment = run_experiment_matrix.load_experiments(path)[0]
        summary_command = run_experiment_matrix.build_summary_command(experiment)

        self.assertIsNotNone(summary_command)
        assert summary_command is not None
        self.assertIn("--allow-errors", summary_command)

    def test_runner_failure_counts_failed_experiment(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            runner = Path(temp_dir) / "fail_runner.py"
            runner.write_text("#!/usr/bin/env python3\nimport sys\nsys.exit(7)\n", encoding="utf-8")
            runner.chmod(runner.stat().st_mode | stat.S_IXUSR)

            config = base_config()
            config["runner"] = str(runner)
            defaults = config["defaults"]
            assert isinstance(defaults, dict)
            defaults["summary"] = False
            path = self.write_config(config)
            self.addCleanup(path.unlink)

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = run_experiment_matrix.main(["--config", str(path)])

        self.assertEqual(exit_code, 1)
        self.assertIn("failed: 1", stdout.getvalue())

    def test_fail_fast_stops_after_first_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            log_path = Path(temp_dir) / "runner.log"
            runner = Path(temp_dir) / "fail_runner.py"
            runner.write_text(
                "#!/usr/bin/env python3\n"
                "import pathlib, sys\n"
                f"pathlib.Path({str(log_path)!r}).write_text('ran\\n', encoding='utf-8')\n"
                "sys.exit(7)\n",
                encoding="utf-8",
            )
            runner.chmod(runner.stat().st_mode | stat.S_IXUSR)

            config = base_config()
            config["runner"] = str(runner)
            config["experiments"] = [
                {"name": "first", "black": "first", "white": "random"},
                {"name": "second", "black": "first", "white": "random"},
            ]
            defaults = config["defaults"]
            assert isinstance(defaults, dict)
            defaults["summary"] = False
            path = self.write_config(config)
            self.addCleanup(path.unlink)

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = run_experiment_matrix.main(["--config", str(path), "--fail-fast"])
            log_text = log_path.read_text(encoding="utf-8")
            output_text = stdout.getvalue()

        self.assertEqual(exit_code, 7)
        self.assertEqual(log_text, "ran\n")
        self.assertIn("experiment: first", output_text)
        self.assertNotIn("experiment: second", output_text)
        self.assertIn("failed: 1", output_text)


if __name__ == "__main__":
    unittest.main()
