from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from concurrent.futures import Future
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import external_teacher_label_workflow as workflow  # noqa: E402
from common import ScriptError  # noqa: E402


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"
FAKE_PERSISTENT_NBOARD = SCRIPT_DIR / "external_engines" / "fake_persistent_nboard.py"
WORKFLOW_SCRIPT = SCRIPT_DIR / "external_teacher_label_workflow.py"

BOARD9_POSITION = """\
# name: initial
# phase: opening
# tags: smoke
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


def write_positions(directory: Path, text: str = BOARD9_POSITION) -> Path:
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


def write_stdin_router_engine(directory: Path) -> Path:
    path = directory / "stdin_router_engine.py"
    path.write_text(
        "#!/usr/bin/env python3\n"
        + textwrap.dedent(
            """
            import sys
            import time

            text = sys.stdin.read()
            if "SLOW" in text:
                time.sleep(0.2)
                print("d3")
            elif "BADTOKEN" in text:
                print("z9")
            elif "EXIT7" in text:
                print("boom", file=sys.stderr)
                sys.exit(7)
            else:
                print("d3")
            """
        ),
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def read_rows(path: Path) -> list[dict[str, object]]:
    with path.open("r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def make_config(
    temp_dir: Path,
    *,
    engine_args: list[str] | None = None,
    engine_command: list[str] | None = None,
    extra_args: list[str] | None = None,
    positions_text: str = BOARD9_POSITION,
) -> workflow.WorkflowConfig:
    positions = write_positions(temp_dir, positions_text)
    legal_validator = write_legal_validator(temp_dir)
    args = [
        "--positions",
        str(positions),
        "--out",
        str(temp_dir / "workflow"),
        "--engine-name",
        "fake",
        "--legal-validator",
        str(legal_validator),
    ]
    if extra_args:
        args.extend(extra_args)
    command = engine_command or [sys.executable, str(FAKE_ENGINE), *(engine_args or [])]
    args.extend(["--engine-cmd", "--", *command])
    parsed = workflow.parse_args(args)
    return workflow.config_from_args(
        parsed,
        invocation=["external_teacher_label_workflow.py", *args],
    )


class ExternalTeacherLabelWorkflowTests(unittest.TestCase):
    def test_fake_engine_success_creates_teacher_label_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, engine_args=["--move", "D3"])

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)
            report = config.report_path.read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["schema"], "teacher_label.v1")
        self.assertEqual(row["position_name"], "initial")
        self.assertEqual(row["status"], "ok")
        self.assertEqual(row["move"], "d3")
        self.assertIsNone(row["engine_move"])
        self.assertIs(row["move_token_valid"], True)
        self.assertIs(row["legal_move_valid"], True)
        self.assertEqual(row["legal_validation_source"], str(config.legal_validator))
        self.assertEqual(row["legal_moves"], ["d3", "c4", "d6"])
        self.assertIn("board_text", row)
        self.assertIn("external_input_text", row)
        self.assertIn("No strength claim", report)
        self.assertIn("fake-engine tests cover CI", report)

    def test_jobs_keeps_labels_in_position_index_order_with_slow_engine(self) -> None:
        positions_text = """\
        # name: slow
        SLOW

        # name: fast
        FAST

        # name: normal
        NORMAL
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            engine = write_stdin_router_engine(temp_path)
            config = make_config(
                temp_path,
                engine_command=[sys.executable, str(engine)],
                extra_args=["--input-format", "raw", "--jobs", "3"],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual([row["position_index"] for row in rows], [0, 1, 2])
        self.assertEqual([row["position_name"] for row in rows], ["slow", "fast", "normal"])
        self.assertTrue(all(row["status"] == "ok" for row in rows))

    def test_jobs_uses_bounded_pending_future_window(self) -> None:
        positions_text = "\n\n".join(f"# name: p{index}\nP{index}" for index in range(9))
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_command=["unused"],
                extra_args=["--input-format", "raw", "--jobs", "2"],
                positions_text=positions_text,
            )
            positions = workflow.parse_positions(config.positions_path, config.input_format)
            metadata = workflow.Metadata(generated_at="test", git_sha="test")
            max_pending = config.jobs * 2
            outstanding: set[Future[dict[str, object]]] = set()
            observed_pending_sizes: list[int] = []

            class TrackingExecutor:
                def __init__(self, *, max_workers: int) -> None:
                    self.max_workers = max_workers

                def __enter__(self) -> TrackingExecutor:
                    return self

                def __exit__(self, *_: object) -> bool:
                    return False

                def submit(
                    self,
                    fn: object,
                    config_arg: object,
                    metadata_arg: object,
                    position: object,
                ) -> Future[dict[str, object]]:
                    future: Future[dict[str, object]] = Future()
                    future.set_result(
                        {
                            "position_index": getattr(position, "position_index"),
                            "position_name": getattr(position, "name"),
                        }
                    )
                    outstanding.add(future)
                    if len(outstanding) > max_pending:
                        raise AssertionError("submitted more than the pending-future window")
                    return future

            def fake_wait(
                futures: object,
                *,
                return_when: object,
            ) -> tuple[set[Future[dict[str, object]]], set[Future[dict[str, object]]]]:
                pending = set(futures)
                observed_pending_sizes.append(len(pending))
                done = {next(iter(pending))}
                outstanding.difference_update(done)
                return done, pending - done

            original_executor = workflow.ThreadPoolExecutor
            original_wait = workflow.wait
            try:
                workflow.ThreadPoolExecutor = TrackingExecutor  # type: ignore[assignment]
                workflow.wait = fake_wait  # type: ignore[assignment]
                rows = workflow.run_positions_concurrently(config, metadata, positions)
            finally:
                workflow.ThreadPoolExecutor = original_executor  # type: ignore[assignment]
                workflow.wait = original_wait  # type: ignore[assignment]

        self.assertEqual([row["position_index"] for row in rows], list(range(9)))
        self.assertGreater(len(positions), max_pending)
        self.assertTrue(observed_pending_sizes)
        self.assertLessEqual(max(observed_pending_sizes), max_pending)

    def test_position_log_mode_failures_writes_only_failed_logs(self) -> None:
        positions_text = """\
        # name: ok
        OK

        # name: bad-token
        BADTOKEN
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            engine = write_stdin_router_engine(temp_path)
            config = make_config(
                temp_path,
                engine_command=[sys.executable, str(engine)],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--position-log-mode",
                    "failures",
                    "--allow-failures",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)
            report = config.report_path.read_text(encoding="utf-8")
            failed_log_exists = Path(str(rows[1]["log_path"])).is_file()
            log_names = sorted(path.name for path in config.logs_dir.glob("*.log"))

        self.assertEqual(exit_code, 0)
        self.assertEqual(rows[0]["status"], "ok")
        self.assertIsNone(rows[0]["log_path"])
        self.assertNotIn("engine_stdout", rows[0])
        self.assertNotIn("engine_stderr", rows[0])
        self.assertEqual(rows[1]["status"], "failed")
        self.assertIsNotNone(rows[1]["log_path"])
        self.assertNotIn("engine_stdout", rows[1])
        self.assertNotIn("engine_stderr", rows[1])
        self.assertTrue(failed_log_exists)
        self.assertEqual(log_names, ["position-000001.log"])
        self.assertIn("- position_log_mode: `failures`", report)

    def test_position_log_mode_none_writes_diagnostics_without_logs(self) -> None:
        positions_text = """\
        # name: ok
        OK

        # name: bad-token
        BADTOKEN
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            engine = write_stdin_router_engine(temp_path)
            config = make_config(
                temp_path,
                engine_command=[sys.executable, str(engine)],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--position-log-mode",
                    "none",
                    "--allow-failures",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)
            report = config.report_path.read_text(encoding="utf-8")
            logs_dir_exists = config.logs_dir.exists()

        self.assertEqual(exit_code, 0)
        self.assertEqual(rows[0]["status"], "ok")
        self.assertIsNone(rows[0]["log_path"])
        self.assertNotIn("engine_stdout", rows[0])
        self.assertNotIn("engine_stderr", rows[0])
        self.assertEqual(rows[1]["status"], "failed")
        self.assertIsNone(rows[1]["log_path"])
        self.assertFalse(logs_dir_exists)
        self.assertEqual(str(rows[1]["engine_stdout"]).strip(), "z9")
        self.assertIn("invalid move format", str(rows[1]["error"]))
        self.assertIn("- position_log_mode: `none`", report)

    def test_summary_counts_mixed_ok_fail_rows(self) -> None:
        positions_text = """\
        # name: ok
        OK

        # name: bad-token
        BADTOKEN

        # name: exited
        EXIT7

        # name: timed-out
        SLOW
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            engine = write_stdin_router_engine(temp_path)
            config = make_config(
                temp_path,
                engine_command=[sys.executable, str(engine)],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--timeout-ms",
                    "100",
                    "--position-log-mode",
                    "none",
                    "--allow-failures",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)
            report = config.report_path.read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertEqual([row["status"] for row in rows], ["ok", "failed", "failed", "failed"])
        self.assertEqual([row["timed_out"] for row in rows], [False, False, False, True])
        self.assertIn("- total_input_positions: `4`", report)
        self.assertIn("- requested: `4`", report)
        self.assertIn("- ok: `1`", report)
        self.assertIn("- failed: `3`", report)
        self.assertIn("- timed_out: `1`", report)
        self.assertIn("- invalid_move_token: `1`", report)
        self.assertIn("- illegal_move: `0`", report)
        self.assertIn("- legal_validation_failed: `0`", report)

    def test_invalid_move_token_is_recorded_as_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, engine_args=["--move", "z9"])

            exit_code = workflow.run_workflow(config)
            row = read_rows(config.labels_path)[0]

        self.assertEqual(exit_code, 1)
        self.assertEqual(row["status"], "failed")
        self.assertIsNone(row["move"])
        self.assertIs(row["move_token_valid"], False)
        self.assertIsNone(row["legal_move_valid"])
        self.assertIn("invalid move format", row["error"])

    def test_illegal_engine_move_is_recorded_as_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, engine_args=["--move", "a1"])

            exit_code = workflow.run_workflow(config)
            row = read_rows(config.labels_path)[0]
            log_text = Path(row["log_path"]).read_text(encoding="utf-8")

        self.assertEqual(exit_code, 1)
        self.assertEqual(row["status"], "failed")
        self.assertEqual(row["move"], "a1")
        self.assertIs(row["move_token_valid"], True)
        self.assertIs(row["legal_move_valid"], False)
        self.assertEqual(row["legal_validation_error"], "illegal move")
        self.assertIn("legal_move_valid: False", log_text)

    def test_timeout_is_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_args=["--move", "d3", "--sleep-ms", "200"],
                extra_args=["--timeout-ms", "20"],
            )

            exit_code = workflow.run_workflow(config)
            row = read_rows(config.labels_path)[0]

        self.assertEqual(exit_code, 1)
        self.assertEqual(row["status"], "failed")
        self.assertTrue(row["timed_out"])
        self.assertEqual(row["exit_code"], -1)
        self.assertEqual(row["error"], "engine timed out")

    def test_non_zero_engine_exit_is_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, engine_args=["--exit-code", "7", "--stderr", "boom"])

            exit_code = workflow.run_workflow(config)
            row = read_rows(config.labels_path)[0]
            log_text = Path(row["log_path"]).read_text(encoding="utf-8")

        self.assertEqual(exit_code, 1)
        self.assertEqual(row["status"], "failed")
        self.assertEqual(row["exit_code"], 7)
        self.assertEqual(row["error"], "engine exited non-zero")
        self.assertIn("boom", log_text)

    def test_dry_run_writes_planned_report_labels_and_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_args=["--move", "d3"],
                extra_args=["--dry-run"],
            )

            exit_code = workflow.run_workflow(config)
            row = read_rows(config.labels_path)[0]
            report = config.report_path.read_text(encoding="utf-8")
            log_text = Path(row["log_path"]).read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertEqual(row["status"], "skipped")
        self.assertEqual(row["error"], "dry-run")
        self.assertEqual(row["legal_validation_error"], "dry-run")
        self.assertIn("Status: dry run", report)
        self.assertIn("command:", log_text)
        self.assertIn("status: skipped", log_text)

    def test_duplicate_position_name_is_rejected(self) -> None:
        duplicate_text = BOARD9_POSITION + "\n" + BOARD9_POSITION
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_args=["--move", "d3"],
                positions_text=duplicate_text,
            )

            with self.assertRaises(ScriptError) as context:
                workflow.run_workflow(config)

        self.assertIn("duplicate # name", str(context.exception))

    def test_malformed_board9_input_is_rejected(self) -> None:
        malformed = BOARD9_POSITION.replace("........", "........X", 1)
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_args=["--move", "d3"],
                positions_text=malformed,
            )

            with self.assertRaises(ScriptError) as context:
                workflow.run_workflow(config)

        self.assertIn("invalid board row", str(context.exception))

    def test_ntest_nboard_uses_converted_board9_input(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_args=["--move", "=== D3//0.00"],
                extra_args=["--adapter", "ntest", "--protocol", "nboard", "--depth", "6"],
            )

            exit_code = workflow.run_workflow(config)
            row = read_rows(config.labels_path)[0]

        self.assertEqual(exit_code, 0)
        self.assertEqual(row["status"], "ok")
        self.assertEqual(row["adapter"], "ntest")
        self.assertEqual(row["protocol"], "nboard")
        self.assertEqual(row["depth"], 6)
        self.assertEqual(row["engine_move"], "d3")
        self.assertEqual(row["move"], "d6")
        self.assertIs(row["legal_move_valid"], True)
        self.assertEqual(row["external_input_format"], "nboard-game")
        self.assertIn("BO[8", row["external_input_text"])

    def test_ntest_nboard_defaults_to_depth_26(self) -> None:
        parsed = workflow.parse_args(
            [
                "--positions",
                "positions.txt",
                "--adapter",
                "ntest",
                "--protocol",
                "nboard",
                "--engine-cmd",
                "--",
                "ntest",
                "x",
            ]
        )

        self.assertEqual(parsed.depth, 26)

    def test_engine_lifecycle_defaults_to_per_request(self) -> None:
        parsed = workflow.parse_args(
            [
                "--positions",
                "positions.txt",
                "--engine-cmd",
                "--",
                "fake-engine",
            ]
        )

        self.assertEqual(parsed.engine_lifecycle, "per-request")

    def test_persistent_rejects_unsupported_adapter_protocol(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                extra_args=["--engine-lifecycle", "persistent"],
            )

            with self.assertRaises(ScriptError) as context:
                workflow.run_workflow(config)

        self.assertIn("persistent requires --adapter ntest --protocol nboard", str(context.exception))

    def test_persistent_ntest_worker_serves_multiple_positions_from_one_process(self) -> None:
        positions_text = """\
        # name: p0
        (;GM[Othello]BO[8 P0];)

        # name: p1
        (;GM[Othello]BO[8 P1];)

        # name: p2
        (;GM[Othello]BO[8 P2];)
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            counter = temp_path / "counter.tsv"
            config = make_config(
                temp_path,
                engine_command=[
                    sys.executable,
                    str(FAKE_PERSISTENT_NBOARD),
                    "--counter-file",
                    str(counter),
                ],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--adapter",
                    "ntest",
                    "--protocol",
                    "nboard",
                    "--depth",
                    "10",
                    "--engine-lifecycle",
                    "persistent",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)
            events = counter.read_text(encoding="utf-8").splitlines()
            report = config.report_path.read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertEqual([row["position_index"] for row in rows], [0, 1, 2])
        self.assertTrue(all(row["status"] == "ok" for row in rows))
        self.assertEqual(sum(1 for line in events if line.endswith("\tstart")), 1)
        self.assertEqual(sum(1 for line in events if line.endswith("\trequest")), 3)
        self.assertIn("- engine_lifecycle: `persistent`", report)

    def test_persistent_jobs_keep_deterministic_order_with_multiple_workers(self) -> None:
        positions_text = "\n\n".join(
            f"# name: p{index}\n(;GM[Othello]BO[8 P{index}];)" for index in range(8)
        )
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            counter = temp_path / "counter.tsv"
            config = make_config(
                temp_path,
                engine_command=[
                    sys.executable,
                    str(FAKE_PERSISTENT_NBOARD),
                    "--counter-file",
                    str(counter),
                    "--sleep-ms",
                    "10",
                ],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--adapter",
                    "ntest",
                    "--protocol",
                    "nboard",
                    "--depth",
                    "10",
                    "--jobs",
                    "2",
                    "--engine-lifecycle",
                    "persistent",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)
            events = counter.read_text(encoding="utf-8").splitlines()

        self.assertEqual(exit_code, 0)
        self.assertEqual([row["position_index"] for row in rows], list(range(8)))
        self.assertTrue(all(row["status"] == "ok" for row in rows))
        self.assertLessEqual(sum(1 for line in events if line.endswith("\tstart")), 2)
        self.assertEqual(sum(1 for line in events if line.endswith("\trequest")), 8)

    def test_persistent_invalid_move_token_is_recorded(self) -> None:
        positions_text = """\
        # name: bad
        (;GM[Othello]BO[8 BADTOKEN];)
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_command=[sys.executable, str(FAKE_PERSISTENT_NBOARD)],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--adapter",
                    "ntest",
                    "--protocol",
                    "nboard",
                    "--depth",
                    "10",
                    "--engine-lifecycle",
                    "persistent",
                    "--allow-failures",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            row = read_rows(config.labels_path)[0]

        self.assertEqual(exit_code, 0)
        self.assertEqual(row["status"], "failed")
        self.assertIsNone(row["move"])
        self.assertIs(row["move_token_valid"], False)

    def test_persistent_restarts_after_engine_exit_mid_run(self) -> None:
        positions_text = """\
        # name: ok0
        (;GM[Othello]BO[8 OK0];)

        # name: exits
        (;GM[Othello]BO[8 EXIT];)

        # name: ok1
        (;GM[Othello]BO[8 OK1];)
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            counter = temp_path / "counter.tsv"
            config = make_config(
                temp_path,
                engine_command=[
                    sys.executable,
                    str(FAKE_PERSISTENT_NBOARD),
                    "--counter-file",
                    str(counter),
                ],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--adapter",
                    "ntest",
                    "--protocol",
                    "nboard",
                    "--depth",
                    "10",
                    "--engine-lifecycle",
                    "persistent",
                    "--allow-failures",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)
            events = counter.read_text(encoding="utf-8").splitlines()

        self.assertEqual(exit_code, 0)
        self.assertEqual([row["status"] for row in rows], ["ok", "failed", "ok"])
        self.assertEqual(rows[1]["exit_code"], 7)
        self.assertGreaterEqual(sum(1 for line in events if line.endswith("\tstart")), 2)

    def test_persistent_timeout_kills_worker_and_later_rows_continue(self) -> None:
        positions_text = """\
        # name: hang
        (;GM[Othello]BO[8 HANG];)

        # name: ok
        (;GM[Othello]BO[8 OK];)
        """
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                engine_command=[sys.executable, str(FAKE_PERSISTENT_NBOARD)],
                extra_args=[
                    "--input-format",
                    "raw",
                    "--adapter",
                    "ntest",
                    "--protocol",
                    "nboard",
                    "--depth",
                    "10",
                    "--timeout-ms",
                    "50",
                    "--engine-lifecycle",
                    "persistent",
                    "--allow-failures",
                ],
                positions_text=positions_text,
            )

            exit_code = workflow.run_workflow(config)
            rows = read_rows(config.labels_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual([row["status"] for row in rows], ["failed", "ok"])
        self.assertEqual([row["timed_out"] for row in rows], [True, False])

    def test_config_from_args_resolves_dataset_positions_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "datasets"
            root.mkdir()
            positions = root / "positions" / "positions.txt"
            positions.parent.mkdir()
            positions.write_text(BOARD9_POSITION, encoding="utf-8")
            parsed = workflow.parse_args(
                [
                    "--positions",
                    "dataset:positions/positions.txt",
                    "--dataset-root",
                    str(root),
                    "--engine-cmd",
                    "--",
                    "fake-engine",
                ]
            )

            config = workflow.config_from_args(parsed)

        self.assertEqual(config.positions_path, positions.resolve(strict=False))

    def test_command_boundary_passes_help_to_engine(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            positions = write_positions(temp_path)
            legal_validator = write_legal_validator(temp_path)
            out_dir = temp_path / "workflow"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(WORKFLOW_SCRIPT),
                    "--positions",
                    str(positions),
                    "--out",
                    str(out_dir),
                    "--allow-failures",
                    "--engine-name",
                    "fake",
                    "--legal-validator",
                    str(legal_validator),
                    "--engine-cmd",
                    "--",
                    sys.executable,
                    str(FAKE_ENGINE),
                    "--help",
                ],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            row = read_rows(out_dir / "labels.jsonl")[0]
            log_text = Path(row["log_path"]).read_text(encoding="utf-8")

        self.assertEqual(completed.returncode, 0)
        self.assertEqual(row["status"], "failed")
        self.assertIs(row["move_token_valid"], False)
        self.assertIn("usage: fake_engine.py", log_text)
        self.assertNotIn("Generate external-engine teacher labels", log_text)


if __name__ == "__main__":
    unittest.main()
