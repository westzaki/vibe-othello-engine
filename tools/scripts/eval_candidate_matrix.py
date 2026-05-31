#!/usr/bin/env python3
"""Run a reproducible smoke matrix for .eval evaluator candidates."""

from __future__ import annotations

import argparse
import datetime as dt
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

from common import ScriptError, quote_command, slugify


DEFAULT_BASELINE_CONFIG = Path("data") / "eval" / "current_default.eval"
DEFAULT_SEARCH_DEPTHS = "5,6,7"
DEFAULT_POSITIONS = "smoke"
DEFAULT_REPETITIONS = 1
DEFAULT_EXACT_ENDGAME_THRESHOLD = 0


@dataclass(frozen=True)
class EvalTarget:
    role: str
    config_path: Path
    label: str
    slug: str


@dataclass(frozen=True)
class MatrixConfig:
    build_dir: Path
    labels_path: Path | None
    baseline_config: Path | None
    candidate_configs: tuple[Path, ...]
    out_dir: Path
    search_depths: str
    positions: str
    repetitions: int
    exact_endgame_threshold: int
    dry_run: bool
    allow_failures: bool
    eval_vs_exact: Path
    search_bench: Path
    invocation: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class Metadata:
    timestamp: str
    git_sha: str


@dataclass(frozen=True)
class CommandResult:
    name: str
    command: list[str]
    log_path: Path
    status: str
    exit_code: int | None = None
    output_path: Path | None = None
    skipped_reason: str | None = None
    error: str = ""


@dataclass(frozen=True)
class TargetResult:
    target: EvalTarget
    eval_result: CommandResult | None
    search_result: CommandResult


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


def parse_depth_list(value: str) -> list[int]:
    depths: list[int] = []
    for raw_part in value.split(","):
        part = raw_part.strip()
        if not part:
            raise ScriptError(f"invalid search depth list: {value}")
        try:
            depth = int(part)
        except ValueError as exc:
            raise ScriptError(f"invalid search depth value: {part}") from exc
        if depth <= 0:
            raise ScriptError(f"search depth must be positive: {depth}")
        depths.append(depth)
    if not depths:
        raise ScriptError("search depth list must not be empty")
    return depths


def read_eval_name(path: Path) -> str:
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            body = line.split("#", 1)[0].strip()
            if not body or "=" not in body:
                continue
            key, value = (part.strip() for part in body.split("=", 1))
            if key == "name" and value:
                return value
    except OSError:
        pass
    return path.stem or str(path)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run eval-vs-exact and search-bench smoke checks for .eval candidates."
    )
    parser.add_argument("--build-dir", default="build", help="directory containing C++ tools")
    parser.add_argument("--labels", help="optional exact-label JSONL for othello_eval_vs_exact")
    parser.add_argument(
        "--candidates",
        nargs="+",
        required=True,
        help="one or more candidate .eval config paths",
    )
    parser.add_argument(
        "--baseline-config",
        help="baseline .eval config; defaults to data/eval/current_default.eval when present",
    )
    parser.add_argument("--out", required=True, help="output directory, normally under runs/")
    parser.add_argument("--search-depths", default=DEFAULT_SEARCH_DEPTHS)
    parser.add_argument("--positions", default=DEFAULT_POSITIONS)
    parser.add_argument("--repetitions", type=parse_positive_int, default=DEFAULT_REPETITIONS)
    parser.add_argument(
        "--exact-endgame-threshold",
        type=parse_non_negative_int,
        default=DEFAULT_EXACT_ENDGAME_THRESHOLD,
    )
    parser.add_argument("--dry-run", action="store_true", help="write commands/report without running C++ tools")
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="return zero while recording command failures",
    )
    parser.add_argument("--eval-vs-exact", help="override othello_eval_vs_exact path")
    parser.add_argument("--search-bench", help="override othello_search_bench path")
    return parser.parse_args(argv)


