from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from external_engines.ntest import (  # noqa: E402
    NTestConfig,
    normalize_ntest_move,
    parse_ntest_move_output,
    request_best_move,
)
from external_engines.common import ExternalEngineError  # noqa: E402


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"
RUN_NTEST_ONCE = SCRIPT_DIR / "run_ntest_once.py"


class NTestAdapterTests(unittest.TestCase):
    def test_parse_ntest_move_output_parses_plain_move(self) -> None:
        self.assertEqual(parse_ntest_move_output("d3\n"), "d3")

    def test_parse_ntest_move_output_parses_labeled_move(self) -> None:
        self.assertEqual(parse_ntest_move_output("best move: D3\nscore: 4\n"), "d3")

    def test_parse_ntest_move_output_prefers_nboard_best_move_line(self) -> None:
        output = "book C4 1.81,-1.85 15165 26\n=== D3//0.00\n"
        self.assertEqual(parse_ntest_move_output(output), "d3")

    def test_normalize_ntest_move_accepts_uppercase_coordinate(self) -> None:
        self.assertEqual(normalize_ntest_move("D3"), "d3")

    def test_normalize_ntest_move_accepts_pass(self) -> None:
        self.assertEqual(normalize_ntest_move("PASS"), "pass")

    def test_normalize_ntest_move_rejects_invalid_move(self) -> None:
        self.assertIsNone(normalize_ntest_move("z9"))

    def test_empty_output_is_error_result(self) -> None:
        result = request_best_move(
            NTestConfig(command=[sys.executable, str(FAKE_ENGINE)], timeout_ms=1000),
            "board\n",
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.error, "ntest produced no recognizable move")

    def test_fake_engine_move_is_received(self) -> None:
        result = request_best_move(
            NTestConfig(command=[sys.executable, str(FAKE_ENGINE), "--move", "D3"], timeout_ms=1000),
            "board\n",
        )

        self.assertEqual(result.move, "d3")
        self.assertEqual(result.exit_code, 0)
        self.assertFalse(result.timed_out)

    def test_fake_engine_non_zero_exit_is_error_result(self) -> None:
        result = request_best_move(
            NTestConfig(
                command=[sys.executable, str(FAKE_ENGINE), "--move", "d3", "--exit-code", "7"],
                timeout_ms=1000,
            ),
            "board\n",
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 7)

    def test_fake_engine_timeout_is_detected(self) -> None:
        result = request_best_move(
            NTestConfig(
                command=[sys.executable, str(FAKE_ENGINE), "--move", "d3", "--sleep-ms", "200"],
                timeout_ms=20,
            ),
            "board\n",
        )

        self.assertIsNone(result.move)
        self.assertTrue(result.timed_out)

    def test_run_ntest_once_cli_success(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_NTEST_ONCE),
                "--stdin-board",
                "--ntest-cmd",
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

    def test_run_ntest_once_cli_accepts_board_file(self) -> None:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as board_file:
            board_file.write("board\n")
            board_path = Path(board_file.name)
        self.addCleanup(board_path.unlink)

        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_NTEST_ONCE),
                "--board-file",
                str(board_path),
                "--ntest-cmd",
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

    def test_run_ntest_once_cli_invalid_move_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_NTEST_ONCE),
                "--stdin-board",
                "--ntest-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "z9",
            ],
            cwd=REPO_ROOT,
            input="board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("error:", completed.stdout)

    def test_run_ntest_once_cli_timeout_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_NTEST_ONCE),
                "--stdin-board",
                "--timeout-ms",
                "20",
                "--ntest-cmd",
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

    def test_run_ntest_once_cli_non_zero_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_NTEST_ONCE),
                "--stdin-board",
                "--ntest-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--exit-code",
                "7",
            ],
            cwd=REPO_ROOT,
            input="board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("exit_code: 7", completed.stdout)

    def test_run_ntest_once_cli_does_not_capture_engine_help(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_NTEST_ONCE),
                "--stdin-board",
                "--ntest-cmd",
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
        self.assertIn("error: ntest produced no recognizable move", completed.stdout)
        self.assertNotIn("Run one NTest best-move request", completed.stdout)

    def test_ntest_adapter_passes_engine_help_to_command(self) -> None:
        result = request_best_move(
            NTestConfig(command=[sys.executable, str(FAKE_ENGINE), "--help"], timeout_ms=1000),
            "board\n",
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 0)
        self.assertIn("usage: fake_engine.py", result.raw_output)
        self.assertEqual(result.error, "ntest produced no recognizable move")

    def test_ntest_adapter_rejects_unknown_profile(self) -> None:
        with self.assertRaisesRegex(ExternalEngineError, "unknown NTest protocol profile"):
            request_best_move(
                NTestConfig(
                    command=[sys.executable, str(FAKE_ENGINE), "--move", "d3"],
                    timeout_ms=1000,
                    profile="unknown",
                ),
                "board\n",
            )


if __name__ == "__main__":
    unittest.main()
