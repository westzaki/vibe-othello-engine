#!/usr/bin/env python3
"""Sample positions, dump exact labels, and optionally run eval-vs-exact analysis."""

from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

from common import ScriptError, quote_command


DEFAULT_SEED = 20260531
DEFAULT_COUNT = 20
DEFAULT_TARGET_EMPTIES = "8,10,12"
DEFAULT_MAX_EMPTIES = 14


@dataclass(frozen=True)
class WorkflowConfig:
    build_dir: Path
    out_dir: Path
    count: int
    target_empties: str
    seed: int
    max_empties: int
    eval_config: Path | None
    analyze: bool
    skip_sampling: bool
    positions_path: Path
    labels_path: Path
    report_path: Path
    dry_run: bool
    allow_failures: bool
    position_sampler: Path
    exact_label_dump: Path
    eval_vs_exact: Path
    invocation: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class Metadata:
    generated_at: str
    git_sha: str
    invocation: list[str]
    build_dir: Path
    seed: int
    target_empties: str
    count: int
    max_empties: int


@dataclass(frozen=True)
class WorkflowStep:
    name: str
    command: list[str]
    log_path: Path
    required: bool = True
    skipped_reason: str | None = None


@dataclass(frozen=True)
class StepResult:
    name: str
    command: list[str]
    log_path: Path
    required: bool
    status: str
    exit_code: int | None = None
    skipped_reason: str | None = None
    output: str = ""


def parse_positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a positive integer") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def parse_non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a non-negative integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be a non-negative integer")
    return parsed


def validate_target_empties(value: str) -> str:
    parts = value.split(",")
    if not parts or any(part == "" for part in parts):
        raise argparse.ArgumentTypeError("--target-empties must not contain empty segments")
    for part in parts:
        try:
            empties = int(part)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(
                "--target-empties values must be integers in [0, 64]"
            ) from exc
        if empties < 0 or empties > 64:
            raise argparse.ArgumentTypeError(
                "--target-empties values must be integers in [0, 64]"
            )
    return value


def default_out_dir() -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("runs") / "exact-label-workflow" / timestamp


def default_tool(build_dir: Path, name: str) -> Path:
    return build_dir / name


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a reproducible exact-label sampling workflow."
    )
    parser.add_argument("--build-dir", default="build", help="directory containing C++ tools")
    parser.add_argument(
        "--out",
        default=None,
        help="workflow output directory (default: runs/exact-label-workflow/<timestamp>)",
    )
    parser.add_argument("--count", type=parse_positive_int, default=DEFAULT_COUNT)
    parser.add_argument(
        "--target-empties",
        type=validate_target_empties,
        default=DEFAULT_TARGET_EMPTIES,
        help="comma-separated exact empty counts to sample",
    )
    parser.add_argument("--seed", type=parse_non_negative_int, default=DEFAULT_SEED)
    parser.add_argument(
        "--max-empties",
        type=parse_non_negative_int,
        default=DEFAULT_MAX_EMPTIES,
        help="exact-label dump safety cap",
    )
    parser.add_argument("--eval-config", help="analyze with a fully expanded .eval config")
    parser.add_argument("--analyze", action="store_true", help="run othello_eval_vs_exact")
    parser.add_argument(
        "--skip-sampling",
        action="store_true",
        help="reuse --positions instead of running othello_position_sampler",
    )
    parser.add_argument("--positions", help="input/output positions path")
    parser.add_argument("--labels", help="output exact-label JSONL path")
    parser.add_argument("--report", help="output eval-vs-exact Markdown path")
    parser.add_argument("--dry-run", action="store_true", help="write planned commands only")
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="return zero while recording failed required commands",
    )
    parser.add_argument("--position-sampler", help="override othello_position_sampler path")
    parser.add_argument("--exact-label-dump", help="override othello_exact_label_dump path")
    parser.add_argument("--eval-vs-exact", help="override othello_eval_vs_exact path")
    return parser.parse_args(argv)


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> WorkflowConfig:
    if args.max_empties > 64:
        raise ScriptError("--max-empties must be in [0, 64]")

    build_dir = Path(args.build_dir)
    out_dir = Path(args.out) if args.out else default_out_dir()
    positions_path = Path(args.positions) if args.positions else out_dir / "positions.txt"
    if args.skip_sampling and not args.positions:
        raise ScriptError("--skip-sampling requires --positions")

    labels_path = Path(args.labels) if args.labels else out_dir / "labels.jsonl"
    report_path = Path(args.report) if args.report else out_dir / "eval-vs-exact.md"

    return WorkflowConfig(
        build_dir=build_dir,
        out_dir=out_dir,
        count=args.count,
        target_empties=args.target_empties,
        seed=args.seed,
        max_empties=args.max_empties,
        eval_config=Path(args.eval_config) if args.eval_config else None,
        analyze=args.analyze,
        skip_sampling=args.skip_sampling,
        positions_path=positions_path,
        labels_path=labels_path,
        report_path=report_path,
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        position_sampler=Path(args.position_sampler)
        if args.position_sampler
        else default_tool(build_dir, "othello_position_sampler"),
        exact_label_dump=Path(args.exact_label_dump)
        if args.exact_label_dump
        else default_tool(build_dir, "othello_exact_label_dump"),
        eval_vs_exact=Path(args.eval_vs_exact)
        if args.eval_vs_exact
        else default_tool(build_dir, "othello_eval_vs_exact"),
        invocation=invocation or [],
    )


