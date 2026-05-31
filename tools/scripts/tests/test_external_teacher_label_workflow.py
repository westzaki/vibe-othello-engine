from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import external_teacher_label_workflow as workflow  # noqa: E402
from common import ScriptError  # noqa: E402


FAKE_ENGINE = SCRIPT_DIR / "external_engines" / "fake_engine.py"
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


def read_rows(path: Path) -> list[dict[str, object]]:
    with path.open("r", encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def make_config(
    temp_dir: Path,
    *,
    engine_args: list[str],
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
    args.extend(["--engine-cmd", "--", sys.executable, str(FAKE_ENGINE), *engine_args])
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
