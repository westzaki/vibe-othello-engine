from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pattern_dataset_qc as qc  # noqa: E402


class PatternDatasetQcTests(unittest.TestCase):
    def test_write_subset_uses_first_nonblank_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            source_a = temp_path / "a.jsonl"
            source_b = temp_path / "b.jsonl"
            output = temp_path / "runs" / "subset.jsonl"
            source_a.write_text('{"id": 1}\n\n{"id": 2}\n', encoding="utf-8")
            source_b.write_text('{"id": 3}\n{"id": 4}\n', encoding="utf-8")

            selected = qc.write_subset([source_a, source_b], output=output, limit=3)

            self.assertEqual(selected, 3)
            self.assertEqual(
                output.read_text(encoding="utf-8").splitlines(),
                ['{"id": 1}', '{"id": 2}', '{"id": 3}'],
            )

    def test_summarize_run_extracts_qc_summary_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            summary_path = Path(temp) / "summary.json"
            summary_path.write_text(
                json.dumps(
                    {
                        "rows": {
                            "teacher_rows": 2,
                            "accepted_teacher_rows": 2,
                            "training_examples": 1,
                            "qc_summary": {
                                "root_phase_counts": {"overall": {"opening": 1, "midgame": 1}},
                                "child_phase_counts": {"overall": {"midgame": 3}},
                                "root_to_child_phase_counts": {"opening": {"midgame": 3}},
                                "legal_move_count_distribution": {"3": 1, "4": 1},
                                "complete_exact_move_scores": {
                                    "overall": {
                                        "rows": 2,
                                        "complete_rows": 1,
                                        "partial_rows": 0,
                                        "missing_rows": 1,
                                        "present_scores": 3,
                                        "legal_moves": 7,
                                    },
                                    "by_split": {"train": {"rows": 2, "complete_rows": 1}},
                                    "by_phase": {},
                                    "by_source_bucket": {},
                                },
                                "complete_teacher_move_scores": {
                                    "overall": {
                                        "rows": 2,
                                        "complete_rows": 0,
                                        "partial_rows": 0,
                                        "missing_rows": 2,
                                        "present_scores": 0,
                                        "legal_moves": 7,
                                    },
                                    "by_split": {},
                                    "by_phase": {},
                                    "by_source_bucket": {},
                                },
                                "source_bucket_counts": {"__missing__": 2},
                                "training_bucket_counts": {"__missing__": 1},
                                "duplicate_group_split_leakage_check": {
                                    "duplicate_groups": 1,
                                    "leaking_groups": 1,
                                    "leaking_rows": 2,
                                },
                            },
                            "dataset_diagnostics": {
                                "by_split": {
                                    "train": {
                                        "exact_unavailable": 1,
                                        "teacher_exact_disagreement": 1,
                                        "exact_best_top_group_size_distribution": {"1": 1},
                                    }
                                },
                                "by_phase": {
                                    "opening": {
                                        "exact_best_top_group_size_distribution": {"1": 1, "2": 3}
                                    }
                                },
                                "by_source_bucket": {},
                            },
                        }
                    }
                ),
                encoding="utf-8",
            )

            row = qc.summarize_run("fixture", summary_path)

            self.assertEqual(row["run"], "fixture")
            self.assertEqual(row["exact_unavailable"], 1)
            self.assertEqual(row["teacher_exact_disagreement"], 1)
            self.assertEqual(row["exact_complete_rows"], 1)
            self.assertEqual(row["teacher_complete_rows"], 0)
            self.assertEqual(row["duplicate_groups"], 1)
            self.assertEqual(row["leaking_groups"], 1)
            self.assertEqual(row["exact_best_top_group_size"], '{"1": 1, "2": 3}')

    def test_dry_run_writes_commands_under_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            dataset_root = temp_path / "datasets"
            labels_dir = dataset_root / "teacher" / "fixture" / "labels" / "ntest12-local" / "shards"
            exact_dir = dataset_root / "teacher" / "fixture-exact-overlap-v0" / "exact-overlap"
            labels_dir.mkdir(parents=True)
            exact_dir.mkdir(parents=True)
            (labels_dir / "labels-0000.jsonl").write_text('{"row": 1}\n', encoding="utf-8")
            (exact_dir / "labels.jsonl").write_text('{"row": 1}\n', encoding="utf-8")
            eval_config = temp_path / "base.eval"
            analyze = temp_path / "analyze"
            eval_config.write_text("schema_version=eval.v1\n", encoding="utf-8")
            analyze.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            out_root = temp_path / "runs" / "pattern-training"

            with mock.patch("pattern_dataset_qc.REPO_ROOT", temp_path):
                status = qc.main(
                    [
                        "--dataset-root",
                        str(dataset_root),
                        "--dataset-name",
                        "fixture",
                        "--eval-config",
                        str(eval_config),
                        "--analyze-position",
                        str(analyze),
                        "--out-root",
                        str(out_root),
                        "--sizes",
                        "1",
                        "--dry-run",
                    ]
                )

            self.assertEqual(status, 0)
            commands = out_root / "fixture" / "qc" / "commands.md"
            self.assertTrue(commands.exists())
            self.assertIn("--diagnose-dataset", commands.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
