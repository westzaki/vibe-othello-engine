from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_only_train as trainer  # noqa: E402
import teacher_score_label_workflow as workflow  # noqa: E402


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_teacher_score_engine.py"
WORKFLOW_SCRIPT = SCRIPT_DIR / "teacher_score_label_workflow.py"

BOARD = """\
........
........
........
...WB...
...BW...
........
........
........
side=B
"""


def read_jsonl(path: Path) -> list[dict[str, object]]:
    with path.open("r", encoding="utf-8") as input_file:
        return [json.loads(line) for line in input_file if line.strip()]


def write_positions(directory: Path, text: str = BOARD) -> Path:
    path = directory / "positions.txt"
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return path


def write_teacher_labels(directory: Path, *, use_board_text: bool = False) -> Path:
    path = directory / "teacher.jsonl"
    row: dict[str, object] = {
        "schema": "teacher_label.v1",
        "position_id": "fixture-1",
        "position_split": "train",
        "source_bucket": "opening-smoke",
        "move": "d3",
    }
    row["board_text" if use_board_text else "board"] = textwrap.dedent(BOARD).strip()
    path.write_text(json.dumps(row, sort_keys=True) + "\n", encoding="utf-8")
    return path


def make_config(
    temp_path: Path,
    *,
    input_path: Path | None = None,
    input_flag: str = "--teacher-labels",
    mode: str = "complete",
    extra_args: list[str] | None = None,
) -> workflow.WorkflowConfig:
    source = input_path or write_teacher_labels(temp_path)
    args = [
        input_flag,
        str(source),
        "--out-dir",
        str(Path("runs") / "teacher-score-test"),
        "--teacher-score-engine-name",
        "fake",
        "--teacher-score-depth",
        "18",
        *(extra_args or []),
        "--teacher-score-command",
        "--",
        sys.executable,
        str(FAKE_ENGINE),
        "--mode",
        mode,
    ]
    parsed = workflow.parse_args(args)
    config = workflow.config_from_args(
        parsed,
        invocation=["teacher_score_label_workflow.py", *args],
    )
    return workflow.WorkflowConfig(
        teacher_labels=config.teacher_labels,
        positions=config.positions,
        out_dir=temp_path / "out",
        teacher_engine=config.teacher_engine,
        teacher_depth=config.teacher_depth,
        teacher_command=config.teacher_command,
        max_rows=config.max_rows,
        split=config.split,
        allow_partial=config.allow_partial,
        seed=config.seed,
        invocation=config.invocation,
    )