def default_baseline_config(explicit: str | None) -> Path | None:
    if explicit is not None:
        return Path(explicit)
    if DEFAULT_BASELINE_CONFIG.exists():
        return DEFAULT_BASELINE_CONFIG
    return None


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> MatrixConfig:
    parse_depth_list(args.search_depths)
    build_dir = Path(args.build_dir)

    candidate_configs = tuple(Path(path) for path in args.candidates)
    resolved: set[Path] = set()
    duplicate_paths: list[Path] = []
    for path in candidate_configs:
        resolved_path = path.expanduser().resolve(strict=False)
        if resolved_path in resolved:
            duplicate_paths.append(path)
        resolved.add(resolved_path)
    if duplicate_paths:
        raise ScriptError("duplicate candidate paths: " + ", ".join(str(path) for path in duplicate_paths))

    return MatrixConfig(
        build_dir=build_dir,
        labels_path=Path(args.labels) if args.labels else None,
        baseline_config=default_baseline_config(args.baseline_config),
        candidate_configs=candidate_configs,
        out_dir=Path(args.out),
        search_depths=args.search_depths,
        positions=args.positions,
        repetitions=args.repetitions,
        exact_endgame_threshold=args.exact_endgame_threshold,
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        eval_vs_exact=Path(args.eval_vs_exact) if args.eval_vs_exact else build_dir / "othello_eval_vs_exact",
        search_bench=Path(args.search_bench) if args.search_bench else build_dir / "othello_search_bench",
        invocation=invocation or [],
    )


def collect_metadata() -> Metadata:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    git_sha = completed.stdout.strip() if completed.returncode == 0 else "unknown"
    return Metadata(
        timestamp=dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        git_sha=git_sha or "unknown",
    )


def make_targets(config: MatrixConfig) -> list[EvalTarget]:
    targets: list[EvalTarget] = []
    used_slugs: set[str] = set()

    def add_target(role: str, path: Path) -> None:
        label = read_eval_name(path)
        base_slug = slugify(f"{role}-{label}", fallback=role)
        slug = base_slug
        suffix = 2
        while slug in used_slugs:
            slug = f"{base_slug}-{suffix}"
            suffix += 1
        used_slugs.add(slug)
        targets.append(EvalTarget(role=role, config_path=path, label=label, slug=slug))

    if config.baseline_config is not None:
        add_target("baseline", config.baseline_config)
    for path in config.candidate_configs:
        add_target("candidate", path)
    return targets


def eval_vs_exact_command(config: MatrixConfig, target: EvalTarget, report_path: Path) -> list[str]:
    if config.labels_path is None:
        raise AssertionError("eval_vs_exact_command requires labels")
    return [
        str(config.eval_vs_exact),
        "--labels",
        str(config.labels_path),
        "--output",
        str(report_path),
        "--eval-config",
        str(target.config_path),
    ]


def search_bench_command(config: MatrixConfig, target: EvalTarget) -> list[str]:
    return [
        str(config.search_bench),
        "--eval-config",
        str(target.config_path),
        "--mode",
        "iterative",
        "--depths",
        config.search_depths,
        "--positions",
        config.positions,
        "--repetitions",
        str(config.repetitions),
        "--tt",
        "on",
        "--pvs",
        "on",
        "--aspiration",
        "on",
        "--exact-endgame-threshold",
        str(config.exact_endgame_threshold),
    ]


def write_command_log(
    path: Path,
    *,
    command: list[str],
    status: str,
    exit_code: int | None,
    stdout: str,
    stderr: str,
    skipped_reason: str | None = None,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"command: {quote_command(command)}",
        f"status: {status}",
        f"exit_code: {exit_code if exit_code is not None else 'n/a'}",
    ]
    if skipped_reason is not None:
        lines.append(f"skipped: {skipped_reason}")
    lines.extend(("", "stdout:", stdout, "stderr:", stderr))
    path.write_text("\n".join(lines), encoding="utf-8")


