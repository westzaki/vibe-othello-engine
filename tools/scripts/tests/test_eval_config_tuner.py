from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_config_tuner as tuner  # noqa: E402
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


def make_config(
    temp_dir: Path,
    *,
    extra_args: list[str] | None = None,
    analyzer: Path | None = None,
) -> tuner.TunerConfig:
    labels = write_file(temp_dir / "labels.jsonl", "{}\n")
    base_config = write_file(temp_dir / "base.eval", BASE_CONFIG_TEXT)
    args = [
        "--labels",
        str(labels),
        "--base-config",
        str(base_config),
        "--out",
        str(temp_dir / "runs"),
        "--rounds",
        "1",
        "--step",
        "1",
        "--max-candidates",
        "8",
        "--seed",
        "20260531",
    ]
    if analyzer is not None:
        args.extend(["--eval-vs-exact", str(analyzer)])
    if extra_args:
        args.extend(extra_args)
    parsed = tuner.parse_args(args)
    return tuner.config_from_args(parsed, invocation=["eval_config_tuner.py", *args])


class EvalConfigTunerTests(unittest.TestCase):
    def test_eval_parser_reads_comments_name_and_numeric_keys(self) -> None:
        parsed = tuner.parse_eval_config_text(BASE_CONFIG_TEXT)

        self.assertEqual(parsed.name, "current_default")
        self.assertEqual(parsed.values()["opening.mobility"], 8)
        self.assertEqual(parsed.values()["opening_max_occupied"], 20)

    def test_eval_parser_preserves_pattern_table_path(self) -> None:
        parsed = tuner.parse_eval_config_text(
            BASE_CONFIG_TEXT + "pattern_table=patterns/pattern_teacher_v0.tsv\n"
        )

        self.assertEqual(parsed.text_entries[0].key, "pattern_table")
        self.assertEqual(parsed.text_entries[0].value, "patterns/pattern_teacher_v0.tsv")

    def test_eval_parser_rejects_duplicate_numeric_keys(self) -> None:
        with self.assertRaises(ScriptError) as context:
            tuner.parse_eval_config_text("name=x\nopening.mobility=8\nopening.mobility=9\n")

        self.assertIn("duplicate numeric key", str(context.exception))

    def test_generated_config_is_fully_expanded_and_has_metadata_comments(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, analyzer=write_fake_analyzer(temp_path))
            base = tuner.load_eval_config(config.base_config)
            metadata = tuner.collect_metadata(config)
            spec = tuner.generate_round_candidates(
                base,
                ["opening.mobility"],
                config,
                round_index=1,
                next_candidate_index=1,
            )[0][0]

            tuner.write_candidate_config(spec, config=config, metadata=metadata)
            text = spec.config_path.read_text(encoding="utf-8")

        self.assertIn("# generated_by=tools/scripts/eval_config_tuner.py", text)
        self.assertIn("# base_config_sha256=", text)
        self.assertIn("# labels_sha256=", text)
        self.assertIn("# objective=", text)
        self.assertIn("name=tuned_candidate_0001", text)
        self.assertIn("opening.disc_difference=0", text)
        self.assertIn("midgame_max_occupied=44", text)

    def test_candidate_generation_is_deterministic_for_same_seed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, extra_args=["--max-candidates", "3"])
            base = tuner.load_eval_config(config.base_config)
            keys = tuner.select_tunable_keys(base, config)

            first = tuner.generate_round_candidates(
                base,
                keys,
                config,
                round_index=1,
                next_candidate_index=1,
            )[0]
            second = tuner.generate_round_candidates(
                base,
                keys,
                config,
                round_index=1,
                next_candidate_index=1,
            )[0]

        self.assertEqual(
            [(spec.changed_key, spec.delta) for spec in first],
            [(spec.changed_key, spec.delta) for spec in second],
        )

    def test_candidate_generation_respects_max_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, extra_args=["--max-candidates", "3"])
            base = tuner.load_eval_config(config.base_config)
            keys = tuner.select_tunable_keys(base, config)

            specs, generated_count, _ = tuner.generate_round_candidates(
                base,
                keys,
                config,
                round_index=1,
                next_candidate_index=1,
            )

        self.assertGreater(generated_count, 3)
        self.assertEqual(len(specs), 3)

    def test_candidate_generation_skips_existing_signatures(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp))
            base = tuner.load_eval_config(config.base_config)
            already_seen = {
                base.signature(),
                base.with_value("opening.mobility", 9).signature(),
            }

            specs, generated_count, _ = tuner.generate_round_candidates(
                base,
                ["opening.mobility"],
                config,
                round_index=1,
                next_candidate_index=1,
                existing_signatures=already_seen,
            )

        self.assertEqual(generated_count, 1)
        self.assertEqual(specs[0].config.values()["opening.mobility"], 7)

    def test_keys_option_limits_tuned_keys(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), extra_args=["--keys", "opening.mobility"])
            base = tuner.load_eval_config(config.base_config)

        self.assertEqual(tuner.select_tunable_keys(base, config), ["opening.mobility"])

    def test_phase_boundary_keys_are_excluded_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp))
            base = tuner.load_eval_config(config.base_config)

        selected = tuner.select_tunable_keys(base, config)
        self.assertNotIn("opening_max_occupied", selected)
        self.assertNotIn("midgame_max_occupied", selected)

    def test_explicit_phase_boundary_requires_flag(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), extra_args=["--keys", "opening_max_occupied"])
            base = tuner.load_eval_config(config.base_config)

            with self.assertRaises(ScriptError) as context:
                tuner.select_tunable_keys(base, config)

        self.assertIn("--include-phase-boundaries", str(context.exception))

    def test_analyzer_command_includes_required_flags(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(temp_path, analyzer=temp_path / "eval_vs_exact")

            command = tuner.analyzer_command(
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

            command = tuner.analyzer_command(
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

        metrics = tuner.parse_analyzer_stdout(stdout)

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

        metrics = tuner.parse_analyzer_stdout(stdout)

        self.assertEqual(metrics.move_rank_records_with_scores, 3)
        self.assertEqual(metrics.move_rank_records_missing_scores, 2)
        self.assertEqual(metrics.move_rank_analyzed, 3)
        self.assertEqual(metrics.move_rank_top_exact_best, 2)
        self.assertEqual(metrics.move_rank_top_non_best, 1)
        self.assertEqual(metrics.move_rank_eval_score_gap_sum, 17)

    def test_objective_ranking_prefers_fewer_wrong_direction_cases(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer = write_fake_analyzer(temp_path)
            config = make_config(
                temp_path,
                analyzer=analyzer,
                extra_args=["--keys", "opening.mobility"],
            )

            exit_code = tuner.run_tuner(config)
            report = (config.out_dir / "tuner_report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("`opening.mobility`: `8` -> `9`", report)
        self.assertIn("| 1 | `candidate_0001` |", report)

    def test_move_rank_analysis_records_metrics_without_changing_objective(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer = write_fake_analyzer(temp_path)
            config = make_config(
                temp_path,
                analyzer=analyzer,
                extra_args=[
                    "--keys",
                    "opening.mobility",
                    "--move-rank-analysis",
                ],
            )

            exit_code = tuner.run_tuner(config)
            report = (config.out_dir / "tuner_report.md").read_text(encoding="utf-8")
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("## Move-Rank Metrics", report)
        self.assertIn("move_rank_analysis: `true`", report)
        self.assertIn("move_rank_analyzed", summary)
        self.assertIn("\t3\t2\t0\t3\t2\t1\t4\t17\t9\t", summary)
        self.assertIn("candidate_0001\tpassed\t5\t5\t5\t0\t0", summary)

    def test_dry_run_writes_planned_commands_and_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                analyzer=write_fake_analyzer(temp_path),
                extra_args=["--dry-run", "--keys", "opening.mobility"],
            )

            exit_code = tuner.run_tuner(config)
            report = (config.out_dir / "tuner_report.md").read_text(encoding="utf-8")
            summary = (config.out_dir / "summary.tsv").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("No strength claim", report)
        self.assertIn("objective_formula", report)
        self.assertIn("status", summary)
        self.assertIn("planned", report)

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
            failing_config = make_config(
                temp_path,
                analyzer=failing,
                extra_args=["--keys", "opening.mobility"],
            )
            allowed_config = make_config(
                temp_path,
                analyzer=failing,
                extra_args=[
                    "--keys",
                    "opening.mobility",
                    "--allow-failures",
                    "--out",
                    str(temp_path / "allowed"),
                ],
            )

            failing_exit = tuner.run_tuner(failing_config)
            allowed_exit = tuner.run_tuner(allowed_config)
            report = (allowed_config.out_dir / "tuner_report.md").read_text(encoding="utf-8")

        self.assertEqual(failing_exit, 1)
        self.assertEqual(allowed_exit, 0)
        self.assertIn("## Failures", report)
        self.assertIn("failed", report)

    def test_report_contains_hashes_and_objective_formula(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            config = make_config(
                temp_path,
                analyzer=write_fake_analyzer(temp_path),
                extra_args=["--keys", "opening.mobility"],
            )

            exit_code = tuner.run_tuner(config)
            report = (config.out_dir / "tuner_report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("No strength claim", report)
        self.assertIn("objective_formula", report)
        self.assertIn("base_config_sha256", report)
        self.assertIn("labels_sha256", report)


if __name__ == "__main__":
    unittest.main()
