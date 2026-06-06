from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from external_engines.common import ExternalEngineError  # noqa: E402
from external_engines.ntest import (  # noqa: E402
    NTestConfig,
    normalize_ntest_move,
    parse_ntest_move_output,
    request_best_move,
)


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"


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