def run_command_result(
    *,
    name: str,
    command: list[str],
    log_path: Path,
    output_path: Path | None,
    dry_run: bool,
) -> CommandResult:
    if dry_run:
        write_command_log(
            log_path,
            command=command,
            status="planned",
            exit_code=None,
            stdout="dry run\n",
            stderr="",
        )
        return CommandResult(
            name=name,
            command=command,
            log_path=log_path,
            output_path=output_path,
            status="planned",
        )

    try:
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
    except OSError as exc:
        write_command_log(
            log_path,
            command=command,
            status="failed",
            exit_code=None,
            stdout="",
            stderr=str(exc),
        )
        return CommandResult(
            name=name,
            command=command,
            log_path=log_path,
            output_path=output_path,
            status="failed",
            error=str(exc),
        )

    status = "passed" if completed.returncode == 0 else "failed"
    write_command_log(
        log_path,
        command=command,
        status=status,
        exit_code=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
    )
    return CommandResult(
        name=name,
        command=command,
        log_path=log_path,
        output_path=output_path,
        status=status,
        exit_code=completed.returncode,
        error=completed.stderr.strip() if completed.returncode != 0 else "",
    )


def skipped_eval_result(config: MatrixConfig, target: EvalTarget) -> CommandResult:
    command = [
        str(config.eval_vs_exact),
        "--labels",
        "<labels>",
        "--output",
        str(config.out_dir / "eval-vs-exact" / f"{target.slug}.md"),
        "--eval-config",
        str(target.config_path),
    ]
    log_path = config.out_dir / "logs" / target.slug / "eval-vs-exact.log"
    reason = "--labels was not provided"
    write_command_log(
        log_path,
        command=command,
        status="skipped",
        exit_code=None,
        stdout="",
        stderr="",
        skipped_reason=reason,
    )
    return CommandResult(
        name="eval-vs-exact",
        command=command,
        log_path=log_path,
        status="skipped",
        skipped_reason=reason,
    )


def run_target(config: MatrixConfig, target: EvalTarget) -> TargetResult:
    target_logs = config.out_dir / "logs" / target.slug

    eval_result: CommandResult | None
    if config.labels_path is None:
        eval_result = skipped_eval_result(config, target)
    else:
        eval_report = config.out_dir / "eval-vs-exact" / f"{target.slug}.md"
        eval_result = run_command_result(
            name="eval-vs-exact",
            command=eval_vs_exact_command(config, target, eval_report),
            log_path=target_logs / "eval-vs-exact.log",
            output_path=eval_report,
            dry_run=config.dry_run,
        )

    search_result = run_command_result(
        name="search-bench",
        command=search_bench_command(config, target),
        log_path=target_logs / "search-bench.log",
        output_path=None,
        dry_run=config.dry_run,
    )
    return TargetResult(target=target, eval_result=eval_result, search_result=search_result)


def matrix_status(config: MatrixConfig, results: list[TargetResult]) -> str:
    if config.dry_run:
        return "dry run"
    if any(command_failed(result) for result in results):
        return "completed with failures"
    return "completed"


def command_failed(result: TargetResult) -> bool:
    commands = [result.search_result]
    if result.eval_result is not None:
        commands.append(result.eval_result)
    return any(command.status == "failed" for command in commands)


def _md(value: object) -> str:
    return str(value).replace("|", "\\|")


