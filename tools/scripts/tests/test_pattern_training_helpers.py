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

    def test_write_split_files_is_deterministic(self) -> None:
        rows = [{"board_text": BOARD, "move": move} for move in ("d3", "c4", "f5")]
        ratios = splits.parse_split_ratios("60,20,20")
        with tempfile.TemporaryDirectory() as temp:
            out_dir = Path(temp)
            splits.write_split_files(rows, out_dir, ratios, 20260601)
            first = {path.name: path.read_text(encoding="utf-8") for path in out_dir.iterdir()}
            splits.write_split_files(rows, out_dir, ratios, 20260601)
            second = {path.name: path.read_text(encoding="utf-8") for path in out_dir.iterdir()}

        self.assertEqual(first, second)
        self.assertEqual(
            set(first),
            {"teacher_train.jsonl", "teacher_validation.jsonl", "teacher_holdout.jsonl"},
        )

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

    def test_pattern_table_rendering_is_deterministic(self) -> None:
        text = pattern_tables.render_table(
            corner_entries=[(1, 2), (3, -2)],
            edge_entries=[(9, 1)],
            stats={"residual_updates": 2, "already_agreed": 1},
            command=["helper-test", "--split", "train"],
            generated_by="helper-test",
        )

        self.assertEqual(
            text,
            "# schema_version: pattern_table.v1\n"
            "# name: pattern_teacher_v0\n"
            "# generated_by: helper-test\n"
            "# command: helper-test --split train\n"
            "# already_agreed: 1\n"
            "# residual_updates: 2\n"
            "\n"
            "corner_2x3\t1\t2\n"
            "corner_2x3\t3\t-2\n"
            "edge_8\t9\t1\n",
        )

    def test_pattern_table_rendering_supports_multiple_families(self) -> None:
        text = pattern_tables.render_table(
            name="broad_pattern_fixture",
            family_entries={
                "edge_x_10": [(1, 2), (2, -2)],
                "corner_3x3": [(4, 1), (8, -1)],
                "inner_row_8": [(16, 3), (32, -3)],
            },
            stats={"residual_updates": 1},
            command=["helper-test", "--families", "broad_all"],
            generated_by="helper-test",
        )

        self.assertEqual(
            text,
            "# schema_version: pattern_table.v1\n"
            "# name: broad_pattern_fixture\n"
            "# generated_by: helper-test\n"
            "# command: helper-test --families broad_all\n"
            "# residual_updates: 1\n"
            "\n"
            "corner_3x3\t4\t1\n"
            "corner_3x3\t8\t-1\n"
            "edge_x_10\t1\t2\n"
            "edge_x_10\t2\t-2\n"
            "inner_row_8\t16\t3\n"
            "inner_row_8\t32\t-3\n",
        )

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
