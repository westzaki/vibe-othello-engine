from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_specs  # noqa: E402


class PatternSpecsTests(unittest.TestCase):
    def test_family_order_and_spec_shapes_are_stable(self) -> None:
        expected_shapes = {
            "corner_2x3": (4, 6),
            "corner_3x3": (4, 9),
            "edge_8": (4, 8),
            "edge_x_10": (4, 10),
            "diagonal_8": (2, 8),
            "inner_row_8": (4, 8),
        }

        self.assertEqual(tuple(expected_shapes), pattern_specs.FAMILY_ORDER)
        for family, (spec_count, cell_count) in expected_shapes.items():
            specs = pattern_specs.PATTERN_SPECS[family]
            self.assertEqual(len(specs), spec_count)
            self.assertTrue(all(len(spec) == cell_count for spec in specs))

    def test_common_aliases_expand_to_valid_families(self) -> None:
        self.assertEqual(pattern_specs.COMMON_FAMILY_ALIASES["legacy"], ("corner_2x3", "edge_8"))
        self.assertEqual(
            pattern_specs.COMMON_FAMILY_ALIASES["broad_all"],
            ("corner_3x3", "edge_8", "edge_x_10", "diagonal_8", "inner_row_8"),
        )
        for families in pattern_specs.COMMON_FAMILY_ALIASES.values():
            self.assertTrue(all(family in pattern_specs.PATTERN_SPECS for family in families))

    def test_pattern_index_accepts_string_and_list_rows(self) -> None:
        rows = [
            "BWB.WB.W",
            "WB..BW.B",
            "..W.B...",
            "...BW...",
            "...WB...",
            "...B.W..",
            "B.W..WB.",
            "W.BW.BWB",
        ]
        spec = pattern_specs.PATTERN_SPECS["corner_2x3"][0]

        self.assertEqual(pattern_specs.pattern_index(rows, "B", spec), 151)
        self.assertEqual(pattern_specs.pattern_index([list(row) for row in rows], "B", spec), 151)


if __name__ == "__main__":
    unittest.main()
