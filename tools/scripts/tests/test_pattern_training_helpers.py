from __future__ import annotations

import collections
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from common import ScriptError  # noqa: E402
from pattern_training import dataset, pattern_tables, preference_features, splits  # noqa: E402


BOARD = (
    "........\n"
    "........\n"
    "........\n"
    "...WB...\n"
    "...BW...\n"
    "........\n"
    "........\n"
    "........\n"
    "side=B"
)

TEACHER_CHILD = (
    "BBB.....\n"
    "BB....B.\n"
    "..B.....\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "side=B"
)

COMPARED_CHILD = (
    "WWW.....\n"
    "WW....W.\n"
    "..W.....\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "side=B"
)


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> None:
    path.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
        encoding="utf-8",
    )


class PatternTrainingHelperTests(unittest.TestCase):
    def test_split_ratios_and_row_split_are_deterministic(self) -> None:
        ratios = splits.parse_split_ratios("60,20,20")
        row = {"board_text": BOARD, "move": "d3"}

        self.assertEqual(ratios.total, 100)
        self.assertEqual(
            splits.split_name_for_row(row, ratios, 20260601),
            splits.split_name_for_row(row, ratios, 20260601),
        )
        with self.assertRaises(ScriptError):
            splits.parse_split_ratios("60,40,0")

    def test_dataset_loaders_filter_accepted_rows_and_exact_best_moves(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            teacher = temp_path / "teacher.jsonl"
            exact = temp_path / "exact.jsonl"
            write_jsonl(
                teacher,
                [
                    {
                        "status": "ok",
                        "legal_move_valid": True,
                        "move_token_valid": True,
                        "move": "d3",
                        "board_text": BOARD,
                    },
                    {"status": "error", "move": "c4", "board_text": BOARD},
                ],
            )
            write_jsonl(exact, [{"board": BOARD, "best_moves": ["d3", "c4"]}])

            rows = dataset.accepted_teacher_rows([teacher], limit=None)
            exact_best = dataset.load_exact_best([exact])

        self.assertEqual(len(rows), 1)
        self.assertEqual(exact_best[dataset.board_key(BOARD)], {"d3", "c4"})

    def test_pattern_tables_sparse_entries_are_antisymmetric(self) -> None:
        partner = pattern_tables.swapped_index(1, 6)
        counts: collections.Counter[int] = collections.Counter({1: 9, partner: -9})

        entries = pattern_tables.sparse_entries(
            counts,
            cells=6,
            limit_pairs=8,
            min_abs_diff=3,
            scale=3,
            max_abs_weight=4,
        )

        self.assertEqual(entries, [(1, 4), (partner, -4)])

    def test_preference_update_counts_teacher_minus_compared_features(self) -> None:
        families = pattern_tables.parse_families("corner_only")
        counts = {family: collections.Counter[int]() for family in families}

        preference_features.apply_preference_update(
            board_text=BOARD,
            teacher_child_board=TEACHER_CHILD,
            compared_child_board=COMPARED_CHILD,
            family_counts=counts,
            families=families,
        )

        total_delta = sum(abs(value) for counter in counts.values() for value in counter.values())
        self.assertGreater(total_delta, 0)


if __name__ == "__main__":
    unittest.main()
