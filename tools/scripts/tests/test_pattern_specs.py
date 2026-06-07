from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_specs  # noqa: E402


class PatternSpecsTests(unittest.TestCase):
    def test_family_order_and_spec_shapes_are_stable(self) -> None:
        expected_shapes = {
            "corner_2x3": (4, 6),
            "corner_3x3": (4, 9),
            "edge_8": (4, 8),
            "edge_x_10": (4, 10),
            "row_8": (8, 8),
            "column_8": (8, 8),
            "diagonal_4": (4, 4),
            "diagonal_5": (4, 5),
            "diagonal_6": (4, 6),
            "diagonal_7": (4, 7),
            "diagonal_8": (2, 8),
            "inner_row_8": (4, 8),
            "corner_2x4": (4, 8),
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
        self.assertEqual(
            pattern_specs.COMMON_FAMILY_ALIASES["broad_v2"],
            (
                "corner_3x3",
                "edge_8",
                "edge_x_10",
                "diagonal_8",
                "inner_row_8",
                "row_8",
                "column_8",
                "diagonal_4",
                "diagonal_5",
                "diagonal_6",
                "diagonal_7",
                "corner_2x4",
            ),
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

    def test_board9_rows_to_square_index_rows_reverses_display_order(self) -> None:
        square_index_rows = [
            "BWB.WB.W",
            "WB..BW.B",
            "..W.B...",
            "...BW...",
            "...WB...",
            "...B.W..",
            "B.W..WB.",
            "W.BW.BWB",
        ]
        board9_rows = list(reversed(square_index_rows))
        spec = pattern_specs.PATTERN_SPECS["corner_2x3"][0]

        converted = pattern_specs.board9_rows_to_square_index_rows(board9_rows)

        self.assertEqual(converted, tuple(square_index_rows))
        self.assertEqual(pattern_specs.pattern_index(converted, "B", spec), 151)
        self.assertNotEqual(pattern_specs.pattern_index(board9_rows, "B", spec), 151)

    def test_parse_board9_square_index_rows_matches_cpp_fixture(self) -> None:
        fixture = REPO_ROOT / "tests" / "fixtures" / "pattern_index_drift.txt"
        rows = [
            line.split()[1]
            for line in fixture.read_text(encoding="utf-8").splitlines()
            if line.startswith("row ")
        ]
        self.assertEqual(len(rows), 8)
        board9_rows = list(reversed(rows))
        board9 = "\n".join(board9_rows) + "\nside=B"

        converted = pattern_specs.parse_board9_square_index_rows(board9)

        self.assertEqual(converted, tuple(rows))
        for family in (
            "corner_2x3",
            "corner_3x3",
            "edge_8",
            "edge_x_10",
            "row_8",
            "column_8",
            "diagonal_4",
            "diagonal_5",
            "diagonal_6",
            "diagonal_7",
            "diagonal_8",
            "inner_row_8",
            "corner_2x4",
        ):
            with self.subTest(family=family):
                spec = pattern_specs.PATTERN_SPECS[family][0]
                expected_index = pattern_specs.pattern_index(converted, "B", spec)
                flipped_index = pattern_specs.pattern_index(board9_rows, "B", spec)
                self.assertNotEqual(flipped_index, expected_index)


if __name__ == "__main__":
    unittest.main()
