from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import phase_balanced_label_sample as sampler  # noqa: E402
from common import ScriptError  # noqa: E402
from pattern_training.board9 import apply_move_to_board, board_key, legal_moves_for_board  # noqa: E402


INITIAL_BOARD = (
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


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
        encoding="utf-8",
    )
    return path


def read_jsonl(path: Path) -> list[dict[str, object]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]


def write_eval_config(path: Path) -> Path:
    path.write_text(
        "# schema_version: eval.v1\n"
        "name=phase_sample_fixture\n"
        "opening_max_occupied=20\n"
        "midgame_max_occupied=44\n",
        encoding="utf-8",
    )
    return path


def board_after_moves(count: int, *, offset: int = 0) -> str:
    board = INITIAL_BOARD
    for index in range(count):
        moves = sorted(legal_moves_for_board(board))
        non_pass = [move for move in moves if move != "pass"]
        candidates = non_pass or moves
        board = apply_move_to_board(board, candidates[(index + offset) % len(candidates)])
    return board


def legal_teacher_move(board: str) -> str:
    return sorted(legal_moves_for_board(board))[0]


def teacher_row(
    board: str,
    *,
    position_id: str,
    split: str = "train",
    source_bucket: str = "fixture",
) -> dict[str, object]:
    return {
        "schema": "teacher_label.v1",
        "status": "ok",
        "legal_move_valid": True,
        "move_token_valid": True,
        "board_text": board,
        "move": legal_teacher_move(board),
        "position_split": split,
        "position_id": position_id,
        "source_bucket": source_bucket,
    }


def exact_row(board: str, *, complete: bool) -> dict[str, object]:
    best = legal_teacher_move(board)
    row: dict[str, object] = {
        "schema": "exact_label.v1",
        "board": board,
        "best_move": best,
        "best_moves": [best],
        "exact_score_side_to_move": 12,
    }
    if complete:
        row["move_scores"] = [
            {"move": move, "exact_score_side_to_move": 20 - index}
            for index, move in enumerate(sorted(legal_moves_for_board(board)))
        ]
    return row


def fixture_rows() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for phase, move_counts in {
        "opening": (0, 1, 2, 3),
        "midgame": (22, 24, 26, 28),
        "late": (46, 48, 50, 52),
    }.items():
        for index, move_count in enumerate(move_counts):
            rows.append(
                teacher_row(
                    board_after_moves(move_count, offset=index),
                    position_id=f"{phase}-{index}",
                    source_bucket=f"{phase}-bucket",
                )
            )
    return rows


def make_config(
    temp_path: Path,
    labels: Path,
    *,
    exact_labels: Path | None = None,
    out_name: str = "sample",
    rows: int = 6,
    seed: int = 20260607,
    extra_args: list[str] | None = None,
) -> sampler.SampleConfig:
    args = [
        "--teacher-labels",
        str(labels),
        "--eval-config",
        str(write_eval_config(temp_path / "base.eval")),
        "--out-dir",
        str(temp_path / "runs" / out_name),
        "--rows",
        str(rows),
        "--seed",
        str(seed),
    ]
    if exact_labels is not None:
        args.extend(["--exact-labels", str(exact_labels)])
    if extra_args is not None:
        args.extend(extra_args)
    with mock.patch.object(sampler, "REPO_ROOT", temp_path):
        return sampler.config_from_args(
            sampler.parse_args(args),
            invocation=["phase_balanced_label_sample.py", *args],
        )


