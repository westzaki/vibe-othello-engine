from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_config_search_validate as search_validate  # noqa: E402
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


CANDIDATE_TEXT = BASE_CONFIG_TEXT.replace("opening.mobility=8", "opening.mobility=9")


def write_file(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def write_tool(directory: Path, name: str, body: str) -> Path:
    path = directory / name
    path.write_text("#!/usr/bin/env python3\n" + textwrap.dedent(body), encoding="utf-8")
    path.chmod(0o755)
    return path


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
        parser.add_argument("--mode", required=True)
        parser.add_argument("--depths", required=True)
        parser.add_argument("--positions", required=True)
        parser.add_argument("--repetitions", required=True)
        parser.add_argument("--eval-config", required=True)
        parser.add_argument("--exact-endgame-threshold", required=True)
        parser.add_argument("--format", required=True)
        args = parser.parse_args()

        stem = Path(args.eval_config).stem
        if "fail" in stem:
            print("search failed", file=sys.stderr)
            sys.exit(7)
        checksum = "base-checksum" if stem == "base" else f"{stem}-checksum"
        print(json.dumps({
            "row": "aggregate",
            "mode": "fixed",
            "depth": int(args.depths.split(",")[0]),
            "result_checksum": checksum,
            "work_checksum": f"{checksum}-work",
            "nodes": 123,
            "elapsed_ms": 4.5,
        }))
        """,
    )


def write_fake_match_runner(directory: Path) -> Path:
    return write_tool(
        directory,
        "fake_match_runner.py",
        """
        import argparse
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--black", required=True)
        parser.add_argument("--white", required=True)
        parser.add_argument("--games", required=True)
        parser.add_argument("--swap-sides", required=True)
        parser.add_argument("--seed", required=True)
        parser.add_argument("--openings", required=True)
        parser.add_argument("--output", required=True)
        parser.add_argument("--quiet", action="store_true")
        args = parser.parse_args()

        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text('{"winner":"black"}\\n', encoding="utf-8")
        print("match ok")
        """,
    )


def write_fake_match_summary(directory: Path) -> Path:
    return write_tool(
        directory,
        "fake_match_summary.py",
        """
        import argparse

        parser = argparse.ArgumentParser()
        parser.add_argument("--input", required=True)
        parser.add_argument("--by-opening", action="store_true")
        parser.parse_args()
        print("Match summary: candidate smoke ok")
        """,
    )


def make_args(
    temp_dir: Path,
    *,
    candidates: list[Path] | None = None,
    extra_args: list[str] | None = None,
    search_bench: Path | None = None,
    match_runner: Path | None = None,
    match_summary: Path | None = None,
) -> list[str]:
    base = write_file(temp_dir / "base.eval", BASE_CONFIG_TEXT)
    if candidates is None:
        candidates = [write_file(temp_dir / "candidate_0001.eval", CANDIDATE_TEXT)]
    openings = write_file(temp_dir / "openings.txt", "f5 d6 c3\n")
    args = [
        "--base-config",
        str(base),
        "--candidate-configs",
        ",".join(str(candidate) for candidate in candidates),
        "--out",
        str(temp_dir / "runs"),
        "--openings",
        str(openings),
        "--top",
        "10",
        "--run-search-bench",
    ]
    if search_bench is not None:
        args.extend(["--search-bench", str(search_bench)])
    if match_runner is not None:
        args.extend(["--match-runner", str(match_runner)])
    if match_summary is not None:
        args.extend(["--match-summary", str(match_summary)])
    if extra_args:
        args.extend(extra_args)
    return args


def make_config(temp_dir: Path, *, extra_args: list[str] | None = None) -> search_validate.ValidationConfig:
    search_bench = write_fake_search_bench(temp_dir)
    match_runner = write_fake_match_runner(temp_dir)
    match_summary = write_fake_match_summary(temp_dir)
    args = make_args(
        temp_dir,
        extra_args=extra_args,
        search_bench=search_bench,
        match_runner=match_runner,
        match_summary=match_summary,
    )
    parsed = search_validate.parse_args(args)
    return search_validate.config_from_args(parsed, invocation=["eval_config_search_validate.py", *args])


class EvalConfigSearchValidateTests(unittest.TestCase):
    def test_rejects_missing_candidate_source(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            base = write_file(temp_path / "base.eval", BASE_CONFIG_TEXT)
            parsed = search_validate.parse_args(
                ["--base-config", str(base), "--run-search-bench"]
            )

            with self.assertRaises(ScriptError) as context:
                search_validate.config_from_args(parsed)

        self.assertIn("--candidate-configs or --candidate-dir", str(context.exception))

    def test_rejects_both_candidate_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = write_file(temp_path / "candidate.eval", CANDIDATE_TEXT)
            args = make_args(temp_path, candidates=[candidate], extra_args=["--candidate-dir", str(temp_path)])
            parsed = search_validate.parse_args(args)

            with self.assertRaises(ScriptError) as context:
                search_validate.config_from_args(parsed)

        self.assertIn("cannot combine", str(context.exception))

    def test_rejects_no_validation_mode(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            base = write_file(temp_path / "base.eval", BASE_CONFIG_TEXT)
            candidate = write_file(temp_path / "candidate.eval", CANDIDATE_TEXT)
            parsed = search_validate.parse_args(
                ["--base-config", str(base), "--candidate-configs", str(candidate)]
            )

            with self.assertRaises(ScriptError) as context:
                search_validate.config_from_args(parsed)

        self.assertIn("--run-search-bench", str(context.exception))

    def test_discovers_candidates_from_directory_sorted_by_filename(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate_dir = temp_path / "configs"
            beta = write_file(candidate_dir / "b.eval", CANDIDATE_TEXT)
            alpha = write_file(candidate_dir / "a.eval", CANDIDATE_TEXT)
            base = write_file(temp_path / "base.eval", BASE_CONFIG_TEXT)
            parsed = search_validate.parse_args(
                [
                    "--base-config",
                    str(base),
                    "--candidate-dir",
                    str(candidate_dir),
                    "--run-search-bench",
                ]
            )

            config = search_validate.config_from_args(parsed)

        self.assertEqual(config.candidate_configs, (alpha, beta))

    def test_rejects_duplicate_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = write_file(temp_path / "candidate.eval", CANDIDATE_TEXT)
            args = make_args(temp_path, candidates=[candidate, candidate])
            parsed = search_validate.parse_args(args)

            with self.assertRaises(ScriptError) as context:
                search_validate.config_from_args(parsed)

        self.assertIn("duplicate candidate paths", str(context.exception))

    def test_parses_heldout_summary_and_selects_top_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            first = write_file(temp_path / "candidate_a.eval", CANDIDATE_TEXT)
            second = write_file(temp_path / "candidate_b.eval", CANDIDATE_TEXT)
            summary = write_file(
                temp_path / "heldout.tsv",
                "rank\tcandidate\tstatus\tobjective\tdelta_vs_base\tanalyzed\tsign_agreements\t"
                "wrong_direction\thigh_confidence_wrong_direction\tconfig\treport\n"
                f"2\tcandidate_a\tpassed\t8\t1\t5\t4\t1\t0\t{first}\ta.md\n"
                f"1\tcandidate_b\tpassed\t10\t3\t5\t5\t0\t0\t{second}\tb.md\n",
            )
            args = make_args(
                temp_path,
                candidates=[first, second],
                extra_args=["--heldout-summary", str(summary), "--top", "1"],
            )
            parsed = search_validate.parse_args(args)
            config = search_validate.config_from_args(parsed)
            heldout = search_validate.parse_heldout_summary(summary)

            all_candidates, selected = search_validate.select_candidates(config, heldout)

        self.assertEqual([candidate.candidate_id for candidate in all_candidates], ["candidate_b", "candidate_a"])
        self.assertEqual([candidate.candidate_id for candidate in selected], ["candidate_b"])

    def test_falls_back_to_filename_order_without_heldout_summary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            first = write_file(temp_path / "a.eval", CANDIDATE_TEXT)
            second = write_file(temp_path / "b.eval", CANDIDATE_TEXT)
            args = make_args(temp_path, candidates=[first, second], extra_args=["--top", "1"])
            config = search_validate.config_from_args(search_validate.parse_args(args))

            all_candidates, selected = search_validate.select_candidates(config, None)

        self.assertEqual([candidate.candidate_id for candidate in all_candidates], ["a", "b"])
        self.assertEqual([candidate.candidate_id for candidate in selected], ["a"])

    def test_search_bench_command_includes_required_flags(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), extra_args=["--depths", "4"])

            command = search_validate.search_command(config, eval_config=Path(temp) / "candidate.eval")

        self.assertIn("--eval-config", command)
        self.assertIn("--exact-endgame-threshold", command)
        self.assertIn("--depths", command)
        self.assertEqual(command[command.index("--positions") + 1], "smoke")

    def test_search_bench_command_uses_configured_positions(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(
                Path(temp),
                extra_args=["--depths", "4", "--positions", "evaluation"],
            )

            command = search_validate.search_command(config, eval_config=Path(temp) / "candidate.eval")

        self.assertIn("--positions", command)
        self.assertEqual(command[command.index("--positions") + 1], "evaluation")

    def test_match_runner_command_includes_eval_configs_and_swap_sides(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate_path = write_file(temp_path / "candidate_0001.eval", CANDIDATE_TEXT)
            config = make_config(temp_path, extra_args=["--run-match-smoke"])
            candidate = search_validate.Candidate(
                candidate_id="candidate_0001",
                config_path=candidate_path,
                slug="candidate_0001",
                selected=True,
            )

            command = search_validate.match_command(
                config,
                candidate=candidate,
                jsonl_path=temp_path / "match.jsonl",
            )

        self.assertTrue(any(f"eval_config={candidate_path}" in part for part in command))
        self.assertTrue(any(f"eval_config={config.base_config}" in part for part in command))
        self.assertIn("--swap-sides", command)
        self.assertIn("true", command)

    def test_dry_run_writes_planned_commands_and_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), extra_args=["--run-match-smoke", "--dry-run"])

            exit_code = search_validate.run_validation(config)
            report = (config.out_dir / "search_match_validation_report.md").read_text(
                encoding="utf-8"
            )
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("planned", report)
        self.assertIn("No strength claim", report)
        self.assertIn("summary.tsv", str(config.out_dir / "summary.tsv"))
        self.assertIn("candidate\tselected", summary)

    def test_allow_failures_changes_exit_behavior_but_records_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            failing_candidate = write_file(temp_path / "candidate_fail.eval", CANDIDATE_TEXT)
            failing_args = make_args(
                temp_path,
                candidates=[failing_candidate],
                search_bench=write_fake_search_bench(temp_path),
                match_runner=write_fake_match_runner(temp_path),
                match_summary=write_fake_match_summary(temp_path),
            )
            allowed_args = [
                *failing_args,
                "--allow-failures",
                "--out",
                str(temp_path / "allowed"),
            ]
            failing_config = search_validate.config_from_args(search_validate.parse_args(failing_args))
            allowed_config = search_validate.config_from_args(search_validate.parse_args(allowed_args))

            failing_exit = search_validate.run_validation(failing_config)
            allowed_exit = search_validate.run_validation(allowed_config)
            report = (allowed_config.out_dir / "search_match_validation_report.md").read_text(
                encoding="utf-8"
            )

        self.assertEqual(failing_exit, 1)
        self.assertEqual(allowed_exit, 0)
        self.assertIn("## Failures", report)
        self.assertIn("failed", report)

    def test_report_contains_exact_commands_and_summary_is_written(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), extra_args=["--run-match-smoke"])

            exit_code = search_validate.run_validation(config)
            report = (config.out_dir / "search_match_validation_report.md").read_text(
                encoding="utf-8"
            )
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("No strength claim", report)
        self.assertIn("## Commands", report)
        self.assertIn("--eval-config", report)
        self.assertIn("positions: `smoke`", report)
        self.assertIn("--swap-sides true", report)
        self.assertIn("candidate_0001\tyes", summary)

    def test_tool_overrides_are_respected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            custom_search = temp_path / "custom_search"
            custom_match = temp_path / "custom_match"
            custom_summary = temp_path / "custom_summary.py"
            args = make_args(
                temp_path,
                search_bench=custom_search,
                match_runner=custom_match,
                match_summary=custom_summary,
            )
            config = search_validate.config_from_args(search_validate.parse_args(args))

        self.assertEqual(config.search_bench, custom_search)
        self.assertEqual(config.match_runner, custom_match)
        self.assertEqual(config.match_summary, custom_summary)


if __name__ == "__main__":
    unittest.main()
