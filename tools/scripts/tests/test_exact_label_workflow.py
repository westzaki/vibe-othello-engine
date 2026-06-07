from __future__ import annotations

import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import exact_label_workflow as workflow  # noqa: E402
from common import ScriptError  # noqa: E402
from pattern_training.board9 import apply_move_to_board, legal_moves_for_board  # noqa: E402


INITIAL_BOARD = "\n".join(
    [
        "........",
        "........",
        "........",
        "...WB...",
        "...BW...",
        "........",
        "........",
        "........",
        "side=B",
    ]
)


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
        parser.add_argument("--eval-config")
        args = parser.parse_args()

        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text("# fake eval-vs-exact\\n", encoding="utf-8")
        print(f"fake analyzer records_read=4 analyzed=4 output={args.output}")
        """,
    )
    return sampler, dump, analyzer


def write_phase_exact_dump_tool(directory: Path, *, complete_move_scores: bool = True) -> Path:
    move_scores_expr = "{}"
    if complete_move_scores:
        move_scores_expr = (
            "[{'move': f'{col}{row}', 'exact_score_side_to_move': 0} "
            "for col in 'abcdefgh' for row in '12345678'] + "
            "[{'move': 'PASS', 'exact_score_side_to_move': 0}]"
        )
    return write_tool(
        directory,
        "fake_phase_dump.py",
        f"""
        import argparse
        import json
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--input", required=True)
        parser.add_argument("--output", required=True)
        parser.add_argument("--max-empties", required=True)
        parser.add_argument("--include-move-scores", action="store_true")
        args = parser.parse_args()

        boards = []
        current = []
        for line in Path(args.input).read_text(encoding="utf-8").splitlines():
            if line.startswith("#"):
                continue
            if not line.strip():
                if current:
                    boards.append("\\n".join(current))
                    current = []
                continue
            current.append(line)
        if current:
            boards.append("\\n".join(current))

        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        with Path(args.output).open("w", encoding="utf-8") as output:
            for board in boards:
                record = {{"board": board, "exact_elapsed_ms": 1.5, "exact_nodes": 42}}
                if args.include_move_scores:
                    record["move_scores"] = {move_scores_expr}
                output.write(json.dumps(record, sort_keys=True) + "\\n")
        print(f"fake dump labeled={{len(boards)}} output={{args.output}}")
        """,
    )


def board_after_legal_moves(count: int) -> str:
    board = INITIAL_BOARD
    for _ in range(count):
        move = sorted(legal_moves_for_board(board))[0]
        board = apply_move_to_board(board, move)
    return board


def teacher_row(board: str, *, split: str = "train") -> dict[str, str]:
    return {
        "board": board,
        "move": sorted(legal_moves_for_board(board))[0],
        "status": "ok",
        "position_split": split,
    }


def write_teacher_labels(path: Path, records: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(json.dumps(record, sort_keys=True) + "\n" for record in records),
        encoding="utf-8",
    )


def write_eval_config(path: Path, *, opening_max: int = 10, midgame_max: int = 40) -> None:
    path.write_text(
        f"opening_max_occupied = {opening_max}\nmidgame_max_occupied = {midgame_max}\n",
        encoding="utf-8",
    )


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
                extra_args=["--dry-run", "--analyze"],
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

    def test_analyze_without_eval_config_uses_default_evaluator(self) -> None:
        parsed = workflow.parse_args(["--analyze"])

        config = workflow.config_from_args(parsed)

        self.assertIsNone(config.eval_config)
        self.assertNotIn("--eval-config", workflow.analyzer_command(config))

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

    def test_phase_target_parser_accepts_map(self) -> None:
        targets = workflow.parse_phase_targets("opening=1,midgame=2,late=3")

        self.assertEqual(targets, {"opening": 1, "midgame": 2, "late": 3})

    def test_phase_target_parser_rejects_unknown_phase(self) -> None:
        with self.assertRaises(Exception) as context:
            workflow.parse_phase_targets("opening=1,endgame=2")

        self.assertIn("unknown phase", str(context.exception))

    def test_phase_split_respects_eval_config_cutoffs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            runs_dir = repo_root / "runs"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            dump = write_phase_exact_dump_tool(temp_path)
            write_eval_config(eval_config, opening_max=10, midgame_max=40)
            write_teacher_labels(
                teacher_path,
                [
                    teacher_row(board_after_legal_moves(2)),
                    teacher_row(board_after_legal_moves(20)),
                    teacher_row(board_after_legal_moves(44)),
                ],
            )
            args = workflow.parse_args(
                [
                    "--exact-phase-targets",
                    "opening=1,midgame=1,late=1",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    str(runs_dir / "phase-exact"),
                    "--eval-config",
                    str(eval_config),
                    "--exact-label-dump",
                    str(dump),
                    "--exact-require-complete-move-scores",
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)
                exit_code = workflow.run_workflow(config)
            summary = json.loads((runs_dir / "phase-exact" / "summary.json").read_text(encoding="utf-8"))

        self.assertEqual(exit_code, 0)
        self.assertEqual(summary["phase_selected_for_exact"], {"late": 1, "midgame": 1, "opening": 1})
        self.assertEqual(summary["phase_complete_move_scores"], {"late": 1, "midgame": 1, "opening": 1})

    def test_phase_balanced_out_dir_rejects_non_runs_and_docs_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            write_eval_config(eval_config)
            base_args = [
                "--exact-phase-targets",
                "opening=1,midgame=0,late=0",
                "--exact-source-teacher-labels",
                str(teacher_path),
                "--eval-config",
                str(eval_config),
            ]
            for bad_path in (repo_root / "not-runs" / "x", repo_root / "docs" / "runs" / "x"):
                args = workflow.parse_args(
                    [*base_args, "--exact-phase-balanced-out-dir", str(bad_path)]
                )
                with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                    with self.assertRaises(ScriptError):
                        workflow.config_from_args(args)

    def test_phase_balanced_out_dir_resolves_relative_runs_from_repo_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            write_eval_config(eval_config)
            args = workflow.parse_args(
                [
                    "--exact-phase-targets",
                    "opening=1,midgame=0,late=0",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    "runs/phase-exact",
                    "--eval-config",
                    str(eval_config),
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)

        self.assertEqual(config.exact_phase_balanced_out_dir, repo_root / "runs" / "phase-exact")

    def test_phase_aware_mode_does_not_run_normal_sampling_workflow(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            phase_out_dir = repo_root / "runs" / "phase-exact"
            normal_out_dir = repo_root / "runs" / "normal-workflow"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            dump = write_phase_exact_dump_tool(temp_path)
            failing_sampler = write_tool(
                temp_path,
                "failing_sampler.py",
                """
                import sys
                print("normal sampler should not run", file=sys.stderr)
                sys.exit(9)
                """,
            )
            write_eval_config(eval_config, opening_max=10, midgame_max=40)
            write_teacher_labels(teacher_path, [teacher_row(board_after_legal_moves(2))])
            args = workflow.parse_args(
                [
                    "--out",
                    str(normal_out_dir),
                    "--exact-phase-targets",
                    "opening=1,midgame=0,late=0",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    str(phase_out_dir),
                    "--eval-config",
                    str(eval_config),
                    "--position-sampler",
                    str(failing_sampler),
                    "--exact-label-dump",
                    str(dump),
                    "--exact-require-complete-move-scores",
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)
                exit_code = workflow.run_workflow(config)
            phase_outputs_exist = (
                (phase_out_dir / "teacher_selected_for_exact.jsonl").exists()
                and (phase_out_dir / "exact_phase_balanced.jsonl").exists()
            )
            normal_outputs_exist = (
                (normal_out_dir / "positions.txt").exists()
                or (normal_out_dir / "labels.jsonl").exists()
                or (normal_out_dir / "workflow.md").exists()
            )

        self.assertEqual(exit_code, 0)
        self.assertTrue(phase_outputs_exist)
        self.assertFalse(normal_outputs_exist)

    def test_phase_exact_shortage_fails_by_default_before_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            out_dir = repo_root / "runs" / "phase-exact"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            dump = write_phase_exact_dump_tool(temp_path)
            write_eval_config(eval_config, opening_max=10, midgame_max=40)
            write_teacher_labels(teacher_path, [teacher_row(board_after_legal_moves(2))])
            args = workflow.parse_args(
                [
                    "--exact-phase-targets",
                    "opening=2,midgame=0,late=0",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    str(out_dir),
                    "--eval-config",
                    str(eval_config),
                    "--exact-label-dump",
                    str(dump),
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)
                with self.assertRaises(ScriptError) as context:
                    workflow.run_workflow(config)

        self.assertIn("phase-aware exact target shortage", str(context.exception))
        self.assertFalse((out_dir / "teacher_selected_for_exact.jsonl").exists())

    def test_allow_shortage_writes_partial_and_reports_shortage(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            out_dir = repo_root / "runs" / "phase-exact"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            dump = write_phase_exact_dump_tool(temp_path)
            write_eval_config(eval_config, opening_max=10, midgame_max=40)
            write_teacher_labels(teacher_path, [teacher_row(board_after_legal_moves(2))])
            args = workflow.parse_args(
                [
                    "--exact-phase-targets",
                    "opening=2,midgame=0,late=0",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    str(out_dir),
                    "--eval-config",
                    str(eval_config),
                    "--exact-label-dump",
                    str(dump),
                    "--allow-shortage",
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)
                exit_code = workflow.run_workflow(config)
            summary = json.loads((out_dir / "summary.json").read_text(encoding="utf-8"))
            selected_exists = (out_dir / "teacher_selected_for_exact.jsonl").exists()

        self.assertEqual(exit_code, 0)
        self.assertEqual(summary["phase_shortage"]["opening"], 1)
        self.assertTrue(selected_exists)

    def test_complete_move_scores_requirement_is_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            out_dir = repo_root / "runs" / "phase-exact"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            dump = write_phase_exact_dump_tool(temp_path, complete_move_scores=False)
            write_eval_config(eval_config, opening_max=10, midgame_max=40)
            write_teacher_labels(teacher_path, [teacher_row(board_after_legal_moves(2))])
            args = workflow.parse_args(
                [
                    "--exact-phase-targets",
                    "opening=1,midgame=0,late=0",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    str(out_dir),
                    "--eval-config",
                    str(eval_config),
                    "--exact-label-dump",
                    str(dump),
                    "--exact-require-complete-move-scores",
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)
                exit_code = workflow.run_workflow(config)
            summary = json.loads((out_dir / "summary.json").read_text(encoding="utf-8"))

        self.assertEqual(exit_code, 1)
        self.assertEqual(summary["phase_complete_move_scores"]["opening"], 0)
        self.assertEqual(summary["phase_complete_move_scores_shortage"]["opening"], 1)

    def test_report_includes_phase_exact_coverage_section(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            out_dir = repo_root / "runs" / "phase-exact"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            dump = write_phase_exact_dump_tool(temp_path)
            write_eval_config(eval_config, opening_max=10, midgame_max=40)
            write_teacher_labels(teacher_path, [teacher_row(board_after_legal_moves(2))])
            args = workflow.parse_args(
                [
                    "--exact-phase-targets",
                    "opening=1,midgame=0,late=0",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    str(out_dir),
                    "--eval-config",
                    str(eval_config),
                    "--exact-label-dump",
                    str(dump),
                    "--exact-require-complete-move-scores",
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)
                workflow.run_workflow(config)
            report = (out_dir / "report.md").read_text(encoding="utf-8")

        self.assertIn("## Phase Exact Coverage", report)
        self.assertIn("complete_move_scores", report)

    def test_source_controlled_eval_default_files_are_not_changed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            repo_root = temp_path / "repo"
            out_dir = repo_root / "runs" / "phase-exact"
            teacher_path = temp_path / "teacher.jsonl"
            eval_config = temp_path / "phase.eval"
            dump = write_phase_exact_dump_tool(temp_path)
            write_eval_config(eval_config, opening_max=10, midgame_max=40)
            write_teacher_labels(teacher_path, [teacher_row(board_after_legal_moves(2))])
            args = workflow.parse_args(
                [
                    "--exact-phase-targets",
                    "opening=1,midgame=0,late=0",
                    "--exact-source-teacher-labels",
                    str(teacher_path),
                    "--exact-phase-balanced-out-dir",
                    str(out_dir),
                    "--eval-config",
                    str(eval_config),
                    "--exact-label-dump",
                    str(dump),
                    "--exact-require-complete-move-scores",
                ]
            )
            with mock.patch.object(workflow, "REPO_ROOT", repo_root):
                config = workflow.config_from_args(args)
                workflow.run_workflow(config)

        self.assertFalse((repo_root / "current_default.eval").exists())
        self.assertFalse((repo_root / "data").exists())


if __name__ == "__main__":
    unittest.main()