class PhaseBalancedLabelSampleTests(unittest.TestCase):
    def test_rows_six_selects_two_per_phase_and_writes_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())
            summary = sampler.run(make_config(temp_path, labels))

            self.assertEqual(summary["requested_rows"], 6)
            self.assertEqual(summary["selected_rows"], 6)
            self.assertEqual(summary["phase_selected_rows"], {"opening": 2, "midgame": 2, "late": 2})
            self.assertEqual(summary["phase_shortage"], {"opening": 0, "midgame": 0, "late": 0})
            self.assertEqual(summary["phase_exact_targets"], {"opening": 0, "midgame": 0, "late": 0})
            self.assertEqual(
                summary["phase_complete_move_scores_targets"],
                {"opening": 0, "midgame": 0, "late": 0},
            )
            self.assertEqual(summary["phase_exact_target_shortage"], {"opening": 0, "midgame": 0, "late": 0})
            self.assertEqual(
                summary["phase_complete_move_scores_target_shortage"],
                {"opening": 0, "midgame": 0, "late": 0},
            )
            self.assertFalse(summary["prefer_exact_coverage"])
            self.assertTrue(summary["no_strength_claim"])
            self.assertFalse(summary["default_promotion"])
            report = (temp_path / "runs" / "sample" / "report.md").read_text(encoding="utf-8")
            self.assertIn("source_bucket", report)
            self.assertIn("legal_move_count", report)
            self.assertIn("Exact Coverage Targets", report)

    def test_parser_accepts_phase_exact_target_map(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())

            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--phase-exact-target",
                    "opening=1,midgame=0,late=2",
                    "--phase-complete-move-scores-target",
                    "opening=1,midgame=0,late=0",
                ],
            )

            self.assertEqual(config.phase_exact_targets, {"opening": 1, "midgame": 0, "late": 2})
            self.assertEqual(
                config.phase_complete_move_scores_targets,
                {"opening": 1, "midgame": 0, "late": 0},
            )

    def test_phase_exact_target_unknown_phase_rejects(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())

            with self.assertRaises(ScriptError):
                make_config(
                    temp_path,
                    labels,
                    extra_args=["--phase-exact-target", "early=1"],
                )

    def test_phase_exact_target_greater_than_phase_target_rejects(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())

            with self.assertRaises(ScriptError):
                make_config(
                    temp_path,
                    labels,
                    rows=6,
                    extra_args=["--phase-exact-target", "opening=3,midgame=0,late=0"],
                )

    def test_same_seed_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())

            sampler.run(make_config(temp_path, labels, out_name="a", seed=11))
            sampler.run(make_config(temp_path, labels, out_name="b", seed=11))

            left = [row["position_id"] for row in read_jsonl(temp_path / "runs" / "a" / "teacher_phase_balanced.jsonl")]
            right = [row["position_id"] for row in read_jsonl(temp_path / "runs" / "b" / "teacher_phase_balanced.jsonl")]
            self.assertEqual(left, right)

    def test_different_seed_can_change_selection_order(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())

            selections = set()
            for seed in range(1, 12):
                out_name = f"seed-{seed}"
                sampler.run(make_config(temp_path, labels, out_name=out_name, seed=seed))
                selections.add(
                    tuple(
                        row["position_id"]
                        for row in read_jsonl(
                            temp_path / "runs" / out_name / "teacher_phase_balanced.jsonl"
                        )
                    )
                )

            self.assertGreater(len(selections), 1)

    def test_duplicate_board_is_deduped(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = fixture_rows()
            duplicate = dict(rows[0])
            duplicate["position_id"] = "duplicate"
            labels = write_jsonl(temp_path / "labels.jsonl", [duplicate, *rows])

            summary = sampler.run(make_config(temp_path, labels, rows=9))
            output = read_jsonl(temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl")
            keys = [board_key(str(row["board_text"])) for row in output]

            self.assertEqual(len(keys), len(set(keys)))
            self.assertEqual(summary["teacher_stats"]["duplicate_teacher_boards"], 1)

    def test_short_phase_fails_by_default_before_writing_partial_subset(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = [row for row in fixture_rows() if not str(row["position_id"]).startswith("late-")]
            rows.append(teacher_row(board_after_moves(46), position_id="late-only"))
            labels = write_jsonl(temp_path / "labels.jsonl", rows)

            with self.assertRaises(ScriptError):
                sampler.run(make_config(temp_path, labels, rows=6))
            self.assertFalse((temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl").exists())

    def test_allow_shortage_reports_shortage_without_duplicate_sampling(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = [row for row in fixture_rows() if not str(row["position_id"]).startswith("late-")]
            rows.append(teacher_row(board_after_moves(46), position_id="late-only"))
            labels = write_jsonl(temp_path / "labels.jsonl", rows)

            summary = sampler.run(make_config(temp_path, labels, rows=6, extra_args=["--allow-shortage"]))
            output = read_jsonl(temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl")
            keys = [board_key(str(row["board_text"])) for row in output]

            self.assertEqual(summary["phase_selected_rows"]["late"], 1)
            self.assertEqual(summary["phase_shortage"]["late"], 1)
            self.assertLess(summary["selected_rows"], summary["requested_rows"])
            self.assertTrue(summary["allow_shortage"])
            self.assertEqual(len(keys), len(set(keys)))
            self.assertTrue(summary["top_underfilled_strata"])

    def test_exact_output_contains_only_selected_boards_and_coverage_by_phase(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = fixture_rows()
            labels = write_jsonl(temp_path / "labels.jsonl", rows)
            exact_rows = []
            for row in rows:
                position_id = str(row["position_id"])
                if position_id.startswith("opening-"):
                    exact_rows.append(exact_row(str(row["board_text"]), complete=True))
                elif position_id.startswith("midgame-"):
                    exact_rows.append(exact_row(str(row["board_text"]), complete=False))
            exact_rows.append(exact_row(board_after_moves(5), complete=True))
            exact = write_jsonl(temp_path / "exact.jsonl", exact_rows)

            summary = sampler.run(make_config(temp_path, labels, exact_labels=exact))
            teacher = read_jsonl(temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl")
            exact_output = read_jsonl(temp_path / "runs" / "sample" / "exact_phase_balanced.jsonl")
            selected_keys = {board_key(str(row["board_text"])) for row in teacher}
            exact_keys = {board_key(str(row["board"])) for row in exact_output}

            self.assertEqual(exact_keys, {key for key in selected_keys if any(board_key(str(row["board"])) == key for row in exact_rows)})
            self.assertEqual(summary["phase_exact_coverage"], {"opening": 2, "midgame": 2, "late": 0})
            self.assertEqual(summary["phase_complete_move_scores_coverage"], {"opening": 2, "midgame": 0, "late": 0})
            for row in teacher:
                self.assertIn(row["exact_status"], {"complete_move_scores", "exact_best_only", "no_exact"})
                self.assertIn(row["teacher_exact_status"], {"in_exact_best", "not_in_exact_best", "no_exact"})

    def test_prefer_exact_coverage_satisfies_complete_target_before_no_exact_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = [row for row in fixture_rows() if str(row["position_id"]).startswith("opening-")]
            labels = write_jsonl(temp_path / "labels.jsonl", rows)
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    exact_row(str(rows[0]["board_text"]), complete=True),
                    exact_row(str(rows[1]["board_text"]), complete=True),
                ],
            )

            summary = sampler.run(
                make_config(
                    temp_path,
                    labels,
                    exact_labels=exact,
                    rows=3,
                    extra_args=[
                        "--phase-targets",
                        "opening=3,midgame=0,late=0",
                        "--phase-exact-target",
                        "opening=2,midgame=0,late=0",
                        "--phase-complete-move-scores-target",
                        "opening=2,midgame=0,late=0",
                        "--prefer-exact-coverage",
                    ],
                )
            )
            output = read_jsonl(temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl")

            self.assertEqual([row["exact_status"] for row in output[:2]], ["complete_move_scores", "complete_move_scores"])
            self.assertEqual(summary["phase_exact_coverage"]["opening"], 2)
            self.assertEqual(summary["phase_complete_move_scores_coverage"]["opening"], 2)
            self.assertEqual(summary["phase_exact_target_shortage"]["opening"], 0)
            self.assertEqual(summary["phase_complete_move_scores_target_shortage"]["opening"], 0)
            self.assertTrue(summary["prefer_exact_coverage"])

    def test_prefer_exact_coverage_uses_exact_best_only_when_complete_rows_are_short(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = [row for row in fixture_rows() if str(row["position_id"]).startswith("opening-")]
            labels = write_jsonl(temp_path / "labels.jsonl", rows)
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    exact_row(str(rows[0]["board_text"]), complete=True),
                    exact_row(str(rows[1]["board_text"]), complete=False),
                ],
            )

            summary = sampler.run(
                make_config(
                    temp_path,
                    labels,
                    exact_labels=exact,
                    rows=3,
                    extra_args=[
                        "--phase-targets",
                        "opening=3,midgame=0,late=0",
                        "--phase-exact-target",
                        "opening=2,midgame=0,late=0",
                        "--phase-complete-move-scores-target",
                        "opening=1,midgame=0,late=0",
                        "--prefer-exact-coverage",
                    ],
                )
            )
            output = read_jsonl(temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl")

            self.assertEqual(
                [row["exact_status"] for row in output[:2]],
                ["complete_move_scores", "exact_best_only"],
            )
            self.assertEqual(summary["phase_exact_coverage"]["opening"], 2)
            self.assertEqual(summary["phase_complete_move_scores_coverage"]["opening"], 1)
            self.assertEqual(summary["phase_exact_target_shortage"]["opening"], 0)
            self.assertEqual(summary["phase_complete_move_scores_target_shortage"]["opening"], 0)

    def test_exact_target_shortage_fails_by_default_before_writing_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = [row for row in fixture_rows() if str(row["position_id"]).startswith("opening-")]
            labels = write_jsonl(temp_path / "labels.jsonl", rows)
            exact = write_jsonl(temp_path / "exact.jsonl", [exact_row(str(rows[0]["board_text"]), complete=True)])

            with self.assertRaises(ScriptError):
                sampler.run(
                    make_config(
                        temp_path,
                        labels,
                        exact_labels=exact,
                        rows=3,
                        extra_args=[
                            "--phase-targets",
                            "opening=3,midgame=0,late=0",
                            "--phase-exact-target",
                            "opening=2,midgame=0,late=0",
                            "--prefer-exact-coverage",
                        ],
                    )
                )
            self.assertFalse((temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl").exists())

    def test_allow_shortage_writes_exact_target_shortage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            rows = [row for row in fixture_rows() if str(row["position_id"]).startswith("opening-")]
            labels = write_jsonl(temp_path / "labels.jsonl", rows)
            exact = write_jsonl(temp_path / "exact.jsonl", [exact_row(str(rows[0]["board_text"]), complete=True)])

            summary = sampler.run(
                make_config(
                    temp_path,
                    labels,
                    exact_labels=exact,
                    rows=3,
                    extra_args=[
                        "--phase-targets",
                        "opening=3,midgame=0,late=0",
                        "--phase-exact-target",
                        "opening=2,midgame=0,late=0",
                        "--prefer-exact-coverage",
                        "--allow-shortage",
                    ],
                )
            )
            report = (temp_path / "runs" / "sample" / "report.md").read_text(encoding="utf-8")

            self.assertEqual(summary["selected_rows"], 3)
            self.assertEqual(summary["phase_exact_coverage"]["opening"], 1)
            self.assertEqual(summary["phase_exact_target_shortage"]["opening"], 1)
            self.assertTrue(summary["allow_shortage"])
            self.assertIn("Exact Coverage Targets", report)
            self.assertIn("| opening | 2 | 1 | 1 |", report)

    def test_duplicate_exact_board_prefers_complete_move_scores(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            board = board_after_moves(0)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board, position_id="opening")])
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    exact_row(board, complete=False),
                    exact_row(board, complete=True),
                ],
            )

            summary = sampler.run(
                make_config(
                    temp_path,
                    labels,
                    exact_labels=exact,
                    rows=1,
                    extra_args=["--phase-targets", "opening=1,midgame=0,late=0"],
                )
            )
            output = read_jsonl(temp_path / "runs" / "sample" / "teacher_phase_balanced.jsonl")

            self.assertEqual(output[0]["exact_status"], "complete_move_scores")
            self.assertEqual(summary["phase_complete_move_scores_coverage"]["opening"], 1)
            self.assertEqual(summary["exact_stats"]["exact_rows"], 2)
            self.assertEqual(summary["exact_stats"]["exact_unique_boards"], 1)
            self.assertEqual(summary["exact_stats"]["duplicate_exact_boards"], 1)
            self.assertEqual(summary["exact_stats"]["duplicate_exact_complete_replacements"], 1)

    def test_out_dir_must_be_under_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())
            with self.assertRaises(ScriptError):
                with mock.patch.object(sampler, "REPO_ROOT", temp_path):
                    sampler.config_from_args(
                        sampler.parse_args(
                            [
                                "--teacher-labels",
                                str(labels),
                                "--eval-config",
                                str(write_eval_config(temp_path / "base.eval")),
                                "--out-dir",
                                str(temp_path / "not-runs" / "sample"),
                                "--rows",
                                "6",
                            ]
                        )
                    )

    def test_out_dir_rejects_docs_runs_under_repo(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", fixture_rows())
            with self.assertRaises(ScriptError):
                with mock.patch.object(sampler, "REPO_ROOT", temp_path):
                    sampler.config_from_args(
                        sampler.parse_args(
                            [
                                "--teacher-labels",
                                str(labels),
                                "--eval-config",
                                str(write_eval_config(temp_path / "base.eval")),
                                "--out-dir",
                                str(temp_path / "docs" / "runs" / "sample"),
                                "--rows",
                                "6",
                            ]
                        )
                    )

    def test_help_passes(self) -> None:
        completed = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / "phase_balanced_label_sample.py"), "--help"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.assertEqual(completed.returncode, 0)
        self.assertIn("--phase-targets", completed.stdout)
        self.assertIn("--phase-exact-target", completed.stdout)
        self.assertIn("--phase-complete-move-scores-target", completed.stdout)
        self.assertIn("--prefer-exact-coverage", completed.stdout)
        self.assertIn("--allow-shortage", completed.stdout)

    def test_parse_args_requires_eval_config(self) -> None:
        with mock.patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit):
            sampler.parse_args(["--teacher-labels", "labels.jsonl", "--out-dir", "runs/x", "--rows", "6"])


if __name__ == "__main__":
    unittest.main()
