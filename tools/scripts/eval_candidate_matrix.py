#!/usr/bin/env python3
"""Run a reproducible smoke matrix for .eval evaluator candidates."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any

from common import ScriptError, quote_command, slugify


DEFAULT_BASELINE_CONFIG = Path("data") / "eval" / "current_default.eval"
DEFAULT_SEARCH_DEPTHS = "5,6,7"
DEFAULT_POSITIONS = "smoke"
DEFAULT_REPETITIONS = 1
DEFAULT_EXACT_ENDGAME_THRESHOLD = 0


@dataclass(frozen=True)
class ConfigFingerprint:
    path: Path
    label: str
    sha256: str
    warning: str = ""
    error: str = ""


@dataclass(frozen=True)
class EvalTarget:
    role: str
    config_path: Path
    label: str
    slug: str
    fingerprint: ConfigFingerprint


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
    stdout_text: str = field(default="", repr=False)


@dataclass(frozen=True)
class SearchMetrics:
    aggregate_rows: int
    position_rows: int
    result_checksum: str
    work_checksum: str
    nodes: int | None
    elapsed_ms: float | None
    score_kind: str
    used_exact_endgame: str
    exact_root_positions: int | None
    exact_root_searches: int | None
    best_move: str
    score: str
    principal_variation: str


@dataclass(frozen=True)
class BaselineDelta:
    comparable: bool
    reason: str
    result_checksum_changed: str
    work_checksum_changed: str
    nodes_delta: int | None
    nodes_pct: float | None
    elapsed_ms_delta: float | None
    elapsed_ms_pct: float | None


@dataclass(frozen=True)
class TargetResult:
    target: EvalTarget
    eval_result: CommandResult | None
    search_result: CommandResult
    search_metrics: SearchMetrics | None = None
    search_parse_error: str = ""


class SearchJsonlError(ValueError):
    pass


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


def parse_eval_name(text: str, fallback: str) -> str:
    for line in text.splitlines():
        body = line.split("#", 1)[0].strip()
        if not body or "=" not in body:
            continue
        key, value = (part.strip() for part in body.split("=", 1))
        if key == "name" and value:
            return value
    return fallback


def fingerprint_eval_config(path: Path, *, dry_run: bool) -> ConfigFingerprint:
    fallback = path.stem or str(path)
    try:
        data = path.read_bytes()
    except OSError as exc:
        message = f"unable to read eval config: {exc}"
        return ConfigFingerprint(
            path=path,
            label=fallback,
            sha256="n/a",
            warning=message if dry_run else "",
            error="" if dry_run else message,
        )
    text = data.decode("utf-8", errors="replace")
    return ConfigFingerprint(
        path=path,
        label=parse_eval_name(text, fallback),
        sha256=hashlib.sha256(data).hexdigest(),
    )


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
        help="return zero while recording command and parse failures",
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
        fingerprint = fingerprint_eval_config(path, dry_run=config.dry_run)
        base_slug = slugify(f"{role}-{fingerprint.label}", fallback=role)
        slug = base_slug
        suffix = 2
        while slug in used_slugs:
            slug = f"{base_slug}-{suffix}"
            suffix += 1
        used_slugs.add(slug)
        targets.append(
            EvalTarget(
                role=role,
                config_path=path,
                label=fingerprint.label,
                slug=slug,
                fingerprint=fingerprint,
            )
        )

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
        "--format",
        "jsonl",
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
    notes: list[str] | None = None,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"command: {quote_command(command)}",
        f"status: {status}",
        f"exit_code: {exit_code if exit_code is not None else 'n/a'}",
    ]
    if skipped_reason is not None:
        lines.append(f"skipped: {skipped_reason}")
    for note in notes or []:
        lines.append(note)
    lines.extend(("", "stdout:", stdout, "stderr:", stderr))
    path.write_text("\n".join(lines), encoding="utf-8")


def append_command_log_note(path: Path, note: str) -> None:
    with path.open("a", encoding="utf-8") as handle:
        handle.write(f"\n{note}\n")


def run_command_result(
    *,
    name: str,
    command: list[str],
    log_path: Path,
    output_path: Path | None,
    dry_run: bool,
    stdout_path: Path | None = None,
) -> CommandResult:
    if dry_run:
        write_command_log(
            log_path,
            command=command,
            status="planned",
            exit_code=None,
            stdout="dry run\n",
            stderr="",
            notes=[f"planned_output: {stdout_path or output_path}"] if stdout_path or output_path else None,
        )
        return CommandResult(
            name=name,
            command=command,
            log_path=log_path,
            output_path=stdout_path or output_path,
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
            output_path=stdout_path or output_path,
            status="failed",
            error=str(exc),
        )

    if stdout_path is not None:
        stdout_path.parent.mkdir(parents=True, exist_ok=True)
        stdout_path.write_text(completed.stdout, encoding="utf-8")

    status = "passed" if completed.returncode == 0 else "failed"
    write_command_log(
        log_path,
        command=command,
        status=status,
        exit_code=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
        notes=[f"stdout_jsonl: {stdout_path}"] if stdout_path is not None else None,
    )
    return CommandResult(
        name=name,
        command=command,
        log_path=log_path,
        output_path=stdout_path or output_path,
        status=status,
        exit_code=completed.returncode,
        error=completed.stderr.strip() if completed.returncode != 0 else "",
        stdout_text=completed.stdout,
    )


def failed_command_result(
    *,
    name: str,
    command: list[str],
    log_path: Path,
    output_path: Path | None,
    error: str,
) -> CommandResult:
    write_command_log(
        log_path,
        command=command,
        status="failed",
        exit_code=None,
        stdout="",
        stderr=error,
    )
    return CommandResult(
        name=name,
        command=command,
        log_path=log_path,
        output_path=output_path,
        status="failed",
        error=error,
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


def _jsonl_object_rows(text: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            parsed = json.loads(line)
        except json.JSONDecodeError as exc:
            raise SearchJsonlError(f"malformed JSONL at line {line_number}: {exc.msg}") from exc
        if not isinstance(parsed, dict):
            raise SearchJsonlError(f"malformed JSONL at line {line_number}: expected object row")
        rows.append(parsed)
    if not rows:
        raise SearchJsonlError("search bench JSONL was empty")
    return rows


def _row_key(row: dict[str, Any]) -> str:
    mode = row.get("mode", "unknown")
    depth = row.get("depth", "unknown")
    return f"{mode}:d{depth}"


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _sum_numeric(rows: list[dict[str, Any]], field_name: str) -> int | float | None:
    values = [row[field_name] for row in rows if _is_number(row.get(field_name))]
    if not values:
        return None
    return sum(values)


def _sum_int(rows: list[dict[str, Any]], field_name: str) -> int | None:
    value = _sum_numeric(rows, field_name)
    return int(value) if value is not None else None


def _sum_float(rows: list[dict[str, Any]], field_name: str) -> float | None:
    value = _sum_numeric(rows, field_name)
    return float(value) if value is not None else None


def _unique_values(rows: list[dict[str, Any]], field_name: str) -> list[Any]:
    values: list[Any] = []
    for row in rows:
        if field_name not in row:
            continue
        value = row[field_name]
        if value not in values:
            values.append(value)
    return values


def _format_value(value: Any) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, list):
        return " ".join(_format_value(item) for item in value) or "n/a"
    return str(value)


def _keyed_values(rows: list[dict[str, Any]], field_name: str) -> str:
    values: list[str] = []
    for row in rows:
        if field_name in row:
            values.append(f"{_row_key(row)}={_format_value(row[field_name])}")
    return "; ".join(values) if values else "n/a"


def _bool_summary(rows: list[dict[str, Any]], field_name: str) -> str:
    values = [row[field_name] for row in rows if isinstance(row.get(field_name), bool)]
    if not values:
        return "n/a"
    if all(values):
        return "true"
    if not any(values):
        return "false"
    return "mixed"


def parse_search_metrics(text: str) -> SearchMetrics:
    rows = _jsonl_object_rows(text)
    aggregate_rows = [row for row in rows if row.get("row") == "aggregate"]
    position_rows = [row for row in rows if row.get("row") == "position"]
    if not aggregate_rows:
        raise SearchJsonlError("search bench JSONL contained no aggregate rows")

    first = aggregate_rows[0]
    score_kind_values = [_format_value(value) for value in _unique_values(aggregate_rows, "score_kind")]

    return SearchMetrics(
        aggregate_rows=len(aggregate_rows),
        position_rows=len(position_rows),
        result_checksum=_keyed_values(aggregate_rows, "result_checksum"),
        work_checksum=_keyed_values(aggregate_rows, "work_checksum"),
        nodes=_sum_int(aggregate_rows, "nodes"),
        elapsed_ms=_sum_float(aggregate_rows, "elapsed_ms"),
        score_kind=", ".join(score_kind_values) if score_kind_values else "n/a",
        used_exact_endgame=_bool_summary(aggregate_rows, "used_exact_endgame"),
        exact_root_positions=_sum_int(aggregate_rows, "exact_root_positions"),
        exact_root_searches=_sum_int(aggregate_rows, "exact_root_searches"),
        best_move=_format_value(first.get("best_move")),
        score=_format_value(first.get("score")),
        principal_variation=_format_value(first.get("principal_variation")),
    )


def run_target(config: MatrixConfig, target: EvalTarget) -> TargetResult:
    target_logs = config.out_dir / "logs" / target.slug
    search_jsonl = config.out_dir / "search-bench" / f"{target.slug}.jsonl"

    eval_result: CommandResult | None
    if config.labels_path is None:
        eval_result = skipped_eval_result(config, target)
    else:
        eval_report = config.out_dir / "eval-vs-exact" / f"{target.slug}.md"
        eval_command = eval_vs_exact_command(config, target, eval_report)
        if target.fingerprint.error:
            eval_result = failed_command_result(
                name="eval-vs-exact",
                command=eval_command,
                log_path=target_logs / "eval-vs-exact.log",
                output_path=eval_report,
                error=target.fingerprint.error,
            )
        else:
            eval_result = run_command_result(
                name="eval-vs-exact",
                command=eval_command,
                log_path=target_logs / "eval-vs-exact.log",
                output_path=eval_report,
                dry_run=config.dry_run,
            )

    search_command = search_bench_command(config, target)
    if target.fingerprint.error:
        search_result = failed_command_result(
            name="search-bench",
            command=search_command,
            log_path=target_logs / "search-bench.log",
            output_path=search_jsonl,
            error=target.fingerprint.error,
        )
        return TargetResult(target=target, eval_result=eval_result, search_result=search_result)

    search_result = run_command_result(
        name="search-bench",
        command=search_command,
        log_path=target_logs / "search-bench.log",
        output_path=None,
        dry_run=config.dry_run,
        stdout_path=search_jsonl,
    )

    if search_result.status != "passed":
        return TargetResult(target=target, eval_result=eval_result, search_result=search_result)

    try:
        metrics = parse_search_metrics(search_result.stdout_text)
    except SearchJsonlError as exc:
        parse_error = str(exc)
        append_command_log_note(search_result.log_path, f"parse_error: {parse_error}")
        return TargetResult(
            target=target,
            eval_result=eval_result,
            search_result=replace(search_result, status="failed", error=parse_error),
            search_parse_error=parse_error,
        )
    return TargetResult(
        target=target,
        eval_result=eval_result,
        search_result=search_result,
        search_metrics=metrics,
    )


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


def find_baseline_result(results: list[TargetResult]) -> TargetResult | None:
    for result in results:
        if result.target.role == "baseline":
            return result
    return None


def compare_to_baseline(baseline: TargetResult | None, result: TargetResult) -> BaselineDelta:
    if result.target.role == "baseline":
        return BaselineDelta(False, "baseline row", "n/a", "n/a", None, None, None, None)
    if baseline is None:
        return BaselineDelta(False, "no baseline configured", "n/a", "n/a", None, None, None, None)
    if baseline.search_metrics is None:
        return BaselineDelta(False, "baseline search metrics unavailable", "n/a", "n/a", None, None, None, None)
    if result.search_metrics is None:
        return BaselineDelta(False, "candidate search metrics unavailable", "n/a", "n/a", None, None, None, None)

    base = baseline.search_metrics
    candidate = result.search_metrics
    nodes_delta = None
    nodes_pct = None
    if base.nodes is not None and candidate.nodes is not None:
        nodes_delta = candidate.nodes - base.nodes
        nodes_pct = (nodes_delta / base.nodes * 100.0) if base.nodes else None

    elapsed_ms_delta = None
    elapsed_ms_pct = None
    if base.elapsed_ms is not None and candidate.elapsed_ms is not None:
        elapsed_ms_delta = candidate.elapsed_ms - base.elapsed_ms
        elapsed_ms_pct = (elapsed_ms_delta / base.elapsed_ms * 100.0) if base.elapsed_ms else None

    return BaselineDelta(
        comparable=True,
        reason="compared to baseline",
        result_checksum_changed=str(candidate.result_checksum != base.result_checksum).lower(),
        work_checksum_changed=str(candidate.work_checksum != base.work_checksum).lower(),
        nodes_delta=nodes_delta,
        nodes_pct=nodes_pct,
        elapsed_ms_delta=elapsed_ms_delta,
        elapsed_ms_pct=elapsed_ms_pct,
    )


def _md(value: object) -> str:
    return str(value).replace("|", "\\|")


def _display(value: object | None, digits: int = 2) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def _short_sha(value: str) -> str:
    return value[:12] if value and value != "n/a" else value


def _metric(result: TargetResult, attr: str) -> object | None:
    if result.search_metrics is None:
        return None
    return getattr(result.search_metrics, attr)


def render_summary_table(results: list[TargetResult]) -> str:
    baseline = find_baseline_result(results)
    lines = [
        "| role | name | config sha256 | eval status | eval report | search status | search JSONL | result checksum | work checksum | nodes | elapsed ms | exact root searches | best | checksum changed | failure |",
        "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | ---: | ---: | ---: | :--- | :--- | :--- |",
    ]
    for result in results:
        eval_result = result.eval_result
        eval_status = eval_result.status if eval_result is not None else "skipped"
        eval_report = eval_result.output_path if eval_result is not None and eval_result.output_path else "n/a"
        failed = "yes" if command_failed(result) else "no"
        delta = compare_to_baseline(baseline, result)
        checksum_changed = delta.result_checksum_changed
        if not delta.comparable:
            checksum_changed = f"n/a ({delta.reason})"
        lines.append(
            "| "
            + " | ".join(
                (
                    _md(result.target.role),
                    _md(result.target.label),
                    f"`{_short_sha(result.target.fingerprint.sha256)}`",
                    _md(eval_status),
                    f"`{_md(eval_report)}`" if eval_report != "n/a" else "n/a",
                    _md(result.search_result.status),
                    f"`{_md(result.search_result.output_path)}`" if result.search_result.output_path else "n/a",
                    _md(_metric(result, "result_checksum") or "n/a"),
                    _md(_metric(result, "work_checksum") or "n/a"),
                    _display(_metric(result, "nodes"), digits=0),
                    _display(_metric(result, "elapsed_ms")),
                    _display(_metric(result, "exact_root_searches"), digits=0),
                    _md(
                        f"{_metric(result, 'best_move') or 'n/a'} "
                        f"{_metric(result, 'score') or 'n/a'} "
                        f"{_metric(result, 'principal_variation') or 'n/a'}"
                    ),
                    _md(checksum_changed),
                    failed,
                )
            )
            + " |"
        )
    return "\n".join(lines)


def render_baseline_delta_table(results: list[TargetResult]) -> str:
    baseline = find_baseline_result(results)
    lines = [
        "| candidate | comparable | reason | result checksum changed | work checksum changed | nodes delta | nodes delta % | elapsed delta ms | elapsed delta % |",
        "| :--- | :--- | :--- | :--- | :--- | ---: | ---: | ---: | ---: |",
    ]
    for result in results:
        if result.target.role != "candidate":
            continue
        delta = compare_to_baseline(baseline, result)
        lines.append(
            "| "
            + " | ".join(
                (
                    _md(result.target.label),
                    "yes" if delta.comparable else "no",
                    _md(delta.reason),
                    delta.result_checksum_changed,
                    delta.work_checksum_changed,
                    _display(delta.nodes_delta, digits=0),
                    _display(delta.nodes_pct),
                    _display(delta.elapsed_ms_delta),
                    _display(delta.elapsed_ms_pct),
                )
            )
            + " |"
        )
    return "\n".join(lines)


def render_config_fingerprints(results: list[TargetResult]) -> str:
    lines = [
        "| role | name | path | sha256 | note |",
        "| :--- | :--- | :--- | :--- | :--- |",
    ]
    for result in results:
        note = result.target.fingerprint.error or result.target.fingerprint.warning or ""
        lines.append(
            "| "
            + " | ".join(
                (
                    _md(result.target.role),
                    _md(result.target.label),
                    f"`{_md(result.target.config_path)}`",
                    f"`{result.target.fingerprint.sha256}`",
                    _md(note or "ok"),
                )
            )
            + " |"
        )
    return "\n".join(lines)


def render_report(config: MatrixConfig, metadata: Metadata, results: list[TargetResult]) -> str:
    summary_path = config.out_dir / "summary.tsv"
    lines: list[str] = [
        "# Evaluation Candidate Matrix",
        "",
        f"Status: {matrix_status(config, results)}",
        "",
        "No strength claim. No automatic default promotion.",
        "",
        "Search/eval numbers are smoke evidence unless broader matches are run.",
        "Raw outputs belong under `runs/`; this workflow writes command logs, JSONL, summaries, and reports under the requested output directory.",
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
        f"- summary_tsv: `{summary_path}`",
        f"- search_jsonl_dir: `{config.out_dir / 'search-bench'}`",
        f"- allow_failures: `{str(config.allow_failures).lower()}`",
        "",
        "## Config Fingerprints",
        "",
        render_config_fingerprints(results),
        "",
        "## Summary",
        "",
        render_summary_table(results),
        "",
        "## Baseline Deltas",
        "",
        render_baseline_delta_table(results),
        "",
        "Checksum changes mean search behavior changed in this smoke matrix; they are not regressions by themselves.",
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
                f"- config_sha256: `{result.target.fingerprint.sha256}`",
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
            if command_result.error:
                lines.append(f"- error: `{command_result.error}`")
            lines.extend(["", "```sh", quote_command(command_result.command), "```", ""])

    lines.extend(
        [
            "## Caveats",
            "",
            "- This is an orchestration smoke workflow, not a tuner.",
            "- It does not promote any candidate automatically.",
            "- Eval-vs-exact compares heuristic scores with exact final-disc labels and is not calibrated as disc-margin error.",
            "- Search bench output is fixed-position smoke evidence; use broader deterministic matches or base/head comparison before making strength claims.",
            "- Keep generated reports, logs, JSONL, and analyzer output under `runs/`; do not commit raw run output.",
        ]
    )
    return "\n".join(lines) + "\n"


def write_summary_tsv(config: MatrixConfig, results: list[TargetResult]) -> None:
    path = config.out_dir / "summary.tsv"
    baseline = find_baseline_result(results)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t")
        writer.writerow(
            [
                "role",
                "name",
                "slug",
                "config_path",
                "config_sha256",
                "config_warning",
                "config_error",
                "eval_status",
                "eval_report_path",
                "eval_log_path",
                "search_status",
                "search_jsonl_path",
                "search_log_path",
                "aggregate_rows",
                "position_rows",
                "result_checksum",
                "work_checksum",
                "nodes",
                "elapsed_ms",
                "score_kind",
                "used_exact_endgame",
                "exact_root_positions",
                "exact_root_searches",
                "best_move",
                "score",
                "principal_variation",
                "baseline_comparable",
                "baseline_reason",
                "result_checksum_changed",
                "work_checksum_changed",
                "nodes_delta",
                "nodes_delta_pct",
                "elapsed_ms_delta",
                "elapsed_ms_delta_pct",
                "failure",
                "error",
            ]
        )
        for result in results:
            eval_result = result.eval_result
            metrics = result.search_metrics
            delta = compare_to_baseline(baseline, result)
            errors = "; ".join(
                value
                for value in (
                    result.target.fingerprint.error,
                    eval_result.error if eval_result is not None else "",
                    result.search_result.error,
                    result.search_parse_error,
                )
                if value
            )
            writer.writerow(
                [
                    result.target.role,
                    result.target.label,
                    result.target.slug,
                    result.target.config_path,
                    result.target.fingerprint.sha256,
                    result.target.fingerprint.warning,
                    result.target.fingerprint.error,
                    eval_result.status if eval_result is not None else "skipped",
                    eval_result.output_path if eval_result is not None and eval_result.output_path else "",
                    eval_result.log_path if eval_result is not None else "",
                    result.search_result.status,
                    result.search_result.output_path or "",
                    result.search_result.log_path,
                    metrics.aggregate_rows if metrics else "",
                    metrics.position_rows if metrics else "",
                    metrics.result_checksum if metrics else "",
                    metrics.work_checksum if metrics else "",
                    metrics.nodes if metrics and metrics.nodes is not None else "",
                    metrics.elapsed_ms if metrics and metrics.elapsed_ms is not None else "",
                    metrics.score_kind if metrics else "",
                    metrics.used_exact_endgame if metrics else "",
                    metrics.exact_root_positions if metrics and metrics.exact_root_positions is not None else "",
                    metrics.exact_root_searches if metrics and metrics.exact_root_searches is not None else "",
                    metrics.best_move if metrics else "",
                    metrics.score if metrics else "",
                    metrics.principal_variation if metrics else "",
                    str(delta.comparable).lower(),
                    delta.reason,
                    delta.result_checksum_changed,
                    delta.work_checksum_changed,
                    delta.nodes_delta if delta.nodes_delta is not None else "",
                    delta.nodes_pct if delta.nodes_pct is not None else "",
                    delta.elapsed_ms_delta if delta.elapsed_ms_delta is not None else "",
                    delta.elapsed_ms_pct if delta.elapsed_ms_pct is not None else "",
                    "yes" if command_failed(result) else "no",
                    errors,
                ]
            )


def run_matrix(config: MatrixConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    metadata = collect_metadata()
    results = [run_target(config, target) for target in make_targets(config)]

    write_summary_tsv(config, results)
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
