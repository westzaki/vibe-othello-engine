from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import ntest_teacher_smoke as smoke  # noqa: E402


def make_config(temp_path: Path, *, jobs: int = 4) -> smoke.SmokeConfig:
    dataset_root = temp_path / "datasets"
    return smoke.SmokeConfig(
        dataset_root=dataset_root,
        dataset_id="ntest-depth12-smoke",
        ntest_cmd=[str(temp_path / "engine" / "ntest" / "ntest"), "x"],
        ntest_workdir=temp_path / "engine" / "ntest",
        depth=12,
        timeout_ms=60000,
        jobs=jobs,
        position_log_mode="failures",
        teacher_engine_lifecycle="per-request",
        sampler=temp_path / "build" / "othello_position_sampler",
        legal_validator=temp_path / "build" / "othello_validate_move",
        seed=20260601,
        bucket_spec=smoke.normalize_bucket_spec("52:2,48:1"),
        dry_run=False,
        run_dir=temp_path / "runs" / "smoke",
        invocation=[
            "ntest_teacher_smoke.py",
            "--dataset-root",
            str(dataset_root),
            "--dataset-id",
            "ntest-depth12-smoke",
            "--ntest-workdir",
            str(temp_path / "engine" / "ntest"),
            "--depth",
            "12",
            "--ntest-cmd",
            "--",
            str(temp_path / "engine" / "ntest" / "ntest"),
            "x",
        ],
    )


def write_label_rows(config: smoke.SmokeConfig, rows: list[dict[str, object]]) -> None:
    shard_dir = smoke.teacher_label_root(config) / "shards"
    shard_dir.mkdir(parents=True, exist_ok=True)
    path = shard_dir / "labels-0000.jsonl"
    path.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
        encoding="utf-8",
    )