class TeacherScoreLabelWorkflowTests(unittest.TestCase):
    def test_complete_root_scores_create_teacher_score_labels(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path)

            exit_code = workflow.run_workflow(config)
            rows = read_jsonl(config.labels_path)
            summary = json.loads(config.summary_path.read_text(encoding="utf-8"))
            report = config.report_path.read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["schema"], "teacher_score_label.v1")
        self.assertEqual(row["position_id"], "fixture-1")
        self.assertEqual(row["position_split"], "train")
        self.assertEqual(row["source_bucket"], "opening-smoke")
        self.assertEqual(row["score_kind"], "teacher_search_score")
        self.assertEqual(row["score_perspective"], "side_to_move")
        self.assertEqual(row["teacher_engine"], "fake")
        self.assertEqual(row["teacher_depth"], 18)
        self.assertIs(row["not_exact"], True)
        self.assertEqual(row["score_status"], "complete")
        self.assertTrue(row["move_scores"])
        self.assertNotIn("exact_score_side_to_move", json.dumps(row))
        self.assertEqual(summary["complete_rows"], 1)
        self.assertEqual(summary["partial_rows"], 0)
        self.assertEqual(summary["failed_rows"], 0)
        self.assertIs(summary["no_strength_claim"], True)
        self.assertIn("teacher_score_side_to_move", report)

    def test_partial_scores_are_skipped_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, mode="partial")

            workflow.run_workflow(config)
            rows = read_jsonl(config.labels_path)
            summary = json.loads(config.summary_path.read_text(encoding="utf-8"))

        self.assertEqual(rows, [])
        self.assertEqual(summary["partial_rows"], 1)
        self.assertEqual(summary["skipped_rows"], 1)
        self.assertEqual(summary["failure_reasons"]["partial_row_skipped"], 1)

    def test_allow_partial_writes_partial_row(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, mode="partial", extra_args=["--allow-partial"])

            workflow.run_workflow(config)
            rows = read_jsonl(config.labels_path)
            summary = json.loads(config.summary_path.read_text(encoding="utf-8"))

        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["score_status"], "partial")
        self.assertEqual(summary["partial_rows"], 1)
        self.assertEqual(summary["output_rows"], 1)

    def test_illegal_score_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, mode="illegal")

            workflow.run_workflow(config)
            rows = read_jsonl(config.labels_path)
            summary = json.loads(config.summary_path.read_text(encoding="utf-8"))

        self.assertEqual(len(rows), 1)
        moves = {item["move"] for item in rows[0]["move_scores"]}  # type: ignore[index]
        self.assertNotIn("a1", moves)
        self.assertEqual(summary["illegal_move_score_count"], 1)

    def test_board_text_input_and_dataset_reference_are_supported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            dataset_root = temp_path / "dataset"
            dataset_root.mkdir()
            labels = write_teacher_labels(dataset_root, use_board_text=True)
            args = [
                "--dataset-root",
                str(dataset_root),
                "--teacher-labels",
                f"dataset:{labels.name}",
                "--dataset-output",
                "teacher-score/smoke",
                "--teacher-score-command",
                "--",
                sys.executable,
                str(FAKE_ENGINE),
            ]
            parsed = workflow.parse_args(args)
            config = workflow.config_from_args(parsed, invocation=args)

            workflow.run_workflow(config)
            rows = read_jsonl(config.labels_path)

        self.assertEqual(
            config.labels_path.resolve(strict=False),
            (dataset_root / "teacher-score" / "smoke" / "labels.jsonl").resolve(strict=False),
        )
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["position_id"], "fixture-1")

    def test_positions_input_is_supported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)
            config = make_config(temp_path, input_path=positions, input_flag="--positions")

            workflow.run_workflow(config)
            rows = read_jsonl(config.labels_path)

        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["position_id"], "position-000000")
        self.assertEqual(rows[0]["source_bucket"], "board9")

    def test_duplicate_board_uses_first_row_and_skips_later_duplicate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            path = temp_path / "teacher.jsonl"
            first = {
                "schema": "teacher_label.v1",
                "board": textwrap.dedent(BOARD).strip(),
                "position_id": "first",
                "position_split": "train",
            }
            second = dict(first)
            second["position_id"] = "second"
            second["position_split"] = "validation"
            path.write_text(json.dumps(first) + "\n" + json.dumps(second) + "\n", encoding="utf-8")
            config = make_config(temp_path, input_path=path)

            workflow.run_workflow(config)
            rows = read_jsonl(config.labels_path)
            summary = json.loads(config.summary_path.read_text(encoding="utf-8"))

        self.assertEqual([row["position_id"] for row in rows], ["first"])
        self.assertEqual(summary["duplicate_board_count"], 1)
        self.assertEqual(summary["skipped_rows"], 1)

    def test_pattern_only_qc_reads_teacher_score_side_to_move(self) -> None:
        row = {
            "schema": "teacher_score_label.v1",
            "board": textwrap.dedent(BOARD).strip(),
            "move_scores": [{"move": "d3", "teacher_score_side_to_move": 7}],
        }

        self.assertEqual(trainer.teacher_score_move_scores(row), {"d3": 7})

    def test_script_smoke_writes_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_teacher_labels(temp_path)
            out_dir = Path("runs") / "teacher-score-smoke"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(WORKFLOW_SCRIPT),
                    "--teacher-labels",
                    str(labels),
                    "--out-dir",
                    str(out_dir),
                    "--teacher-score-command",
                    "--",
                    sys.executable,
                    str(FAKE_ENGINE),
                    "--mode",
                    "move-scores",
                ],
                cwd=temp_path,
                env={**os.environ, "PYTHONPATH": str(SCRIPT_DIR)},
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertTrue((temp_path / out_dir / "labels.jsonl").is_file())
            self.assertTrue((temp_path / out_dir / "summary.json").is_file())
            self.assertTrue((temp_path / out_dir / "report.md").is_file())
