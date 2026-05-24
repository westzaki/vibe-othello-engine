from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from external_engines.one_shot import request_best_move  # noqa: E402


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"
RUN_ONCE = SCRIPT_DIR / "run_external_engine_once.py"


class ExternalEngineAdapterTests(unittest.TestCase):
    def test_fake_engine_move_is_parsed(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE), "--move", "D3"],
            board_text="board\n",
            timeout_ms=1000,
        )

        self.assertEqual(result.move, "d3")
        self.assertEqual(result.exit_code, 0)
        self.assertFalse(result.timed_out)

    def test_stderr_is_captured(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE), "--move", "d3", "--stderr", "boom"],
            board_text="board\n",
            timeout_ms=1000,
        )

        self.assertEqual(result.move, "d3")
        self.assertIn("boom", result.raw_error)

    def test_non_zero_exit_code_is_captured(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE), "--move", "d3", "--exit-code", "7"],
            board_text="board\n",
            timeout_ms=1000,
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 7)
        self.assertFalse(result.timed_out)

    def test_timeout_is_detected(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE), "--move", "d3", "--sleep-ms", "200"],
            board_text="board\n",
            timeout_ms=20,
        )

        self.assertIsNone(result.move)
        self.assertTrue(result.timed_out)
        self.assertEqual(result.exit_code, -1)

    def test_empty_stdout_is_error_result(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE)],
            board_text="board\n",
            timeout_ms=1000,
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 0)
        self.assertFalse(result.timed_out)
        self.assertEqual(result.error, "engine produced no move")

    def test_invalid_move_format_is_error_result(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE), "--move", "z9"],
            board_text="board\n",
            timeout_ms=1000,
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.error, "invalid move format: z9")

    def test_pass_move_is_allowed(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE), "--move", "PASS"],
            board_text="board\n",
            timeout_ms=1000,
        )

        self.assertEqual(result.move, "pass")

    def test_workdir_and_env_are_passed_to_engine(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            result = request_best_move(
                [sys.executable, str(FAKE_ENGINE), "--print-env", "OTHELLO_FAKE_MOVE"],
                board_text="board\n",
                timeout_ms=1000,
                workdir=temp_dir,
                env={"OTHELLO_FAKE_MOVE": "c4"},
            )

        self.assertEqual(result.move, "c4")

    def test_run_external_engine_once_cli_success(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "d3",
            ],
            cwd=REPO_ROOT,
            input="board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 0)
        self.assertIn("move: d3", completed.stdout)
        self.assertIn("timed_out: false", completed.stdout)

    def test_run_external_engine_once_cli_accepts_board_file(self) -> None:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as board_file:
            board_file.write("board\n")
            board_path = Path(board_file.name)
        self.addCleanup(board_path.unlink)

        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--board-file",
                str(board_path),
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "d3",
            ],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 0)
        self.assertIn("move: d3", completed.stdout)

    def test_run_external_engine_once_cli_timeout_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--timeout-ms",
                "20",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "d3",
                "--sleep-ms",
                "200",
            ],
            cwd=REPO_ROOT,
            input="board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("timed_out: true", completed.stdout)

    def test_run_external_engine_once_cli_non_zero_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--exit-code",
                "7",
                "--stderr",
                "boom",
            ],
            cwd=REPO_ROOT,
            input="board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("exit_code: 7", completed.stdout)
        self.assertIn("stderr: boom", completed.stdout)

    def test_run_external_engine_once_cli_rejects_missing_command_boundary(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--engine-cmd",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "d3",
            ],
            cwd=REPO_ROOT,
            input="board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("--engine-cmd must be followed by '--'", completed.stderr)

    def test_run_external_engine_once_cli_passes_workdir_and_env(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            completed = subprocess.run(
                [
                    sys.executable,
                    str(RUN_ONCE),
                    "--stdin-board",
                    "--workdir",
                    temp_dir,
                    "--env",
                    "OTHELLO_FAKE_MOVE=f5",
                    "--engine-cmd",
                    "--",
                    sys.executable,
                    str(FAKE_ENGINE),
                    "--print-env",
                    "OTHELLO_FAKE_MOVE",
                ],
                cwd=REPO_ROOT,
                input="board\n",
                capture_output=True,
                text=True,
                check=False,
            )

        self.assertEqual(completed.returncode, 0)
        self.assertIn("move: f5", completed.stdout)

    def test_engine_command_help_is_not_captured_by_adapter(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--help",
            ],
            cwd=REPO_ROOT,
            input="board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("usage: fake_engine.py", completed.stdout)
        self.assertIn("error: invalid move format: usage: fake_engine.py", completed.stdout)
        self.assertNotIn("Run one external engine request", completed.stdout)


if __name__ == "__main__":
    unittest.main()
