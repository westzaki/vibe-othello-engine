from __future__ import annotations

import collections
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_teacher_v0_train as trainer  # noqa: E402
from common import ScriptError  # noqa: E402


PATTERN_FIXTURE_INSTANCES = {
    "corner_2x3": ("A1", "H1", "A8", "H8"),
    "corner_3x3": ("A1", "H1", "A8", "H8"),
    "edge_8": ("Top", "Bottom", "Left", "Right"),
    "edge_x_10": ("Top", "Bottom", "Left", "Right"),
    "diagonal_8": ("A1H8", "H1A8"),
    "inner_row_8": ("Top", "Bottom", "Left", "Right"),
}


def load_pattern_index_fixtures() -> list[dict[str, object]]:
    path = REPO_ROOT / "tests" / "fixtures" / "pattern_index_drift.txt"
    fixtures: list[dict[str, object]] = []
    current: dict[str, object] | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        parts = line.split()
        if parts[0] == "fixture":
            if current is not None:
                fixtures.append(current)
            current = {
                "name": parts[1],
                "side_to_move": "B",
                "rows": [],
                "expectations": [],
            }
            continue

        assert current is not None
        if parts[0] == "side_to_move":
            current["side_to_move"] = parts[1]
        elif parts[0] == "row":
            rows = current["rows"]
            assert isinstance(rows, list)
            rows.append(parts[1])
        elif parts[0] == "expect":
            expectations = current["expectations"]
            assert isinstance(expectations, list)
            indexes: list[tuple[str, int]] = []
            for token in parts[3:]:
                name, _, value = token.partition("=")
                indexes.append((name, int(value)))
            expectations.append(
                {
                    "side": parts[1],
                    "family": parts[2],
                    "indexes": indexes,
                }
            )
        else:
            raise AssertionError(f"unknown fixture keyword: {parts[0]}")

    if current is not None:
        fixtures.append(current)
    return fixtures


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

    def test_parse_label_paths_supports_dataset_references(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "datasets"
            root.mkdir()

            paths = trainer.parse_label_paths(
                "dataset:teacher/labels.jsonl, local.jsonl",
                dataset_root=str(root),
            )

        self.assertEqual(paths[0], (root / "teacher" / "labels.jsonl").resolve(strict=False))
        self.assertEqual(paths[1], Path("local.jsonl"))

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
            "........\n"
            "........\n"
            "........\n"
            "........\n"
            "........\n"
            "..W.....\n"
            "WB....W.\n"
            "BWB.....\n"
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

    def test_pattern_index_fixture_matches_expected_values(self) -> None:
        fixtures = load_pattern_index_fixtures()

        self.assertTrue(fixtures)
        for fixture in fixtures:
            rows = fixture["rows"]
            self.assertIsInstance(rows, list)
            self.assertEqual(len(rows), 8)
            board9_rows = list(reversed(rows))
            board = "\n".join([*board9_rows, f"side={fixture['side_to_move']}"])

            expectations = fixture["expectations"]
            self.assertIsInstance(expectations, list)
            self.assertEqual(len(expectations), 12)
            for expectation in expectations:
                family = expectation["family"]
                side = expectation["side"]
                indexes = expectation["indexes"]

                self.assertIn(family, PATTERN_FIXTURE_INSTANCES)
                self.assertEqual(
                    [name for name, _ in indexes],
                    list(PATTERN_FIXTURE_INSTANCES[family]),
                )
                self.assertEqual(
                    trainer.pattern_indexes_by_family(board, side, (family,))[family],
                    [value for _, value in indexes],
                )

    def test_pattern_index_fixture_would_fail_without_board9_row_reversal(self) -> None:
        fixture = load_pattern_index_fixtures()[0]
        rows = fixture["rows"]
        self.assertIsInstance(rows, list)
        board9_rows = list(reversed(rows))
        board = "\n".join([*board9_rows, f"side={fixture['side_to_move']}"])
        converted = trainer.board9_rows_to_square_index_rows(trainer.parse_board(board)[0])

        for family in PATTERN_FIXTURE_INSTANCES:
            with self.subTest(family=family):
                expected = trainer.pattern_index(
                    converted,
                    "B",
                    trainer.PATTERN_SPECS[family][0],
                )
                display_order_index = trainer.pattern_index(
                    board9_rows,
                    "B",
                    trainer.PATTERN_SPECS[family][0],
                )
                self.assertNotEqual(display_order_index, expected)

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
            name="broad_pattern_fixture",
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
            "# name: broad_pattern_fixture\n"
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
