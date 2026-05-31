from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_candidate_matrix as matrix  # noqa: E402
from common import ScriptError  # noqa: E402


BASE_CONFIG_TEXT = """\
# schema_version: eval.v1
name=current_default

opening.mobility=8
midgame.mobility=10
late.mobility=6
opening_max_occupied=20
midgame_max_occupied=44
"""


CANDIDATE_CONFIG_TEXT = BASE_CONFIG_TEXT.replace("name=current_default", "name=candidate_a")


def write_file(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def write_tool(directory: Path, name: str, body: str) -> Path:
    path = directory / name
    path.write_text("#!/usr/bin/env python3\n" + textwrap.dedent(body), encoding="utf-8")
    path.chmod(0o755)
    return path


def write_fake_eval_vs_exact(directory: Path) -> Path:
    return write_tool(
        directory,
        "fake_eval_vs_exact.py",
        """
        import argparse
        import sys
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--labels", required=True)
        parser.add_argument("--output", required=True)
        parser.add_argument("--eval-config", required=True)
        args = parser.parse_args()

        if "fail" in Path(args.eval_config).stem:
            print("eval failed", file=sys.stderr)
            sys.exit(7)
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text("# fake eval-vs-exact\\n", encoding="utf-8")
        print("analyzed=3")
        """,
    )


def write_fake_search_bench(directory: Path) -> Path:
    return write_tool(
        directory,
        "fake_search_bench.py",
        """
        import argparse
        import sys
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--eval-config", required=True)
        parser.add_argument("--mode", required=True)
        parser.add_argument("--depths", required=True)
        parser.add_argument("--positions", required=True)
        parser.add_argument("--repetitions", required=True)
        parser.add_argument("--tt", required=True)
        parser.add_argument("--pvs", required=True)
        parser.add_argument("--aspiration", required=True)
        parser.add_argument("--exact-endgame-threshold", required=True)
        args = parser.parse_args()

        if "fail" in Path(args.eval_config).stem:
            print("search failed", file=sys.stderr)
            sys.exit(9)
        print(f"search ok {args.depths} {args.positions}")
        """,
    )


def make_config(
    temp_dir: Path,
    *,
    extra_args: list[str] | None = None,
    tools: tuple[Path, Path] | None = None,
    candidate_name: str = "candidate.eval",
) -> matrix.MatrixConfig:
    base = write_file(temp_dir / "base.eval", BASE_CONFIG_TEXT)
    candidate = write_file(temp_dir / candidate_name, CANDIDATE_CONFIG_TEXT)
    args = [
        "--baseline-config",
        str(base),
        "--candidates",
        str(candidate),
        "--out",
        str(temp_dir / "runs"),
    ]
    if tools is not None:
        eval_vs_exact, search_bench = tools
        args.extend(["--eval-vs-exact", str(eval_vs_exact), "--search-bench", str(search_bench)])
    if extra_args:
        args.extend(extra_args)
    parsed = matrix.parse_args(args)
    return matrix.config_from_args(parsed, invocation=["eval_candidate_matrix.py", *args])


class EvalCandidateMatrixTests(unittest.TestCase):
    def test_dry_run_writes_report_and_command_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, extra_args=["--dry-run"])

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")
            search_log = config.out_dir / "logs" / "candidate-candidate_a" / "search-bench.log"
            eval_log = config.out_dir / "logs" / "candidate-candidate_a" / "eval-vs-exact.log"
            search_log_exists = search_log.exists()
            eval_log_exists = eval_log.exists()

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: dry run", report)
        self.assertIn("No strength claim. No automatic default promotion.", report)
        self.assertIn("Search/eval numbers are smoke evidence", report)
        self.assertIn("othello_search_bench", report)
        self.assertTrue(search_log_exists)
        self.assertTrue(eval_log_exists)

    def test_builds_expected_search_command(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                extra_args=[
                    "--search-depths",
                    "4,5",
                    "--positions",
                    "suite",
                    "--repetitions",
                    "2",
                    "--exact-endgame-threshold",
                    "12",
                ],
            )
            target = matrix.make_targets(config)[1]

        command = matrix.search_bench_command(config, target)
        self.assertIn("--eval-config", command)
        self.assertEqual(command[command.index("--mode") + 1], "iterative")
        self.assertEqual(command[command.index("--depths") + 1], "4,5")
        self.assertEqual(command[command.index("--positions") + 1], "suite")
        self.assertEqual(command[command.index("--repetitions") + 1], "2")
        self.assertEqual(command[command.index("--tt") + 1], "on")
        self.assertEqual(command[command.index("--pvs") + 1], "on")
        self.assertEqual(command[command.index("--aspiration") + 1], "on")
        self.assertEqual(command[command.index("--exact-endgame-threshold") + 1], "12")

    def test_with_labels_runs_eval_and_search_for_baseline_and_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            tools = (write_fake_eval_vs_exact(temp_path), write_fake_search_bench(temp_path))
            labels = write_file(temp_path / "labels.jsonl", "{}\n")
            config = make_config(temp_path, tools=tools, extra_args=["--labels", str(labels)])

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")
            candidate_eval = config.out_dir / "eval-vs-exact" / "candidate-candidate_a.md"
            candidate_eval_exists = candidate_eval.exists()

        self.assertEqual(exit_code, 0)
        self.assertTrue(candidate_eval_exists)
        self.assertIn("baseline | current_default", report)
        self.assertIn("candidate | candidate_a", report)
        self.assertIn("passed", report)

    def test_failures_are_recorded_and_affect_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            tools = (write_fake_eval_vs_exact(temp_path), write_fake_search_bench(temp_path))
            labels = write_file(temp_path / "labels.jsonl", "{}\n")
            config = make_config(
                temp_path,
                tools=tools,
                candidate_name="fail.eval",
                extra_args=["--labels", str(labels)],
            )

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 1)
        self.assertIn("Status: completed with failures", report)
        self.assertIn("failure |", report)
        self.assertIn("yes", report)

    def test_allow_failures_returns_success_but_keeps_failure_in_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            tools = (write_fake_eval_vs_exact(temp_path), write_fake_search_bench(temp_path))
            labels = write_file(temp_path / "labels.jsonl", "{}\n")
            config = make_config(
                temp_path,
                tools=tools,
                candidate_name="fail.eval",
                extra_args=["--labels", str(labels), "--allow-failures"],
            )

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: completed with failures", report)
        self.assertIn("allow_failures: `true`", report)

    def test_rejects_duplicate_candidate_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = write_file(temp_path / "candidate.eval", CANDIDATE_CONFIG_TEXT)
            parsed = matrix.parse_args(
                [
                    "--candidates",
                    str(candidate),
                    str(candidate),
                    "--out",
                    str(temp_path / "runs"),
                ]
            )

            with self.assertRaises(ScriptError) as context:
                matrix.config_from_args(parsed)

        self.assertIn("duplicate candidate paths", str(context.exception))


if __name__ == "__main__":
    unittest.main()