def render_summary_table(results: list[TargetResult]) -> str:
    lines = [
        "| role | name | config path | eval-vs-exact status | eval-vs-exact report | search bench status | search bench log | failure |",
        "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |",
    ]
    for result in results:
        eval_result = result.eval_result
        eval_status = eval_result.status if eval_result is not None else "skipped"
        eval_report = eval_result.output_path if eval_result is not None and eval_result.output_path else "n/a"
        failed = "yes" if command_failed(result) else "no"
        lines.append(
            "| "
            + " | ".join(
                (
                    _md(result.target.role),
                    _md(result.target.label),
                    f"`{_md(result.target.config_path)}`",
                    _md(eval_status),
                    f"`{_md(eval_report)}`" if eval_report != "n/a" else "n/a",
                    _md(result.search_result.status),
                    f"`{_md(result.search_result.log_path)}`",
                    failed,
                )
            )
            + " |"
        )
    return "\n".join(lines)


def render_report(config: MatrixConfig, metadata: Metadata, results: list[TargetResult]) -> str:
    lines: list[str] = [
        "# Evaluation Candidate Matrix",
        "",
        f"Status: {matrix_status(config, results)}",
        "",
        "No strength claim. No automatic default promotion.",
        "",
        "Search/eval numbers are smoke evidence unless broader matches are run.",
        "Raw outputs belong under `runs/`; this workflow writes command logs and reports under the requested output directory.",
        "",
        "## Metadata",
        "",
        f"- generated_at: `{metadata.timestamp}`",
        f"- git_sha: `{metadata.git_sha}`",
        f"- command: `{quote_command(config.invocation) if config.invocation else 'unknown'}`",
        f"- build_dir: `{config.build_dir}`",
        f"- labels: `{config.labels_path if config.labels_path is not None else 'not provided'}`",
        f"- baseline_config: `{config.baseline_config if config.baseline_config is not None else 'not found/not provided'}`",
        f"- candidate_configs: `{', '.join(str(path) for path in config.candidate_configs)}`",
        f"- search_depths: `{config.search_depths}`",
        f"- positions: `{config.positions}`",
        f"- repetitions: `{config.repetitions}`",
        f"- exact_endgame_threshold: `{config.exact_endgame_threshold}`",
        f"- allow_failures: `{str(config.allow_failures).lower()}`",
        "",
        "## Summary",
        "",
        render_summary_table(results),
        "",
        "## Exact Commands",
        "",
    ]

    for result in results:
        lines.extend(
            [
                f"### {result.target.role}: {result.target.label}",
                "",
                f"- config: `{result.target.config_path}`",
                f"- logs: `{config.out_dir / 'logs' / result.target.slug}`",
                "",
            ]
        )
        for command_result in (result.eval_result, result.search_result):
            if command_result is None:
                continue
            lines.extend(
                [
                    f"#### {command_result.name}",
                    "",
                    f"- status: `{command_result.status}`",
                    f"- log: `{command_result.log_path}`",
                ]
            )
            if command_result.output_path is not None:
                lines.append(f"- output: `{command_result.output_path}`")
            if command_result.exit_code is not None:
                lines.append(f"- exit_code: `{command_result.exit_code}`")
            if command_result.skipped_reason is not None:
                lines.append(f"- skipped: {command_result.skipped_reason}")
            lines.extend(["", "```sh", quote_command(command_result.command), "```", ""])

    lines.extend(
        [
            "## Caveats",
            "",
            "- This is an orchestration smoke workflow, not a tuner.",
            "- It does not promote any candidate automatically.",
            "- Eval-vs-exact compares heuristic scores with exact final-disc labels and is not calibrated as disc-margin error.",
            "- Search bench output is fixed-position smoke evidence; use broader deterministic matches or base/head comparison before making strength claims.",
            "- Keep generated reports, logs, and analyzer output under `runs/`; do not commit raw run output.",
        ]
    )
    return "\n".join(lines) + "\n"


def run_matrix(config: MatrixConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    metadata = collect_metadata()
    results = [run_target(config, target) for target in make_targets(config)]

    report_path = config.out_dir / "report.md"
    report_path.write_text(render_report(config, metadata, results), encoding="utf-8")
    print(f"report: {report_path}", flush=True)

    if any(command_failed(result) for result in results) and not config.allow_failures:
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_matrix(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
