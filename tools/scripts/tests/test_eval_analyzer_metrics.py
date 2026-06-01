from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_analyzer_metrics as metrics  # noqa: E402
from common import ScriptError  # noqa: E402


class EvalAnalyzerMetricsTests(unittest.TestCase):
    def test_required_analyzer_stdout_metrics_are_parsed(self) -> None:
        stdout = (
            "eval vs exact: records_read=5 analyzed=5 skipped=0 sign_agreements=4 "
            "wrong_direction=1 high_confidence_wrong_direction=0 output=report.md"
        )

        parsed = metrics.parse_analyzer_stdout(stdout)

        self.assertEqual(parsed.records_read, 5)
        self.assertEqual(parsed.analyzed, 5)
        self.assertEqual(parsed.sign_agreements, 4)
        self.assertEqual(parsed.wrong_direction, 1)
        self.assertEqual(parsed.high_confidence_wrong_direction, 0)
        self.assertIsNone(parsed.move_rank_analyzed)

    def test_optional_move_rank_metrics_are_parsed_and_rendered(self) -> None:
        stdout = (
            "eval vs exact: records_read=5 analyzed=5 skipped=0 sign_agreements=4 "
            "wrong_direction=1 high_confidence_wrong_direction=0 "
            "move_rank_records_with_scores=3 "
            "move_rank_records_missing_scores=2 "
            "move_rank_records_no_legal_root_moves=0 "
            "move_rank_analyzed=3 "
            "move_rank_top_exact_best=2 "
            "move_rank_top_non_best=1 "
            "move_rank_exact_best_rank_sum=4 "
            "move_rank_eval_score_gap_sum=17 "
            "move_rank_exact_score_gap_sum=9 "
            "output=report.md"
        )

        parsed = metrics.parse_analyzer_stdout(stdout)

        self.assertEqual(parsed.move_rank_records_with_scores, 3)
        self.assertEqual(parsed.move_rank_records_missing_scores, 2)
        self.assertEqual(parsed.move_rank_analyzed, 3)
        self.assertEqual(parsed.move_rank_top_exact_best, 2)
        self.assertEqual(parsed.move_rank_top_non_best, 1)
        self.assertEqual(parsed.move_rank_eval_score_gap_sum, 17)
        self.assertEqual(
            metrics.move_rank_cells(parsed),
            ["3", "2", "0", "3", "2", "1", "4", "17", "9"],
        )

    def test_missing_required_metric_is_rejected(self) -> None:
        with self.assertRaisesRegex(
            ScriptError,
            "analyzer stdout missing required metric: analyzed",
        ):
            metrics.parse_analyzer_stdout(
                "eval vs exact: records_read=5 sign_agreements=4 "
                "wrong_direction=1 high_confidence_wrong_direction=0"
            )

    def test_missing_move_rank_metrics_render_as_na(self) -> None:
        self.assertEqual(
            metrics.move_rank_cells(None),
            ["n/a"] * len(metrics.MOVE_RANK_METRIC_KEYS),
        )


if __name__ == "__main__":
    unittest.main()
