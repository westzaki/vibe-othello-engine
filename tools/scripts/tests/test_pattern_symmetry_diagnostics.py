from __future__ import annotations

import sys
import tempfile
import unittest
import json
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_symmetry_diagnostics as diagnostics  # noqa: E402
from common import ScriptError  # noqa: E402


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

    def test_reader_rejects_duplicate_family_index(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "table.tsv"
            path.write_text(
                "edge_8\t1\t5\n"
                "edge_8\t1\t6\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ScriptError, "duplicate entry"):
                diagnostics.read_pattern_table(path)

    def test_reversed_index_symmetrize_averages_pair_and_preserves_input(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            input_path = temp_path / "input.tsv"
            output_path = temp_path / "output.tsv"
            original = "edge_8\t1\t5\nedge_8\t2187\t0\n"
            input_path.write_text(original, encoding="utf-8")

            symmetrized, summary, before, after = diagnostics.symmetrize_pattern_table(
                input_path=input_path,
                output_path=output_path,
                modes=("reversed-index",),
            )

            self.assertEqual(input_path.read_text(encoding="utf-8"), original)
            self.assertEqual(symmetrized["edge_8"][1], 2)
            self.assertEqual(symmetrized["edge_8"][2187], 2)
            self.assertEqual(summary.entries_read, 2)
            self.assertEqual(summary.changed_entries, 2)
            self.assertTrue(output_path.exists())
            before_by_label = {row.label: row for row in before}
            after_by_label = {row.label: row for row in after}
            self.assertGreater(before_by_label["index_vs_reversed_index"].violations, 0)
            self.assertEqual(after_by_label["index_vs_reversed_index"].violations, 0)

    def test_color_inversion_symmetrize_enforces_antisymmetry(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            input_path = temp_path / "input.tsv"
            output_path = temp_path / "output.tsv"
            input_path.write_text("edge_8\t1\t5\nedge_8\t2\t1\n", encoding="utf-8")

            symmetrized, summary, before, after = diagnostics.symmetrize_pattern_table(
                input_path=input_path,
                output_path=output_path,
                modes=("color-inversion",),
            )

            self.assertEqual(symmetrized["edge_8"][1], 2)
            self.assertEqual(symmetrized["edge_8"][2], -2)
            self.assertEqual(summary.violations_after["index_vs_color_inverted_index"], 0)
            before_by_label = {row.label: row for row in before}
            after_by_label = {row.label: row for row in after}
            self.assertGreater(before_by_label["index_vs_color_inverted_index"].violations, 0)
            self.assertEqual(after_by_label["index_vs_color_inverted_index"].violations, 0)

    def test_symmetrize_weights_multiple_modes_are_order_independent(self) -> None:
        weights = {
            "edge_8": {
                1: 5,
                2: 1,
                2187: -3,
                4374: 4,
            }
        }

        first = diagnostics.symmetrize_weights(
            weights,
            modes=("reversed-index", "color-inversion"),
        )
        second = diagnostics.symmetrize_weights(
            weights,
            modes=("color-inversion", "reversed-index"),
        )

        self.assertEqual(first, second)

    def test_json_mode_smoke_writes_report_and_symmetrized_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            input_path = temp_path / "input.tsv"
            output_path = temp_path / "output.tsv"
            report_path = temp_path / "report.json"
            original = "edge_8\t1\t5\nedge_8\t2187\t0\n"
            input_path.write_text(original, encoding="utf-8")

            exit_code = diagnostics.main(
                [
                    "--pattern-table",
                    str(input_path),
                    "--symmetrize-output",
                    str(output_path),
                    "--symmetrize",
                    "reversed-index",
                    "--json",
                    "--out",
                    str(report_path),
                ]
            )

            payload = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(exit_code, 0)
            self.assertEqual(input_path.read_text(encoding="utf-8"), original)
            self.assertTrue(output_path.exists())
            self.assertEqual(
                payload["symmetrize_summary"]["violations_after"]["index_vs_reversed_index"],
                0,
            )
            self.assertEqual(payload["symmetrize_summary"]["entries_read"], 2)


if __name__ == "__main__":
    unittest.main()