def collect_metadata(config: WorkflowConfig) -> Metadata:
    git_sha = "unknown"
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode == 0:
        git_sha = completed.stdout.strip() or "unknown"

    return Metadata(
        generated_at=dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        git_sha=git_sha,
        invocation=config.invocation,
        build_dir=config.build_dir,
        seed=config.seed,
        target_empties=config.target_empties,
        count=config.count,
        max_empties=config.max_empties,
    )


def sampler_command(config: WorkflowConfig) -> list[str]:
    return [
        str(config.position_sampler),
        "--output",
        str(config.positions_path),
        "--count",
        str(config.count),
        "--target-empties",
        config.target_empties,
        "--seed",
        str(config.seed),
    ]


def exact_label_command(config: WorkflowConfig) -> list[str]:
    return [
        str(config.exact_label_dump),
        "--input",
        str(config.positions_path),
        "--output",
        str(config.labels_path),
        "--max-empties",
        str(config.max_empties),
    ]


def analyzer_command(config: WorkflowConfig) -> list[str]:
    command = [
        str(config.eval_vs_exact),
        "--labels",
        str(config.labels_path),
        "--output",
        str(config.report_path),
    ]
    if config.eval_config:
        command.extend(["--eval-config", str(config.eval_config)])
    return command


def build_steps(config: WorkflowConfig) -> list[WorkflowStep]:
    logs_dir = config.out_dir / "logs"
    steps: list[WorkflowStep] = []
    if config.skip_sampling:
        steps.append(
            WorkflowStep(
                name="sample-positions",
                command=sampler_command(config),
                log_path=logs_dir / "sampler.log",
                skipped_reason="--skip-sampling was set",
            )
        )
    else:
        steps.append(
            WorkflowStep(
                name="sample-positions",
                command=sampler_command(config),
                log_path=logs_dir / "sampler.log",
            )
        )

    steps.append(
        WorkflowStep(
            name="dump-exact-labels",
            command=exact_label_command(config),
            log_path=logs_dir / "exact-label-dump.log",
        )
    )
    if config.analyze:
        steps.append(
            WorkflowStep(
                name="analyze-eval-vs-exact",
                command=analyzer_command(config),
                log_path=logs_dir / "eval-vs-exact.log",
            )
        )
    return steps


