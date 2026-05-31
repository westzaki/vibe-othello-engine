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


if __name__ == "__main__":
    unittest.main()
