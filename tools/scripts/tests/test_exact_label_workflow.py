from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import exact_label_workflow as workflow  # noqa: E402
from common import ScriptError  # noqa: E402


def write_tool(directory: Path, name: str, body: str) -> Path:
    path = directory / name
    path.write_text("#!/usr/bin/env python3\n" + textwrap.dedent(body), encoding="utf-8")
    path.chmod(0o755)
    return path


def write_success_tools(directory: Path) -> tuple[Path, Path, Path]:
    sampler = write_tool(
        directory,
        "fake_sampler.py",
        """
        import argparse
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--output", required=True)
        parser.add_argument("--count", required=True)
        parser.add_argument("--target-empties", required=True)
        parser.add_argument("--seed", required=True)
        args = parser.parse_args()

        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text(
            "........\\n........\\n........\\n...BW...\\n...WB...\\n........\\n........\\n........\\nside=B\\n",
            encoding="utf-8",
        )
        print(f"fake sampler sampled={args.count} output={args.output}")
        """,
    )
    dump = write_tool(
        directory,
        "fake_dump.py",
        """
        import argparse
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--input", required=True)
        parser.add_argument("--output", required=True)
        parser.add_argument("--max-empties", required=True)
        args = parser.parse_args()

        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text("{}\\n", encoding="utf-8")
        print(f"fake dump labeled=4 output={args.output}")
        """,
    )
    analyzer = write_tool(
        directory,
        "fake_analyzer.py",
        """
        import argparse
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--labels", required=True)
        parser.add_argument("--output", required=True)
        parser.add_argument("--eval-preset")
        parser.add_argument("--eval-config")
        args = parser.parse_args()

        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text("# fake eval-vs-exact\\n", encoding="utf-8")
        print(f"fake analyzer records_read=4 analyzed=4 output={args.output}")
        """,
    )
    return sampler, dump, analyzer


def make_config(
    temp_dir: Path,
    *,
    extra_args: list[str] | None = None,
    tools: tuple[Path, Path, Path] | None = None,
) -> workflow.WorkflowConfig:
    args = [
        "--out",
        str(temp_dir / "workflow"),
        "--count",
        "4",
        "--target-empties",
        "8,10",
        "--seed",
        "20260531",
        "--max-empties",
        "14",
    ]
    if tools is not None:
        sampler, dump, analyzer = tools
        args.extend(
            [
                "--position-sampler",
                str(sampler),
                "--exact-label-dump",
                str(dump),
                "--eval-vs-exact",
                str(analyzer),
            ]
        )
    if extra_args:
        args.extend(extra_args)
    parsed = workflow.parse_args(args)
    return workflow.config_from_args(parsed, invocation=["exact_label_workflow.py", *args])


class ExactLabelWorkflowTests(unittest.TestCase):
    def test_dry_run_writes_report_with_planned_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                extra_args=["--dry-run", "--analyze", "--eval-preset", "default"],
            )

            exit_code = workflow.run_workflow(config)
            report = (config.out_dir / "workflow.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: dry run", report)
        self.assertIn("No strength claim", report)
        self.assertIn("## Exact Commands", report)
        self.assertIn("othello_position_sampler", report)
        self.assertIn("othello_exact_label_dump", report)
        self.assertIn("othello_eval_vs_exact", report)

    def test_command_construction_and_tool_overrides_are_respected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            tools = write_success_tools(temp_path)
            config = make_config(
                temp_path,
                tools=tools,
                extra_args=["--analyze", "--eval-config", str(temp_path / "current.eval")],
            )

            steps = workflow.build_steps(config)
            exit_code = workflow.run_workflow(config)
            report = (config.out_dir / "workflow.md").read_text(encoding="utf-8")

        self.assertEqual([step.name for step in steps], [
            "sample-positions",
            "dump-exact-labels",
            "analyze-eval-vs-exact",
        ])
        self.assertEqual(steps[0].command[0], str(tools[0]))
        self.assertEqual(steps[1].command[0], str(tools[1]))
        self.assertEqual(steps[2].command[0], str(tools[2]))
        self.assertIn("--eval-config", steps[2].command)
        self.assertEqual(exit_code, 0)
        self.assertIn(str(tools[0]), report)
        self.assertIn("sampled: `4`", report)
        self.assertIn("labeled: `4`", report)
        self.assertIn("analyzed: `4`", report)

    def test_analyze_requires_evaluator_selection(self) -> None:
        parsed = workflow.parse_args(["--analyze"])

        with self.assertRaises(ScriptError) as context:
            workflow.config_from_args(parsed)

        self.assertIn("--analyze requires", str(context.exception))

    def test_rejects_eval_preset_with_eval_config(self) -> None:
        parsed = workflow.parse_args(
            ["--eval-preset", "default", "--eval-config", "data/eval/current_default.eval"]
        )

        with self.assertRaises(ScriptError) as context:
            workflow.config_from_args(parsed)

        self.assertIn("cannot combine", str(context.exception))

    def test_failed_required_command_is_recorded_and_fails_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            sampler, _, analyzer = write_success_tools(temp_path)
            failing_dump = write_tool(
                temp_path,
                "failing_dump.py",
                """
                import sys
                print("dump failed", file=sys.stderr)
                sys.exit(7)
                """,
            )
            config = make_config(temp_path, tools=(sampler, failing_dump, analyzer))

            exit_code = workflow.run_workflow(config)
            report = (config.out_dir / "workflow.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 1)
        self.assertIn("Status: completed with failures", report)
        self.assertIn("dump-exact-labels", report)
        self.assertIn("status: `failed`", report)
        self.assertIn("exit_code: `7`", report)

    def test_allow_failures_changes_exit_code_but_records_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            sampler, _, analyzer = write_success_tools(temp_path)
            failing_dump = write_tool(
                temp_path,
                "failing_dump.py",
                """
                import sys
                print("dump failed", file=sys.stderr)
                sys.exit(7)
                """,
            )
            config = make_config(
                temp_path,
                tools=(sampler, failing_dump, analyzer),
                extra_args=["--allow-failures"],
            )

            exit_code = workflow.run_workflow(config)
            report = (config.out_dir / "workflow.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: completed with failures", report)
        self.assertIn("status: `failed`", report)

    def test_output_paths_default_under_workflow_out_dir(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp))

        self.assertEqual(config.positions_path, config.out_dir / "positions.txt")
        self.assertEqual(config.labels_path, config.out_dir / "labels.jsonl")
        self.assertEqual(config.report_path, config.out_dir / "eval-vs-exact.md")

    def test_skip_sampling_requires_positions(self) -> None:
        parsed = workflow.parse_args(["--skip-sampling"])

        with self.assertRaises(ScriptError) as context:
            workflow.config_from_args(parsed)

        self.assertIn("--skip-sampling requires --positions", str(context.exception))


if __name__ == "__main__":
    unittest.main()
