from __future__ import annotations

import json
import sys
import tempfile
import textwrap
import unittest
from dataclasses import replace
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import teacher_dataset_build as builder  # noqa: E402


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"

BOARD9_INITIAL = """\
# name: initial
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

BOARD9_SECOND = """\
# name: second
........
........
........
...WB...
...BB...
...B....
........
........
side=W
"""


def write_positions(directory: Path, text: str = BOARD9_INITIAL) -> Path:
    path = directory / "positions.txt"
    path.write_text(textwrap.dedent(text), encoding="utf-8")
    return path


def write_legal_validator(directory: Path, *, legal_moves: str = "d3 c4 d6") -> Path:
    path = directory / "fake_legal_validator.py"
    path.write_text(
        "#!/usr/bin/env python3\n"
        + textwrap.dedent(
            f"""
            import argparse
            import sys

            parser = argparse.ArgumentParser()
            parser.add_argument("--stdin", action="store_true")
            parser.add_argument("--move", required=True)
            args = parser.parse_args()

            sys.stdin.read()
            legal_moves = {legal_moves!r}.split()
            legal = args.move in legal_moves
            print("legal_move_valid=" + ("true" if legal else "false"))
            print("legal_validation_source=fake-validator")
            print("legal_moves=" + " ".join(legal_moves))
            print("error=" + ("-" if legal else "illegal move"))
            sys.exit(0 if legal else 1)
            """
        ),
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def write_exact_tool(directory: Path) -> Path:
    path = directory / "fake_exact_dump.py"
    path.write_text(
        "#!/usr/bin/env python3\n"
        + textwrap.dedent(
            """
            import argparse
            import json
            from pathlib import Path

            parser = argparse.ArgumentParser()
            parser.add_argument("--input", required=True)
            parser.add_argument("--output", required=True)
            parser.add_argument("--max-empties", required=True)
            parser.add_argument("--include-move-scores", action="store_true")
            args = parser.parse_args()

            Path(args.output).parent.mkdir(parents=True, exist_ok=True)
            row = {
                "schema": "exact_label.v1",
                "exact_score_side_to_move": 0,
                "include_move_scores": args.include_move_scores,
            }
            Path(args.output).write_text(json.dumps(row) + "\\n", encoding="utf-8")
            print("labeled=1")
            """
        ),
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def read_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def read_jsonl(path: Path) -> list[dict[str, object]]:
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def make_config(
    temp_dir: Path,
    *,
    dataset_root: Path,
    positions: Path,
    extra_args: list[str] | None = None,
    engine_args: list[str] | None = None,
) -> builder.BuildConfig:
    args = [
        "--dataset-id",
        "tiny",
        "--dataset-root",
        str(dataset_root),
        "--positions",
        str(positions),
        "--split-seed",
        "20260601",
        "--shard-size",
        "1",
    ]
    if extra_args:
        args.extend(extra_args)
    if engine_args is not None:
        args.extend(["--teacher-engine-cmd", "--", sys.executable, str(FAKE_ENGINE), *engine_args])
    parsed = builder.parse_args(args)
    return builder.config_from_args(parsed, invocation=["teacher_dataset_build.py", *args])


class TeacherDatasetBuildTests(unittest.TestCase):
    def test_normalized_position_id_is_deterministic(self) -> None:
        compact = textwrap.dedent(BOARD9_INITIAL).strip()
        padded = "\n" + compact.replace("side=B", " side=B ") + "\n"

        first = builder.position_id_for_board9(compact)
        second = builder.position_id_for_board9(padded)

        self.assertEqual(first, second)
        self.assertTrue(first.startswith("sha256:"))

    def test_duplicate_positions_are_detected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(
                temp_path,
                BOARD9_INITIAL.replace("# name: initial", "# name: first")
                + "\n"
                + BOARD9_INITIAL.replace("# name: initial", "# name: duplicate"),
            )
            config = make_config(temp_path, dataset_root=temp_path / "datasets", positions=positions)

            records, duplicates = builder._read_source_positions(config, "git")

        self.assertEqual(len(records), 1)
        self.assertEqual(len(duplicates), 1)
        self.assertEqual(duplicates[0].position_id, records[0].position_id)

    def test_split_is_deterministic_and_stable(self) -> None:
        first = "sha256:" + "0" * 64
        second = "sha256:" + "1" * 64

        split_a = builder.split_for_position(
            first, seed=20260601, ratios=(70, 15, 15)
        )
        split_b = builder.split_for_position(
            first, seed=20260601, ratios=(70, 15, 15)
        )
        split_c = builder.split_for_position(
            second, seed=20260601, ratios=(70, 15, 15)
        )

        self.assertEqual(split_a, split_b)
        self.assertIn(split_c, {"train", "validation", "holdout"})

    def test_shard_file_naming_and_manifest_counts_are_stable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            dataset_root = temp_path / "datasets"
            positions = write_positions(temp_path, BOARD9_INITIAL + "\n" + BOARD9_SECOND)
            config = make_config(temp_path, dataset_root=dataset_root, positions=positions)

            exit_code = builder.run_build(config)
            manifest = read_json(config.out_dir / "manifest.json")
            summary = read_json(config.out_dir / "qc" / "summary.json")

            self.assertEqual(exit_code, 0)
            self.assertTrue(
                (config.out_dir / "positions" / "shards" / "positions-0000.jsonl").is_file()
            )
            self.assertTrue(
                (config.out_dir / "positions" / "shards" / "positions-0001.jsonl").is_file()
            )
            self.assertEqual(manifest["counts"]["unique_positions"], 2)
            self.assertEqual(summary["total_positions"], 2)
            self.assertEqual(summary["duplicates"], 0)

    def test_resume_skips_completed_teacher_shard(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            dataset_root = temp_path / "datasets"
            positions = write_positions(temp_path)
            validator = write_legal_validator(temp_path)
            first = make_config(
                temp_path,
                dataset_root=dataset_root,
                positions=positions,
                extra_args=[
                    "--teacher-engine-name",
                    "fake",
                    "--legal-validator",
                    str(validator),
                ],
                engine_args=["--move", "d3"],
            )
            self.assertEqual(builder.run_build(first), 0)

            second = make_config(
                temp_path,
                dataset_root=dataset_root,
                positions=positions,
                extra_args=[
                    "--teacher-engine-name",
                    "fake",
                    "--legal-validator",
                    str(validator),
                    "--resume",
                    "--allow-failures",
                ],
                engine_args=["--move", "z9"],
            )
            self.assertEqual(builder.run_build(second), 0)
            labels = read_jsonl(
                second.out_dir / "labels" / "fake" / "shards" / "labels-0000.jsonl"
            )
            manifest = read_json(second.out_dir / "labels" / "fake" / "manifest.json")

        self.assertEqual(labels[0]["status"], "ok")
        self.assertEqual(manifest["resumed_label_shards"], 1)

    def test_failed_fake_engine_rows_are_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)
            validator = write_legal_validator(temp_path)
            config = make_config(
                temp_path,
                dataset_root=temp_path / "datasets",
                positions=positions,
                extra_args=[
                    "--teacher-engine-name",
                    "fake",
                    "--legal-validator",
                    str(validator),
                    "--allow-failures",
                ],
                engine_args=["--exit-code", "7", "--stderr", "boom"],
            )
            exit_code = builder.run_build(config)
            failures = read_jsonl(config.out_dir / "labels" / "fake" / "failed.jsonl")

        self.assertEqual(exit_code, 0)
        self.assertEqual(len(failures), 1)
        self.assertEqual(failures[0]["status"], "failed")
        self.assertIn("engine exited non-zero", failures[0]["error"])

    def test_illegal_fake_engine_move_is_recorded_as_failed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)
            validator = write_legal_validator(temp_path, legal_moves="d3")
            config = make_config(
                temp_path,
                dataset_root=temp_path / "datasets",
                positions=positions,
                extra_args=[
                    "--teacher-engine-name",
                    "fake",
                    "--legal-validator",
                    str(validator),
                    "--allow-failures",
                ],
                engine_args=["--move", "a1"],
            )
            exit_code = builder.run_build(config)
            failures = read_jsonl(config.out_dir / "labels" / "fake" / "failed.jsonl")
            summary = read_json(config.out_dir / "qc" / "summary.json")

        self.assertEqual(exit_code, 0)
        self.assertIs(failures[0]["legal_move_valid"], False)
        self.assertEqual(summary["illegal_teacher_moves"], 1)

    def test_exact_overlap_can_run_with_fake_exact_tool(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)
            exact_tool = write_exact_tool(temp_path)
            config = make_config(
                temp_path,
                dataset_root=temp_path / "datasets",
                positions=positions,
                extra_args=[
                    "--build-exact-overlap",
                    "--exact-label-dump",
                    str(exact_tool),
                    "--exact-max-empties",
                    "60",
                    "--include-move-scores",
                ],
            )
            exit_code = builder.run_build(config)
            exact_manifest = read_json(config.out_dir / "exact-overlap" / "manifest.json")
            exact_rows = read_jsonl(config.out_dir / "exact-overlap" / "labels.jsonl")

        self.assertEqual(exit_code, 0)
        self.assertEqual(exact_manifest["labels_generated"], 1)
        self.assertEqual(len(exact_rows), 1)
        self.assertIn("--include-move-scores", exact_manifest["command"])

    def test_dataset_card_redacts_raw_local_ntest_path(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)
            validator = write_legal_validator(temp_path)
            raw_ntest_path = "/path/to/ntest/build/ntest"
            config = make_config(
                temp_path,
                dataset_root=temp_path / "datasets",
                positions=positions,
                extra_args=[
                    "--teacher-engine-name",
                    "ntest26-local",
                    "--legal-validator",
                    str(validator),
                    "--allow-failures",
                ],
                engine_args=[],
            )
            config = replace(config, teacher_engine_cmd=[raw_ntest_path, "x"])
            builder.run_build(config)
            card = (config.out_dir / "dataset_card.md").read_text(encoding="utf-8")

        self.assertNotIn(raw_ntest_path, card)
        self.assertIn("<absolute-path:ntest>", card)

    def test_custom_relative_out_updates_dataset_card_reuse_refs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            dataset_root = temp_path / "datasets"
            positions = write_positions(temp_path)
            config = make_config(
                temp_path,
                dataset_root=dataset_root,
                positions=positions,
                extra_args=["--out", "experiments/tiny"],
            )

            self.assertEqual(builder.run_build(config), 0)
            card = (config.out_dir / "dataset_card.md").read_text(encoding="utf-8")

        self.assertIn(
            "dataset:experiments/tiny/positions/shards/positions-0000.jsonl",
            card,
        )
        self.assertIn(
            "dataset:experiments/tiny/labels/none/shards/labels-0000.jsonl",
            card,
        )
        self.assertIn(
            "dataset:experiments/tiny/exact-overlap/labels.jsonl",
            card,
        )

    def test_absolute_out_outside_dataset_root_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)

            with self.assertRaisesRegex(builder.ScriptError, "under dataset root"):
                make_config(
                    temp_path,
                    dataset_root=temp_path / "datasets",
                    positions=positions,
                    extra_args=["--out", str(temp_path / "outside")],
                )

    def test_default_output_is_under_dataset_root_not_source_data(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)
            config = make_config(temp_path, dataset_root=temp_path / "datasets", positions=positions)
            self.assertEqual(builder.run_build(config), 0)
            card = (config.out_dir / "dataset_card.md").read_text(encoding="utf-8")

        self.assertEqual(config.out_dir, (temp_path / "datasets" / "teacher" / "tiny").resolve(strict=False))
        self.assertIn("dataset:teacher/tiny/positions/shards/positions-0000.jsonl", card)
        self.assertIn("dataset:teacher/tiny/labels/none/shards/labels-0000.jsonl", card)
        self.assertIn("dataset:teacher/tiny/exact-overlap/labels.jsonl", card)
        with self.assertRaises(ValueError):
            config.out_dir.relative_to(REPO_ROOT / "data")


if __name__ == "__main__":
    unittest.main()
