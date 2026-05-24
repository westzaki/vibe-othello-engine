from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from external_engines.common import ExternalEngineError  # noqa: E402
from external_engines.ntest import (  # noqa: E402
    NTestConfig,
    normalize_ntest_move,
    parse_ntest_move_output,
    request_best_move,
)


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"
RUN_ONCE = SCRIPT_DIR / "run_external_engine_once.py"


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

    def test_fake_engine_move_is_received_with_nboard_protocol(self) -> None:
        result = request_best_move(
            NTestConfig(
                command=[sys.executable, str(FAKE_ENGINE), "--move", "=== D3//0.00"],
                timeout_ms=1000,
                profile="nboard",
            ),
            "ggf-board\n",
        )

        self.assertEqual(result.move, "d3")
        self.assertEqual(result.exit_code, 0)
        self.assertFalse(result.timed_out)

    def test_fake_engine_move_is_received_with_one_shot_protocol(self) -> None:
        result = request_best_move(
            NTestConfig(
                command=[sys.executable, str(FAKE_ENGINE), "--move", "D3"],
                timeout_ms=1000,
                profile="one-shot",
            ),
            "board\n",
        )

        self.assertEqual(result.move, "d3")
        self.assertEqual(result.exit_code, 0)
        self.assertFalse(result.timed_out)

    def test_empty_output_is_error_result(self) -> None:
        result = request_best_move(
            NTestConfig(command=[sys.executable, str(FAKE_ENGINE)], timeout_ms=1000),
            "ggf-board\n",
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.error, "ntest produced no recognizable move")

    def test_fake_engine_non_zero_exit_is_error_result(self) -> None:
        result = request_best_move(
            NTestConfig(
                command=[sys.executable, str(FAKE_ENGINE), "--move", "d3", "--exit-code", "7"],
                timeout_ms=1000,
            ),
            "ggf-board\n",
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 7)

    def test_fake_engine_timeout_is_detected(self) -> None:
        result = request_best_move(
            NTestConfig(
                command=[sys.executable, str(FAKE_ENGINE), "--move", "d3", "--sleep-ms", "200"],
                timeout_ms=20,
            ),
            "ggf-board\n",
        )

        self.assertIsNone(result.move)
        self.assertTrue(result.timed_out)

    def test_unknown_protocol_is_rejected(self) -> None:
        with self.assertRaisesRegex(ExternalEngineError, "unknown NTest protocol profile"):
            request_best_move(
                NTestConfig(
                    command=[sys.executable, str(FAKE_ENGINE), "--move", "d3"],
                    timeout_ms=1000,
                    profile="unknown",
                ),
                "ggf-board\n",
            )

    def test_run_external_engine_once_cli_ntest_success(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "ntest",
                "--protocol",
                "nboard",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "=== D3//0.00",
            ],
            cwd=REPO_ROOT,
            input="ggf-board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 0)
        self.assertIn("move: d3", completed.stdout)

    def test_run_external_engine_once_cli_ntest_invalid_move_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "ntest",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "z9",
            ],
            cwd=REPO_ROOT,
            input="ggf-board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("error: ntest produced no recognizable move", completed.stdout)

    def test_run_external_engine_once_cli_ntest_timeout_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "ntest",
                "--timeout-ms",
                "20",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "=== D3//0.00",
                "--sleep-ms",
                "200",
            ],
            cwd=REPO_ROOT,
            input="ggf-board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("timed_out: true", completed.stdout)

    def test_run_external_engine_once_cli_ntest_non_zero_is_failure(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "ntest",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--exit-code",
                "7",
            ],
            cwd=REPO_ROOT,
            input="ggf-board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 1)
        self.assertIn("exit_code: 7", completed.stdout)

    def test_run_external_engine_once_cli_unknown_adapter_is_error(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "unknown",
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

        self.assertEqual(completed.returncode, 2)
        self.assertIn("invalid choice", completed.stderr)

    def test_run_external_engine_once_cli_invalid_protocol_is_error(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "one-shot",
                "--protocol",
                "nboard",
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

        self.assertEqual(completed.returncode, 2)
        self.assertIn("--adapter one-shot only allows --protocol one-shot", completed.stderr)

    def test_run_external_engine_once_cli_invalid_depth_is_error(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "ntest",
                "--depth",
                "0",
                "--engine-cmd",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
                "--move",
                "d3",
            ],
            cwd=REPO_ROOT,
            input="ggf-board\n",
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(completed.returncode, 2)
        self.assertIn("--depth must be positive", completed.stderr)

    def test_run_external_engine_once_cli_rejects_one_shot_depth(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "one-shot",
                "--depth",
                "3",
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

        self.assertEqual(completed.returncode, 2)
        self.assertIn("--depth is only valid", completed.stderr)

    def test_engine_command_help_is_not_captured_by_ntest_adapter(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(RUN_ONCE),
                "--stdin-board",
                "--adapter",
                "ntest",
                "--protocol",
                "one-shot",
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
        self.assertIn("error: ntest produced no recognizable move", completed.stdout)
        self.assertNotIn("Run one external engine request", completed.stdout)

    def test_ntest_adapter_passes_engine_help_to_command(self) -> None:
        result = request_best_move(
            NTestConfig(
                command=[sys.executable, str(FAKE_ENGINE), "--help"],
                timeout_ms=1000,
                profile="one-shot",
            ),
            "board\n",
        )

        self.assertIsNone(result.move)
        self.assertEqual(result.exit_code, 0)
        self.assertIn("usage: fake_engine.py", result.raw_output)
        self.assertEqual(result.error, "ntest produced no recognizable move")


if __name__ == "__main__":
    unittest.main()
