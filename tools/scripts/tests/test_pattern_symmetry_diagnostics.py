from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_symmetry_diagnostics as diagnostics  # noqa: E402


class PatternSymmetryDiagnosticsTests(unittest.TestCase):
    def test_synthetic_transform_diagnostic_preserves_legality_and_pairs(self) -> None:
        report = diagnostics.run_synthetic_transform_diagnostic()

        self.assertEqual(report["color_side_relative_invariance_failures"], [])
        self.assertGreater(report["base_feature_count"], 0)
        for row in report["transforms"]:
            with self.subTest(transform=row["transform"]):
                self.assertTrue(row["legal_moves_ok"])
                self.assertTrue(row["teacher_child_ok"])
                self.assertTrue(row["other_child_ok"])
                self.assertTrue(row["features_nonempty"])
                self.assertIn(row["teacher_move"], row["root_scores"])
                self.assertEqual(row["exact_best"], (row["teacher_move"],))

    def test_spec_report_marks_row_column_as_cross_family_not_shared(self) -> None:
        cross_report = diagnostics.cross_family_sharing_report()

        row_column = cross_report["row_8_to_column_8"]
        self.assertFalse(row_column["same_table"])
        self.assertGreater(row_column["exact_order_mappings"], 0)
        self.assertGreater(row_column["reversed_index_mappings"], 0)

    def test_tsv_violation_report_detects_reversed_index_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "table.tsv"
            path.write_text(
                "# schema_version: pattern_table.v1\n"
                "edge_8\t1\t5\n"
                "edge_8\t2187\t0\n",
                encoding="utf-8",
            )

            weights = diagnostics.read_pattern_table(path)
            summaries = diagnostics.table_symmetry_violation_report(weights)

        by_label = {summary.label: summary for summary in summaries}
        self.assertIn("index_vs_reversed_index", by_label)
        self.assertGreater(by_label["index_vs_reversed_index"].violations, 0)
        self.assertGreaterEqual(by_label["index_vs_reversed_index"].max_abs_delta, 5)


if __name__ == "__main__":
    unittest.main()
