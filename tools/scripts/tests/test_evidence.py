from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import evidence  # noqa: E402


def make_config(
    temp_dir: Path,
    *,
    profile: str = "smoke",
    dry_run: bool = True,
    allow_failures: bool = False,
    extra_args: list[str] | None = None,
) -> evidence.EvidenceConfig:
    args = [
        "--profile",
        profile,
        "--source-dir",
        ".",
        "--build-dir",
        str(temp_dir / "build"),
        "--out",
        str(temp_dir / "runs"),
        "--dry-run",
    ]
    if not dry_run:
        args.remove("--dry-run")
    if allow_failures:
        args.append("--allow-failures")
    if extra_args:
        args.extend(extra_args)
    parsed = evidence.parse_args(args)
    return evidence.config_from_args(parsed, invocation=["evidence.py", *args])


def fake_executor(fail_names: set[str]) -> evidence.StepExecutor:
    def run(step: evidence.Step, *, dry_run: bool) -> evidence.StepResult:
        if step.skipped_reason is not None:
            return evidence.StepResult(
                name=step.name,
                group=step.group,
                command=step.command,
                cwd=step.cwd,
                log_path=step.log_path,
                required=step.required,
                status="skipped",
                skipped_reason=step.skipped_reason,
                note=step.note,
            )
        failed = step.name in fail_names
        return evidence.StepResult(
            name=step.name,
            group=step.group,
            command=step.command,
            cwd=step.cwd,
            log_path=step.log_path,
            required=step.required,
            status="failed" if failed else "passed",
            exit_code=7 if failed else 0,
            elapsed_seconds=0.01,
            note=step.note,
        )

    return run


class EvidenceTests(unittest.TestCase):
    def test_profile_parsing_accepts_known_profiles(self) -> None:
        for profile in evidence.PROFILES:
            with self.subTest(profile=profile):
                args = evidence.parse_args(["--profile", profile])
                self.assertEqual(args.profile, profile)

    def test_command_construction_uses_release_build_and_exact_threshold_zero(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), profile="smoke")
            steps = evidence.build_steps(config)

        configure = next(step for step in steps if step.name == "configure")
        search = next(step for step in steps if step.name == "search-smoke")
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", configure.command)
        self.assertIn("--exact-endgame-threshold", search.command)
        self.assertEqual(
            search.command[search.command.index("--exact-endgame-threshold") + 1],
            "0",
        )

    def test_dry_run_writes_report_with_no_strength_claim_and_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), profile="smoke", dry_run=True)

            exit_code = evidence.run_evidence(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: dry run", report)
        self.assertIn("No strength claim", report)
        self.assertIn("```sh\ncmake -S", report)
        self.assertIn("--exact-endgame-threshold 0", report)

    def test_pr_dry_run_renders_skipped_optional_commands(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), profile="pr", dry_run=True)

            exit_code = evidence.run_evidence(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("rule-core-bench", report)
        self.assertIn("Skipped:", report)
        self.assertIn("optional executable not found", report)

    def test_pr_real_run_plans_optional_executables_after_build(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), profile="pr", dry_run=False)
            steps = evidence.build_steps(config)

        rule_core = next(step for step in steps if step.name == "rule-core-bench")
        match = next(step for step in steps if step.name == "match-self-play")
        self.assertIsNone(rule_core.skipped_reason)
        self.assertIn("othello_rule_core_bench", rule_core.command[0])
        self.assertIsNone(match.skipped_reason)
        self.assertIn("othello_match_runner", match.command[0])

    def test_failed_required_command_is_recorded_and_fails_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), profile="smoke", dry_run=False)

            exit_code = evidence.run_evidence(config, executor=fake_executor({"ctest"}))
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 1)
        self.assertIn("Status: completed with failures", report)
        self.assertIn("`ctest`: failed", report)
        self.assertIn("exit 7", report)

    def test_allow_failures_changes_exit_behavior_but_records_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(
                Path(temp),
                profile="smoke",
                dry_run=False,
                allow_failures=True,
            )

            exit_code = evidence.run_evidence(config, executor=fake_executor({"ctest"}))
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("Status: completed with failures", report)
        self.assertIn("`ctest`: failed", report)

    def test_eval_profile_with_configs_emits_eval_config_and_reference_config(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            candidate = temp_path / "candidate.eval"
            reference = temp_path / "reference.eval"
            config = make_config(
                temp_path,
                profile="eval",
                dry_run=True,
                extra_args=[
                    "--eval-configs",
                    str(candidate),
                    "--reference-config",
                    str(reference),
                    "--small-depths",
                    "5",
                    "--small-games",
                    "2",
                ],
            )

            exit_code = evidence.run_evidence(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("--eval-config", report)
        self.assertIn(str(candidate), report)
        self.assertIn("--reference-config", report)
        self.assertIn(str(reference), report)

    def test_report_marks_exact_threshold_zero_as_midgame_eval_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = make_config(Path(temp), profile="eval", dry_run=True)

            exit_code = evidence.run_evidence(config)
            report = (config.out_dir / "report.md").read_text(encoding="utf-8")

        self.assertEqual(exit_code, 0)
        self.assertIn("exact_endgame_threshold used for midgame/eval search evidence: `0`", report)
        self.assertIn("isolates midgame/eval search from exact root solving", report)


if __name__ == "__main__":
    unittest.main()
