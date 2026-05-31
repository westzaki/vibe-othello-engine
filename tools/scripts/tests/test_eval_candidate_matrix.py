from __future__ import annotations

import csv
import hashlib
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
        import json
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
        parser.add_argument("--format", required=True)
        args = parser.parse_args()

        stem = Path(args.eval_config).stem
        if "fail" in stem:
            print("search failed", file=sys.stderr)
            sys.exit(9)
        if "malformed" in stem:
            print("{not-json")
            sys.exit(0)
        if args.format != "jsonl":
            print("expected jsonl", file=sys.stderr)
            sys.exit(11)

        is_candidate = "candidate" in stem or "malformed" in stem
        nodes_base = 150 if is_candidate else 100
        elapsed_base = 12.5 if is_candidate else 10.0
        result_prefix = "candidate" if is_candidate else "baseline"
        for depth_text in args.depths.split(","):
            depth = int(depth_text)
            print(json.dumps({
                "tool": "othello_search_bench",
                "row": "aggregate",
                "mode": args.mode,
                "depth": depth,
                "positions": args.positions,
                "repetitions": int(args.repetitions),
                "tt": True,
                "pvs": True,
                "aspiration": True,
                "exact_endgame_profile": "threshold=0",
                "exact_root_positions": depth,
                "exact_root_searches": depth * 2,
                "position_count": 2,
                "best_move": "D3",
                "score": depth,
                "score_kind": "heuristic",
                "used_exact_endgame": False,
                "exact_disc_margin": None,
                "principal_variation": ["D3", "C3"],
                "searches": 2,
                "elapsed_ms": elapsed_base + depth,
                "nodes": nodes_base + depth,
                "result_checksum": f"{result_prefix}-result-{depth}",
                "work_checksum": f"{result_prefix}-work-{depth}",
            }))
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


def read_summary(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


class EvalCandidateMatrixTests(unittest.TestCase):
    def test_dry_run_writes_report_summary_and_command_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, extra_args=["--dry-run"])

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")
            search_log = config.out_dir / "logs" / "candidate-candidate_a" / "search-bench.log"
            eval_log = config.out_dir / "logs" / "candidate-candidate_a" / "eval-vs-exact.log"
            search_jsonl = config.out_dir / "search-bench" / "candidate-candidate_a.jsonl"
            search_log_exists = search_log.exists()
            eval_log_exists = eval_log.exists()

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: dry run", report)
        self.assertIn("No strength claim. No automatic default promotion.", report)
        self.assertIn("Search/eval numbers are smoke evidence", report)
        self.assertIn("summary_tsv", report)
        self.assertIn(str(search_jsonl), report)
        self.assertIn("candidate-candidate_a.jsonl", summary)
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
        self.assertEqual(command[command.index("--format") + 1], "jsonl")
        self.assertNotIn("--by-position", command)

    def test_with_labels_runs_eval_and_search_and_summarizes_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            tools = (write_fake_eval_vs_exact(temp_path), write_fake_search_bench(temp_path))
            labels = write_file(temp_path / "labels.jsonl", "{}\n")
            config = make_config(
                temp_path,
                tools=tools,
                extra_args=["--labels", str(labels), "--search-depths", "5"],
            )

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")
            summary_rows = read_summary(config.out_dir / "summary.tsv")
            candidate_eval = config.out_dir / "eval-vs-exact" / "candidate-candidate_a.md"
            candidate_jsonl = config.out_dir / "search-bench" / "candidate-candidate_a.jsonl"
            candidate = next(row for row in summary_rows if row["role"] == "candidate")
            candidate_eval_exists = candidate_eval.exists()
            candidate_jsonl_exists = candidate_jsonl.exists()
            candidate_jsonl_text = candidate_jsonl.read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertTrue(candidate_eval_exists)
        self.assertTrue(candidate_jsonl_exists)
        self.assertIn("\"row\": \"aggregate\"", candidate_jsonl_text)
        self.assertIn("baseline | current_default", report)
        self.assertIn("candidate | candidate_a", report)
        self.assertIn("Checksum changes mean search behavior changed", report)
        self.assertEqual(candidate["aggregate_rows"], "1")
        self.assertEqual(candidate["position_rows"], "0")
        self.assertEqual(candidate["nodes"], "155")
        self.assertEqual(candidate["result_checksum_changed"], "true")
        self.assertEqual(candidate["work_checksum_changed"], "true")
        self.assertEqual(candidate["baseline_comparable"], "true")

    def test_config_sha256_is_recorded(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            tools = (write_fake_eval_vs_exact(temp_path), write_fake_search_bench(temp_path))
            config = make_config(temp_path, tools=tools, extra_args=["--search-depths", "5"])
            expected = hashlib.sha256(CANDIDATE_CONFIG_TEXT.encode("utf-8")).hexdigest()

            exit_code = matrix.run_matrix(config)
            summary_rows = read_summary(config.out_dir / "summary.tsv")
            candidate = next(row for row in summary_rows if row["role"] == "candidate")
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertEqual(candidate["config_sha256"], expected)
        self.assertIn(expected, report)

    def test_malformed_search_jsonl_is_a_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            tools = (write_fake_eval_vs_exact(temp_path), write_fake_search_bench(temp_path))
            config = make_config(
                temp_path,
                tools=tools,
                candidate_name="malformed.eval",
                extra_args=["--search-depths", "5"],
            )

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")
            log = config.out_dir / "logs" / "candidate-candidate_a" / "search-bench.log"
            summary_rows = read_summary(config.out_dir / "summary.tsv")
            candidate = next(row for row in summary_rows if row["role"] == "candidate")
            log_text = log.read_text(encoding="utf-8")

        self.assertEqual(exit_code, 1)
        self.assertIn("Status: completed with failures", report)
        self.assertIn("malformed JSONL", report)
        self.assertIn("parse_error: malformed JSONL", log_text)
        self.assertEqual(candidate["failure"], "yes")

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

    def test_missing_config_is_warning_in_dry_run(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            base = write_file(temp_path / "base.eval", BASE_CONFIG_TEXT)
            missing = temp_path / "missing.eval"
            args = [
                "--baseline-config",
                str(base),
                "--candidates",
                str(missing),
                "--out",
                str(temp_path / "runs"),
                "--dry-run",
            ]
            config = matrix.config_from_args(matrix.parse_args(args), invocation=["eval_candidate_matrix.py", *args])

            exit_code = matrix.run_matrix(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")
            summary_rows = read_summary(config.out_dir / "summary.tsv")
            candidate = next(row for row in summary_rows if row["role"] == "candidate")

        self.assertEqual(exit_code, 0)
        self.assertIn("unable to read eval config", report)
        self.assertEqual(candidate["config_sha256"], "n/a")
        self.assertIn("unable to read eval config", candidate["config_warning"])

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
