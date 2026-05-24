from __future__ import annotations

import contextlib
import io
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import run_match_experiment  # noqa: E402


class RunMatchExperimentTests(unittest.TestCase):
    def test_dry_run_prints_expected_command(self) -> None:
        stdout = io.StringIO()

        with contextlib.redirect_stdout(stdout):
            exit_code = run_match_experiment.main(
                [
                    "--runner",
                    "./build/othello_match_runner",
                    "--summary-script",
                    "tools/scripts/match_summary.py",
                    "--black",
                    "search:depth=4,tt=on",
                    "--white",
                    "random",
                    "--games",
                    "4",
                    "--swap-sides",
                    "true",
                    "--seed",
                    "1",
                    "--openings",
                    "data/openings/smoke_openings.txt",
                    "--output",
                    "build/matches/out.jsonl",
                    "--summary",
                    "--dry-run",
                ]
            )

        lines = stdout.getvalue().splitlines()
        self.assertEqual(exit_code, 0)
        self.assertEqual(
            lines[0],
            "./build/othello_match_runner --black search:depth=4,tt=on --white random "
            "--games 4 --swap-sides true --seed 1 --openings data/openings/smoke_openings.txt "
            "--output build/matches/out.jsonl",
        )
        self.assertIn("tools/scripts/match_summary.py --input build/matches/out.jsonl", lines[1])

    def test_runner_failure_exit_code_is_propagated(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            runner = Path(temp_dir) / "fail_runner.py"
            runner.write_text(
                "#!/usr/bin/env python3\nimport sys\nsys.exit(7)\n",
                encoding="utf-8",
            )
            runner.chmod(runner.stat().st_mode | stat.S_IXUSR)

            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_DIR / "run_match_experiment.py"),
                    "--runner",
                    str(runner),
                    "--black",
                    "first",
                    "--white",
                    "random",
                    "--games",
                    "1",
                    "--swap-sides",
                    "false",
                    "--seed",
                    "1",
                    "--output",
                    str(Path(temp_dir) / "out.jsonl"),
                ],
                cwd=REPO_ROOT,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

        self.assertEqual(completed.returncode, 7)

    def test_invalid_swap_sides_is_error(self) -> None:
        stderr = io.StringIO()

        with contextlib.redirect_stderr(stderr):
            with self.assertRaises(SystemExit) as raised:
                run_match_experiment.parse_args(
                    [
                        "--runner",
                        "runner",
                        "--black",
                        "first",
                        "--white",
                        "random",
                        "--games",
                        "1",
                        "--swap-sides",
                        "maybe",
                        "--seed",
                        "1",
                        "--output",
                        "out.jsonl",
                    ]
                )

        self.assertNotEqual(raised.exception.code, 0)


if __name__ == "__main__":
    unittest.main()