def write_log(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run_step(step: WorkflowStep, *, dry_run: bool) -> StepResult:
    if step.skipped_reason is not None:
        write_log(
            step.log_path,
            f"skipped: {step.skipped_reason}\ncommand: {quote_command(step.command)}\n",
        )
        return StepResult(
            name=step.name,
            command=step.command,
            log_path=step.log_path,
            required=step.required,
            status="skipped",
            skipped_reason=step.skipped_reason,
        )

    if dry_run:
        text = f"dry run\ncommand: {quote_command(step.command)}\n"
        write_log(step.log_path, text)
        return StepResult(
            name=step.name,
            command=step.command,
            log_path=step.log_path,
            required=step.required,
            status="planned",
            output=text,
        )

    step.log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        completed = subprocess.run(step.command, check=False, capture_output=True, text=True)
    except OSError as exc:
        output = f"command: {quote_command(step.command)}\nfailed to start: {exc}\n"
        write_log(step.log_path, output)
        return StepResult(
            name=step.name,
            command=step.command,
            log_path=step.log_path,
            required=step.required,
            status="failed",
            output=output,
        )
    output = (
        f"command: {quote_command(step.command)}\n"
        f"exit_code: {completed.returncode}\n\n"
        f"stdout:\n{completed.stdout}\n"
        f"stderr:\n{completed.stderr}\n"
    )
    write_log(step.log_path, output)
    return StepResult(
        name=step.name,
        command=step.command,
        log_path=step.log_path,
        required=step.required,
        status="passed" if completed.returncode == 0 else "failed",
        exit_code=completed.returncode,
        output=output,
    )


def extract_count(results: list[StepResult], key: str) -> str:
    pattern = re.compile(rf"{re.escape(key)}=(\d+)")
    for result in results:
        match = pattern.search(result.output)
        if match:
            return match.group(1)
    return "unknown"


def workflow_status(results: list[StepResult], *, dry_run: bool) -> str:
    if dry_run:
        return "dry run"
    if any(result.status == "failed" for result in results):
        return "completed with failures"
    return "completed"


def render_report(config: WorkflowConfig, metadata: Metadata, results: list[StepResult]) -> str:
    lines: list[str] = [
        "# Exact Label Sampling Workflow",
        "",
        f"Status: {workflow_status(results, dry_run=config.dry_run)}",
        "",
        "No strength claim. No default promotion recommendation.",
        "",
        "## Metadata",
        "",
        f"- generated_at: `{metadata.generated_at}`",
        f"- git_sha: `{metadata.git_sha}`",
        f"- command: `{quote_command(metadata.invocation) if metadata.invocation else 'unknown'}`",
        f"- build_dir: `{metadata.build_dir}`",
        f"- seed: `{metadata.seed}`",
        f"- target_empties: `{metadata.target_empties}`",
        f"- requested_count: `{metadata.count}`",
        f"- max_empties: `{metadata.max_empties}`",
        "",
        "## Outputs",
        "",
        f"- positions: `{config.positions_path}`",
        f"- labels: `{config.labels_path}`",
    ]
    if config.analyze:
        lines.append(f"- eval_vs_exact_report: `{config.report_path}`")
    lines.extend(
        [
            f"- logs: `{config.out_dir / 'logs'}`",
            "",
            "## Counts",
            "",
            f"- sampled: `{extract_count(results, 'sampled')}`",
            f"- labeled: `{extract_count(results, 'labeled')}`",
        ]
    )
    if config.analyze:
        lines.append(f"- analyzed: `{extract_count(results, 'analyzed')}`")

    lines.extend(["", "## Exact Commands", ""])
    for result in results:
        lines.extend(
            [
                f"### {result.name}",
                "",
                f"- status: `{result.status}`",
                f"- log: `{result.log_path}`",
            ]
        )
        if result.exit_code is not None:
            lines.append(f"- exit_code: `{result.exit_code}`")
        if result.skipped_reason is not None:
            lines.append(f"- skipped: {result.skipped_reason}")
        lines.extend(["", "```sh", quote_command(result.command), "```", ""])

    lines.extend(
        [
            "## Caveats",
            "",
            "- Random playout samples are reproducible smoke/teacher-data inputs, not a representative Othello training distribution.",
            "- Exact labels are final disc margins from the side-to-move perspective.",
            "- Eval-vs-exact scores, when analyzed, are heuristic units and are not calibrated as disc margins.",
            "- Raw workflow outputs belong under `runs/` and should not be committed.",
            "- Durable summaries should record command, source SHA, input labels, evaluator/config, and caveats.",
        ]
    )
    return "\n".join(lines) + "\n"


def run_workflow(config: WorkflowConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    metadata = collect_metadata(config)
    steps = build_steps(config)
    results: list[StepResult] = []
    failed = False

    for step in steps:
        if failed:
            skipped = WorkflowStep(
                name=step.name,
                command=step.command,
                log_path=step.log_path,
                required=step.required,
                skipped_reason="previous required step failed",
            )
            results.append(run_step(skipped, dry_run=False))
            continue

        result = run_step(step, dry_run=config.dry_run)
        results.append(result)
        if result.status == "failed" and result.required:
            failed = True

    workflow_path = config.out_dir / "workflow.md"
    workflow_path.write_text(render_report(config, metadata, results), encoding="utf-8")

    if failed and not config.allow_failures:
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_workflow(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