class NTestTeacherSmokeTests(unittest.TestCase):
    def test_eta_calculation(self) -> None:
        rate = smoke.labels_per_second(120, 12.0)
        estimate = smoke.estimate_seconds_for_labels(300000, rate)

        self.assertEqual(rate, 10.0)
        self.assertEqual(estimate, 30000.0)
        self.assertEqual(smoke.format_duration(estimate), "8h 20m 0s")

    def test_recommendation_thresholds(self) -> None:
        self.assertEqual(
            smoke.recommend_action(
                requested_labels=100,
                failed_labels=0,
                timed_out_labels=0,
                illegal_teacher_moves=0,
                estimated_300k_seconds=8 * 3600,
                jobs=4,
                teacher_exit_code=0,
            ),
            smoke.ACTION_OK,
        )
        self.assertEqual(
            smoke.recommend_action(
                requested_labels=100,
                failed_labels=0,
                timed_out_labels=0,
                illegal_teacher_moves=0,
                estimated_300k_seconds=13 * 3600,
                jobs=4,
                teacher_exit_code=0,
            ),
            smoke.ACTION_REDUCE_DEPTH,
        )
        self.assertEqual(
            smoke.recommend_action(
                requested_labels=100,
                failed_labels=0,
                timed_out_labels=1,
                illegal_teacher_moves=0,
                estimated_300k_seconds=8 * 3600,
                jobs=1,
                teacher_exit_code=0,
            ),
            smoke.ACTION_INCREASE_TIMEOUT,
        )
        self.assertEqual(
            smoke.recommend_action(
                requested_labels=100,
                failed_labels=20,
                timed_out_labels=20,
                illegal_teacher_moves=0,
                estimated_300k_seconds=8 * 3600,
                jobs=4,
                teacher_exit_code=0,
            ),
            smoke.ACTION_REDUCE_JOBS,
        )
        self.assertEqual(
            smoke.recommend_action(
                requested_labels=100,
                failed_labels=1,
                timed_out_labels=0,
                illegal_teacher_moves=0,
                estimated_300k_seconds=8 * 3600,
                jobs=4,
                teacher_exit_code=0,
            ),
            smoke.ACTION_INSPECT_FAILURES,
        )
        self.assertEqual(
            smoke.recommend_action(
                requested_labels=100,
                failed_labels=0,
                timed_out_labels=0,
                illegal_teacher_moves=1,
                estimated_300k_seconds=8 * 3600,
                jobs=4,
                teacher_exit_code=0,
            ),
            smoke.ACTION_INSPECT_FAILURES,
        )

    def test_nonzero_teacher_exit_is_never_ok(self) -> None:
        self.assertEqual(
            smoke.recommend_action(
                requested_labels=100,
                failed_labels=0,
                timed_out_labels=0,
                illegal_teacher_moves=0,
                estimated_300k_seconds=8 * 3600,
                jobs=4,
                teacher_exit_code=1,
            ),
            smoke.ACTION_INSPECT_FAILURES,
        )

    def test_command_construction_and_redaction(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path)

            pool_command = smoke.build_pool_command(config)
            teacher_command = smoke.build_teacher_command(config)
            smoke.write_commands(config, pool_command=pool_command, teacher_command=teacher_command)
            commands_text = (config.run_dir / "commands.sh").read_text(encoding="utf-8")

        self.assertIn("--label-jobs", teacher_command)
        self.assertIn("4", teacher_command)
        self.assertIn("--position-log-mode", teacher_command)
        self.assertIn("failures", teacher_command)
        self.assertIn("--teacher-engine-lifecycle", teacher_command)
        self.assertIn("per-request", teacher_command)
        self.assertIn("--teacher-workdir", teacher_command)
        self.assertIn("--teacher-depth", teacher_command)
        self.assertIn("12", teacher_command)
        self.assertIn("default-300k", commands_text)
        self.assertIn("ntest-depth12-300k", commands_text)
        self.assertIn("# Change --dataset-id if your full-run dataset should use a different name.", commands_text)
        self.assertNotIn(str(config.dataset_root), commands_text)
        self.assertNotIn(str(config.ntest_workdir), commands_text)
        self.assertNotIn(str(config.ntest_cmd[0]), commands_text)
        self.assertIn("<absolute-path:datasets>", commands_text)
        self.assertIn("<absolute-path:ntest>", commands_text)

    def test_persistent_lifecycle_command_construction(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = smoke.SmokeConfig(
                **{
                    **make_config(temp_path).__dict__,
                    "teacher_engine_lifecycle": "persistent",
                }
            )

            teacher_command = smoke.build_teacher_command(config)

        self.assertIn("--teacher-engine-lifecycle", teacher_command)
        self.assertEqual(
            teacher_command[teacher_command.index("--teacher-engine-lifecycle") + 1],
            "persistent",
        )

    def test_run_smoke_uses_fake_runner_and_collects_summary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, jobs=1)
            calls: list[list[str]] = []

            def fake_runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                calls.append(command)
                if Path(command[1]).name == "teacher_dataset_build.py":
                    write_label_rows(
                        config,
                        [
                            {"status": "ok", "timed_out": False, "legal_move_valid": True},
                            {"status": "failed", "timed_out": True, "legal_move_valid": None},
                            {"status": "failed", "timed_out": False, "legal_move_valid": False},
                        ],
                    )
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

            times = iter([100.0, 112.0])
            exit_code = smoke.run_smoke(
                config,
                runner=fake_runner,
                clock=lambda: next(times),
            )
            report = (config.run_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertEqual([Path(call[1]).name for call in calls], ["balanced_position_pool.py", "teacher_dataset_build.py"])
        self.assertIn("- requested_labels: `3`", report)
        self.assertIn("- ok_labels: `1`", report)
        self.assertIn("- failed_labels: `2`", report)
        self.assertIn("- timed_out_labels: `1`", report)
        self.assertIn("- illegal_teacher_moves: `1`", report)
        self.assertIn(f"- recommended_action: `{smoke.ACTION_INSPECT_FAILURES}`", report)

    def test_run_smoke_ignores_stale_labels_when_teacher_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, jobs=1)
            source_path = smoke.position_pool_path(config)
            source_path.parent.mkdir(parents=True, exist_ok=True)
            source_path.write_text("stale source position should remain\n", encoding="utf-8")
            write_label_rows(
                config,
                [
                    {"status": "ok", "timed_out": False, "legal_move_valid": True},
                    {"status": "ok", "timed_out": False, "legal_move_valid": True},
                ],
            )
            stale_root = smoke.teacher_label_root(config)

            def fake_runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                if Path(command[1]).name == "teacher_dataset_build.py":
                    self.assertFalse(stale_root.exists())
                    return subprocess.CompletedProcess(command, 1, stdout="", stderr="failed early")
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

            times = iter([100.0, 112.0])
            exit_code = smoke.run_smoke(
                config,
                runner=fake_runner,
                clock=lambda: next(times),
            )
            report = (config.run_dir / "report.md").read_text(encoding="utf-8")

            self.assertEqual(exit_code, 1)
            self.assertTrue(source_path.is_file())
            self.assertFalse(stale_root.exists())
            self.assertIn("- requested_labels: `0`", report)
            self.assertIn("- ok_labels: `0`", report)
            self.assertIn("- teacher_exit_code: `1`", report)
            self.assertIn(f"- recommended_action: `{smoke.ACTION_INSPECT_FAILURES}`", report)


if __name__ == "__main__":
    unittest.main()
