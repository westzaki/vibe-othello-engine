from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from external_engines.one_shot import request_best_move  # noqa: E402


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"


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

    def test_engine_command_help_is_passed_to_adapter_command(self) -> None:
        result = request_best_move(
            [sys.executable, str(FAKE_ENGINE), "--help"],
            board_text="board\n",
            timeout_ms=1000,
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 0)
        self.assertIn("usage: fake_engine.py", result.raw_output)
        self.assertTrue(result.error.startswith("invalid move format: usage: fake_engine.py"))


if __name__ == "__main__":
    unittest.main()
