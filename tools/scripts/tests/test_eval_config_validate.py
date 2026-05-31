from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_config_validate as validate  # noqa: E402
from common import ScriptError  # noqa: E402


BASE_CONFIG_TEXT = """\
# schema_version: eval.v1
name=current_default

opening.disc_difference=0
opening.mobility=8
midgame.mobility=10
late.mobility=6
opening_max_occupied=20
midgame_max_occupied=44
"""


BETTER_CONFIG_TEXT = BASE_CONFIG_TEXT.replace("opening.mobility=8", "opening.mobility=9")
WORSE_CONFIG_TEXT = BASE_CONFIG_TEXT.replace("opening.mobility=8", "opening.mobility=7")


def write_file(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def write_tool(directory: Path, name: str, body: str) -> Path:
    path = directory / name
    path.write_text("#!/usr/bin/env python3\n" + textwrap.dedent(body), encoding="utf-8")
    path.chmod(0o755)
    return path


def write_fake_analyzer(directory: Path) -> Path:
    return write_tool(
        directory,
        "fake_eval_vs_exact.py",
        """
        import argparse
        import re
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("--labels", required=True)
        parser.add_argument("--eval-config", required=True)
        parser.add_argument("--output", required=True)
        parser.add_argument("--high-confidence-threshold", required=True)
        parser.add_argument("--move-rank-analysis", action="store_true")
        args = parser.parse_args()

        text = Path(args.eval_config).read_text(encoding="utf-8")
        match = re.search(r"opening\\.mobility=(-?\\d+)", text)
        mobility = int(match.group(1)) if match else 8
        if mobility > 8:
            sign_agreements = 5
            wrong_direction = 0
            high_confidence = 0
        elif mobility < 8:
            sign_agreements = 4
            wrong_direction = 2
            high_confidence = 1
        else:
            sign_agreements = 4
            wrong_direction = 1
            high_confidence = 0

        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text("# fake report\\n", encoding="utf-8")
        output = (
            "eval vs exact: records_read=5 analyzed=5 skipped=0 "
            f"sign_agreements={sign_agreements} "
            f"wrong_direction={wrong_direction} "
            f"high_confidence_wrong_direction={high_confidence} "
        )
        if args.move_rank_analysis:
            output += (
                "move_rank_records_with_scores=3 "
                "move_rank_records_missing_scores=2 "
                "move_rank_records_no_legal_root_moves=0 "
                "move_rank_analyzed=3 "
                "move_rank_top_exact_best=2 "
                "move_rank_top_non_best=1 "
                "move_rank_exact_best_rank_sum=4 "
                "move_rank_eval_score_gap_sum=17 "
                "move_rank_exact_score_gap_sum=9 "
            )
        print(
            output
            + f"output={args.output}"
        )
        """,
    )


def make_args(
    temp_dir: Path,
    *,
    extra_args: list[str] | None = None,
    analyzer: Path | None = None,
    candidates: list[Path] | None = None,
) -> list[str]:
    labels = write_file(temp_dir / "heldout.jsonl", "{}\n")
    base_config = write_file(temp_dir / "base.eval", BASE_CONFIG_TEXT)
    if candidates is None:
        candidates = [write_file(temp_dir / "candidate_0001.eval", BETTER_CONFIG_TEXT)]
    args = [
        "--validation-labels",
        str(labels),
        "--base-config",
        str(base_config),
        "--candidate-configs",
        ",".join(str(candidate) for candidate in candidates),
        "--out",
        str(temp_dir / "runs"),
        "--top",
        "10",
    ]
    if analyzer is not None:
        args.extend(["--eval-vs-exact", str(analyzer)])
    if extra_args:
        args.extend(extra_args)
    return args


def make_config(
    temp_dir: Path,
    *,
    extra_args: list[str] | None = None,
    analyzer: Path | None = None,
    candidates: list[Path] | None = None,
) -> validate.ValidationConfig:
    args = make_args(
        temp_dir,
        extra_args=extra_args,
        analyzer=analyzer,
        candidates=candidates,
    )
    parsed = validate.parse_args(args)
    return validate.config_from_args(parsed, invocation=["eval_config_validate.py", *args])


class EvalConfigValidateTests(unittest.TestCase):
    def test_rejects_missing_candidate_source(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_file(temp_path / "labels.jsonl", "{}\n")
            base = write_file(temp_path / "base.eval", BASE_CONFIG_TEXT)
            parsed = validate.parse_args(
                [
                    "--validation-labels",
                    str(labels),
                    "--base-config",
                    str(base),
                ]
            )

            with self.assertRaises(ScriptError) as context:
                validate.config_from_args(parsed)

        self.assertIn("--candidate-configs or --candidate-dir", str(context.exception))

    def test_rejects_both_candidate_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = write_file(temp_path / "candidate.eval", BETTER_CONFIG_TEXT)
            args = make_args(
                temp_path,
                candidates=[candidate],
                extra_args=["--candidate-dir", str(temp_path)],
            )
            parsed = validate.parse_args(args)

            with self.assertRaises(ScriptError) as context:
                validate.config_from_args(parsed)

        self.assertIn("cannot combine", str(context.exception))

    def test_discovers_candidates_from_directory_sorted_by_filename(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate_dir = temp_path / "configs"
            beta = write_file(candidate_dir / "b.eval", BETTER_CONFIG_TEXT)
            alpha = write_file(candidate_dir / "a.eval", WORSE_CONFIG_TEXT)
            labels = write_file(temp_path / "labels.jsonl", "{}\n")
            base = write_file(temp_path / "base.eval", BASE_CONFIG_TEXT)
            parsed = validate.parse_args(
                [
                    "--validation-labels",
                    str(labels),
                    "--base-config",
                    str(base),
                    "--candidate-dir",
                    str(candidate_dir),
                ]
            )

            config = validate.config_from_args(parsed)

        self.assertEqual(config.candidate_configs, (alpha, beta))

    def test_rejects_duplicate_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = write_file(temp_path / "candidate.eval", BETTER_CONFIG_TEXT)
            args = make_args(temp_path, candidates=[candidate, candidate])
            parsed = validate.parse_args(args)

            with self.assertRaises(ScriptError) as context:
                validate.config_from_args(parsed)

        self.assertIn("duplicate candidate paths", str(context.exception))

    def test_analyzer_command_includes_required_flags(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, analyzer=temp_path / "eval_vs_exact")

            command = validate.analyzer_command(
                config,
                eval_config_path=temp_path / "candidate.eval",
                report_path=temp_path / "candidate.md",
            )

        self.assertIn("--eval-config", command)
        self.assertIn("--labels", command)
        self.assertIn("--output", command)
        self.assertIn("--high-confidence-threshold", command)

    def test_analyzer_command_passes_move_rank_flag_when_enabled(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                analyzer=temp_path / "eval_vs_exact",
                extra_args=["--move-rank-analysis"],
            )

            command = validate.analyzer_command(
                config,
                eval_config_path=temp_path / "candidate.eval",
                report_path=temp_path / "candidate.md",
            )

        self.assertIn("--move-rank-analysis", command)

    def test_fake_analyzer_stdout_is_parsed(self) -> None:
        stdout = (
            "eval vs exact: records_read=5 analyzed=5 skipped=0 sign_agreements=4 "
            "wrong_direction=1 high_confidence_wrong_direction=0 output=report.md"
        )

        metrics = validate.parse_analyzer_stdout(stdout)

        self.assertEqual(metrics.records_read, 5)
        self.assertEqual(metrics.analyzed, 5)
        self.assertEqual(metrics.sign_agreements, 4)
        self.assertEqual(metrics.wrong_direction, 1)
        self.assertEqual(metrics.high_confidence_wrong_direction, 0)
        self.assertIsNone(metrics.move_rank_analyzed)

    def test_fake_analyzer_stdout_with_move_rank_metrics_is_parsed(self) -> None:
        stdout = (
            "eval vs exact: records_read=5 analyzed=5 skipped=0 sign_agreements=4 "
            "wrong_direction=1 high_confidence_wrong_direction=0 "
            "move_rank_records_with_scores=3 "
            "move_rank_records_missing_scores=2 "
            "move_rank_records_no_legal_root_moves=0 "
            "move_rank_analyzed=3 "
            "move_rank_top_exact_best=2 "
            "move_rank_top_non_best=1 "
            "move_rank_exact_best_rank_sum=4 "
            "move_rank_eval_score_gap_sum=17 "
            "move_rank_exact_score_gap_sum=9 "
            "output=report.md"
        )

        metrics = validate.parse_analyzer_stdout(stdout)

        self.assertEqual(metrics.move_rank_records_with_scores, 3)
        self.assertEqual(metrics.move_rank_records_missing_scores, 2)
        self.assertEqual(metrics.move_rank_analyzed, 3)
        self.assertEqual(metrics.move_rank_top_exact_best, 2)
        self.assertEqual(metrics.move_rank_top_non_best, 1)
        self.assertEqual(metrics.move_rank_eval_score_gap_sum, 17)

    def test_objective_and_delta_vs_base_are_computed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            better = write_file(temp_path / "candidate_0001.eval", BETTER_CONFIG_TEXT)
            worse = write_file(temp_path / "candidate_0002.eval", WORSE_CONFIG_TEXT)
            config = make_config(
                temp_path,
                analyzer=write_fake_analyzer(temp_path),
                candidates=[better, worse],
            )

            exit_code = validate.run_validation(config)
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("candidate_0001\tpassed\t5\t3\t5\t5\t0\t0", summary)
        self.assertIn("candidate_0002\tpassed\t-1\t-3\t5\t4\t2\t1", summary)

    def test_move_rank_analysis_records_metrics_without_changing_objective(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            better = write_file(temp_path / "candidate_0001.eval", BETTER_CONFIG_TEXT)
            config = make_config(
                temp_path,
                analyzer=write_fake_analyzer(temp_path),
                candidates=[better],
                extra_args=["--move-rank-analysis"],
            )

            exit_code = validate.run_validation(config)
            report = (config.out_dir / "validation_report.md").read_text(encoding="utf-8")
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("## Move-Rank Metrics", report)
        self.assertIn("move_rank_analysis: `true`", report)
        self.assertIn("move_rank_analyzed", summary)
        self.assertIn("\t3\t2\t0\t3\t2\t1\t4\t17\t9\t", summary)
        self.assertIn("candidate_0001\tpassed\t5\t3\t5\t5\t0\t0", summary)

    def test_report_contains_no_strength_claim_heldout_and_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, analyzer=write_fake_analyzer(temp_path))

            exit_code = validate.run_validation(config)
            report = (config.out_dir / "validation_report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("No strength claim", report)
        self.assertIn("Held-Out", report)
        self.assertIn("base_config_sha256", report)
        self.assertIn("validation_labels_sha256", report)

    def test_dry_run_writes_planned_commands_and_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                analyzer=write_fake_analyzer(temp_path),
                extra_args=["--dry-run"],
            )

            exit_code = validate.run_validation(config)
            report = (config.out_dir / "validation_report.md").read_text(encoding="utf-8")
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("planned", report)
        self.assertIn("No strength claim", report)
        self.assertIn("status", summary)

    def test_allow_failures_changes_exit_behavior_but_records_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            failing = write_tool(
                temp_path,
                "failing_eval_vs_exact.py",
                """
                import sys
                print("boom", file=sys.stderr)
                sys.exit(7)
                """,
            )
            failing_config = make_config(temp_path, analyzer=failing)
            allowed_config = make_config(
                temp_path,
                analyzer=failing,
                extra_args=[
                    "--allow-failures",
                    "--out",
                    str(temp_path / "allowed"),
                ],
            )

            failing_exit = validate.run_validation(failing_config)
            allowed_exit = validate.run_validation(allowed_config)
            report = (allowed_config.out_dir / "validation_report.md").read_text(
                encoding="utf-8"
            )

        self.assertEqual(failing_exit, 1)
        self.assertEqual(allowed_exit, 0)
        self.assertIn("## Failures", report)
        self.assertIn("failed", report)

    def test_train_summary_is_parsed_and_included(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = write_file(temp_path / "candidate_0001.eval", BETTER_CONFIG_TEXT)
            train_summary = write_file(
                temp_path / "summary.tsv",
                "rank\tcandidate\tstatus\tobjective\tanalyzed\tsign_agreements\t"
                "wrong_direction\thigh_confidence_wrong_direction\tconfig\treport\n"
                f"1\tcandidate_0001\tpassed\t12\t5\t5\t0\t0\t{candidate}\ttrain.md\n",
            )
            config = make_config(
                temp_path,
                analyzer=write_fake_analyzer(temp_path),
                candidates=[candidate],
                extra_args=["--train-summary", str(train_summary)],
            )

            exit_code = validate.run_validation(config)
            report = (config.out_dir / "validation_report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("## Train vs Held-Out", report)
        self.assertIn("| 1 | `candidate_0001` | 1 | 12 | 5 | 3 |", report)

    def test_candidate_diff_vs_base_is_shown_for_top_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = write_file(temp_path / "candidate_0001.eval", BETTER_CONFIG_TEXT)
            config = make_config(
                temp_path,
                analyzer=write_fake_analyzer(temp_path),
                candidates=[candidate],
            )

            exit_code = validate.run_validation(config)
            report = (config.out_dir / "validation_report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("`opening.mobility`: `8` -> `9`", report)


if __name__ == "__main__":
    unittest.main()
