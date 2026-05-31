from __future__ import annotations

import collections
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_teacher_v0_train as trainer  # noqa: E402
from common import ScriptError  # noqa: E402


class PatternTeacherTrainTests(unittest.TestCase):
    def test_split_ratio_parser_requires_three_nonzero_parts(self) -> None:
        ratios = trainer.parse_split_ratios("60,20,20")

        self.assertEqual(ratios.train, 60)
        self.assertEqual(ratios.validation, 20)
        self.assertEqual(ratios.holdout, 20)
        self.assertEqual(ratios.total, 100)
        with self.assertRaises(ScriptError):
            trainer.parse_split_ratios("80,20")
        with self.assertRaises(ScriptError):
            trainer.parse_split_ratios("60,40,0")

    def test_split_name_for_row_is_deterministic(self) -> None:
        row = {
            "board_text": "........\n........\n........\n...WB...\n...BW...\n........\n........\n........\nside=B",
            "move": "d3",
        }
        ratios = trainer.parse_split_ratios("60,20,20")

        first = trainer.split_name_for_row(row, ratios, 20260601)
        second = trainer.split_name_for_row(row, ratios, 20260601)

        self.assertIn(first, {"train", "validation", "holdout"})
        self.assertEqual(first, second)

    def test_family_parser_expands_aliases_deterministically(self) -> None:
        self.assertEqual(
            trainer.parse_families("broad_all"),
            ("corner_3x3", "edge_8", "edge_x_10", "diagonal_8", "inner_row_8"),
        )
        self.assertEqual(
            trainer.parse_families("corner_only,edge_context_only,corner_only"),
            ("corner_3x3", "edge_x_10"),
        )
        with self.assertRaises(ScriptError):
            trainer.parse_families("missing_family")

    def test_write_split_files_is_deterministic(self) -> None:
        rows = [
            {
                "board_text": f"........\n........\n........\n...WB...\n...BW...\n........\n........\n.......{i}\nside=B",
                "move": "d3",
            }
            for i in range(3)
        ]
        ratios = trainer.parse_split_ratios("60,20,20")
        with tempfile.TemporaryDirectory() as temp:
            out_dir = Path(temp)
            trainer.write_split_files(rows, out_dir, ratios, 20260601)
            first = {path.name: path.read_text(encoding="utf-8") for path in out_dir.iterdir()}
            trainer.write_split_files(rows, out_dir, ratios, 20260601)
            second = {path.name: path.read_text(encoding="utf-8") for path in out_dir.iterdir()}

        self.assertEqual(first, second)
        self.assertEqual(set(first), {"teacher_train.jsonl", "teacher_validation.jsonl", "teacher_holdout.jsonl"})

    def test_sparse_entries_are_deterministic_and_antisymmetric(self) -> None:
        counts: collections.Counter[int] = collections.Counter({1: 9, trainer.swapped_index(1, 6): -9})

        first = trainer.sparse_entries(
            counts,
            cells=6,
            limit_pairs=8,
            min_abs_diff=3,
            scale=3,
            max_abs_weight=4,
        )
        second = trainer.sparse_entries(
            counts,
            cells=6,
            limit_pairs=8,
            min_abs_diff=3,
            scale=3,
            max_abs_weight=4,
        )

        self.assertEqual(first, second)
        self.assertIn((1, 4), first)
        self.assertIn((trainer.swapped_index(1, 6), -4), first)

    def test_broad_pattern_indexes_are_deterministic(self) -> None:
        board = (
            "BWB.....\n"
            "WB....W.\n"
            "..W.....\n"
            "........\n"
            "........\n"
            "........\n"
            "........\n"
            "........\n"
            "side=B"
        )
        families = trainer.parse_families("broad_all")

        first = trainer.pattern_indexes_by_family(board, "B", families)
        second = trainer.pattern_indexes_by_family(board, "B", families)

        self.assertEqual(first, second)
        self.assertEqual(first["corner_3x3"][0], 13273)
        self.assertEqual(first["edge_x_10"][0], 45943)
        self.assertEqual(first["diagonal_8"][0], 22)
        self.assertEqual(first["inner_row_8"][0], 1463)

    def test_render_table_is_deterministic(self) -> None:
        text = trainer.render_table(
            corner_entries=[(1, 2), (3, -2)],
            edge_entries=[(9, 1)],
            stats={"residual_updates": 2, "already_agreed": 1},
            command=["pattern_teacher_v0_train.py", "--split", "train"],
        )

        self.assertEqual(
            text,
            "# schema_version: pattern_table.v1\n"
            "# name: pattern_teacher_v0\n"
            "# generated_by: tools/scripts/pattern_teacher_v0_train.py\n"
            "# command: pattern_teacher_v0_train.py --split train\n"
            "# already_agreed: 1\n"
            "# residual_updates: 2\n"
            "\n"
            "corner_2x3\t1\t2\n"
            "corner_2x3\t3\t-2\n"
            "edge_8\t9\t1\n",
        )

    def test_render_table_supports_multiple_families(self) -> None:
        text = trainer.render_table(
            name="classic_pattern_v0",
            family_entries={
                "edge_x_10": [(1, 2), (2, -2)],
                "corner_3x3": [(4, 1), (8, -1)],
                "inner_row_8": [(16, 3), (32, -3)],
            },
            stats={"residual_updates": 1},
            command=["pattern_teacher_v0_train.py", "--families", "broad_all"],
        )

        self.assertEqual(
            text,
            "# schema_version: pattern_table.v1\n"
            "# name: classic_pattern_v0\n"
            "# generated_by: tools/scripts/pattern_teacher_v0_train.py\n"
            "# command: pattern_teacher_v0_train.py --families broad_all\n"
            "# residual_updates: 1\n"
            "\n"
            "corner_3x3\t4\t1\n"
            "corner_3x3\t8\t-1\n"
            "edge_x_10\t1\t2\n"
            "edge_x_10\t2\t-2\n"
            "inner_row_8\t16\t3\n"
            "inner_row_8\t32\t-3\n",
        )


if __name__ == "__main__":
    unittest.main()
