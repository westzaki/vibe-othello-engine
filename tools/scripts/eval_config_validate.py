#!/usr/bin/env python3
"""Validate generated .eval candidates against held-out exact-label JSONL."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from common import ScriptError, quote_command
from eval_config_tuner import (
    DEFAULT_HIGH_CONFIDENCE_PENALTY,
    DEFAULT_HIGH_CONFIDENCE_THRESHOLD,
    DEFAULT_WRONG_DIRECTION_PENALTY,
    MOVE_RANK_METRIC_KEYS,
    OBJECTIVE_FORMULA,
    AnalyzerMetrics,
    EvalConfig,
    changed_entries,
    move_rank_cells,
    parse_analyzer_stdout,
    parse_csv,
    parse_eval_config_text,
    parse_non_negative_int,
    parse_positive_int,
    sha256_file,
)


@dataclass(frozen=True)
class ValidationConfig:
    validation_labels: Path
    base_config: Path
    candidate_configs: tuple[Path, ...]
    candidate_source: str
    out_dir: Path
    build_dir: Path
    eval_vs_exact: Path
    top: int
    wrong_direction_penalty: int
    high_confidence_penalty: int
    high_confidence_threshold: int
    move_rank_analysis: bool
    train_summary: Path | None
    dry_run: bool
    allow_failures: bool
    invocation: list[str]


@dataclass(frozen=True)
class Metadata:
    timestamp: str
    git_sha: str
    validation_labels_sha256: str
    base_config_sha256: str


@dataclass(frozen=True)
class ValidationResult:
    candidate_id: str
    config_path: Path
    report_path: Path
    log_path: Path
    command: list[str]
    config: EvalConfig | None
    status: str
    metrics: AnalyzerMetrics | None = None
    objective: int | None = None
    delta_vs_base: int | None = None
    exit_code: int | None = None
    error: str = ""


@dataclass(frozen=True)
class TrainSummaryRow:
    rank: str
    candidate: str
    objective: str
    config: str


@dataclass(frozen=True)
class TrainSummary:
    rows: tuple[TrainSummaryRow, ...]
    by_candidate: dict[str, tuple[TrainSummaryRow, ...]]
    by_config_stem: dict[str, tuple[TrainSummaryRow, ...]]


def default_out_dir() -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("runs") / "eval-config-validation" / timestamp


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate .eval candidates on held-out exact-label JSONL."
    )
    parser.add_argument("--validation-labels", required=True, help="held-out exact_label.v1 JSONL")
    parser.add_argument("--base-config", required=True, help="fully expanded base .eval config")
    parser.add_argument("--candidate-configs", help="comma-separated candidate .eval paths")
    parser.add_argument("--candidate-dir", help="directory containing candidate *.eval files")
    parser.add_argument(
        "--out",
        default=None,
        help="validation output directory (default: runs/eval-config-validation/<timestamp>)",
    )
    parser.add_argument("--build-dir", default="build", help="directory containing C++ tools")
    parser.add_argument("--eval-vs-exact", help="override othello_eval_vs_exact path")
    parser.add_argument("--top", type=parse_positive_int, default=10)
    parser.add_argument(
        "--wrong-direction-penalty",
        type=parse_non_negative_int,
        default=DEFAULT_WRONG_DIRECTION_PENALTY,
    )
    parser.add_argument(
        "--high-confidence-penalty",
        type=parse_non_negative_int,
        default=DEFAULT_HIGH_CONFIDENCE_PENALTY,
    )
    parser.add_argument(
        "--high-confidence-threshold",
        type=parse_non_negative_int,
        default=DEFAULT_HIGH_CONFIDENCE_THRESHOLD,
    )
    parser.add_argument(
        "--move-rank-analysis",
        action="store_true",
        help="pass --move-rank-analysis to othello_eval_vs_exact and record root move-quality metrics",
    )
    parser.add_argument("--train-summary", help="optional tuner summary.tsv")
    parser.add_argument("--dry-run", action="store_true", help="write planned commands only")
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="return zero while recording analyzer failures",
    )
    return parser.parse_args(argv)


def discover_candidate_configs(args: argparse.Namespace) -> tuple[tuple[Path, ...], str]:
    has_configs = bool(args.candidate_configs)
    has_dir = bool(args.candidate_dir)
    if not has_configs and not has_dir:
        raise ScriptError("one of --candidate-configs or --candidate-dir is required")
    if has_configs and has_dir:
        raise ScriptError("cannot combine --candidate-configs and --candidate-dir")

    if has_configs:
        candidates = tuple(Path(path) for path in parse_csv(args.candidate_configs))
        source = f"--candidate-configs {args.candidate_configs}"
    else:
        candidate_dir = Path(args.candidate_dir)
        candidates = tuple(sorted(candidate_dir.glob("*.eval"), key=lambda path: path.name))
        source = f"--candidate-dir {candidate_dir}"
        if not candidates:
            raise ScriptError(f"no .eval candidates found in: {candidate_dir}")

    resolved: set[Path] = set()
    duplicate_paths: list[Path] = []
    for path in candidates:
        resolved_path = path.expanduser().resolve(strict=False)
        if resolved_path in resolved:
            duplicate_paths.append(path)
        resolved.add(resolved_path)
    if duplicate_paths:
        duplicates = ", ".join(str(path) for path in duplicate_paths)
        raise ScriptError("duplicate candidate paths: " + duplicates)

    seen_ids: set[str] = set()
    duplicate_ids: list[str] = []
    for path in candidates:
        candidate_id = path.stem
        if candidate_id in seen_ids:
            duplicate_ids.append(candidate_id)
        seen_ids.add(candidate_id)
    if duplicate_ids:
        raise ScriptError("duplicate candidate output ids: " + ", ".join(duplicate_ids))

    return candidates, source


def config_from_args(
    args: argparse.Namespace,
    invocation: list[str] | None = None,
) -> ValidationConfig:
    candidates, source = discover_candidate_configs(args)
    build_dir = Path(args.build_dir)
    return ValidationConfig(
        validation_labels=Path(args.validation_labels),
        base_config=Path(args.base_config),
        candidate_configs=candidates,
        candidate_source=source,
        out_dir=Path(args.out) if args.out else default_out_dir(),
        build_dir=build_dir,
        eval_vs_exact=Path(args.eval_vs_exact)
        if args.eval_vs_exact
        else build_dir / "othello_eval_vs_exact",
        top=args.top,
        wrong_direction_penalty=args.wrong_direction_penalty,
        high_confidence_penalty=args.high_confidence_penalty,
        high_confidence_threshold=args.high_confidence_threshold,
        move_rank_analysis=args.move_rank_analysis,
        train_summary=Path(args.train_summary) if args.train_summary else None,
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        invocation=invocation or [],
    )


def collect_metadata(config: ValidationConfig) -> Metadata:
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
        validation_labels_sha256=sha256_file(config.validation_labels),
        base_config_sha256=sha256_file(config.base_config),
    )


def load_config_file(path: Path, label: str) -> EvalConfig:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to read {label} config: {path}: {exc}") from exc
    return parse_eval_config_text(text)


def validation_objective(metrics: AnalyzerMetrics, config: ValidationConfig) -> int:
    return (
        metrics.sign_agreements
        - config.wrong_direction_penalty * metrics.wrong_direction
        - config.high_confidence_penalty * metrics.high_confidence_wrong_direction
    )


def analyzer_command(
    config: ValidationConfig,
    *,
    eval_config_path: Path,
    report_path: Path,
) -> list[str]:
    command = [
        str(config.eval_vs_exact),
        "--labels",
        str(config.validation_labels),
        "--eval-config",
        str(eval_config_path),
        "--output",
        str(report_path),
        "--high-confidence-threshold",
        str(config.high_confidence_threshold),
    ]
    if config.move_rank_analysis:
        command.append("--move-rank-analysis")
    return command


def write_log(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run_analyzer_result(
    *,
    candidate_id: str,
    eval_config: EvalConfig | None,
    config_path: Path,
    report_path: Path,
    log_path: Path,
    command: list[str],
    config: ValidationConfig,
) -> ValidationResult:
    if config.dry_run:
        write_log(log_path, f"dry run\ncommand: {quote_command(command)}\n")
        return ValidationResult(
            candidate_id=candidate_id,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            status="planned",
        )

    try:
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
    except OSError as exc:
        output = f"command: {quote_command(command)}\nfailed to start: {exc}\n"
        write_log(log_path, output)
        return ValidationResult(
            candidate_id=candidate_id,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            status="failed",
            error=str(exc),
        )

    output = (
        f"command: {quote_command(command)}\n"
        f"exit_code: {completed.returncode}\n\n"
        f"stdout:\n{completed.stdout}\n"
        f"stderr:\n{completed.stderr}\n"
    )
    write_log(log_path, output)
    if completed.returncode != 0:
        return ValidationResult(
            candidate_id=candidate_id,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            status="failed",
            exit_code=completed.returncode,
            error=completed.stderr.strip(),
        )

    try:
        metrics = parse_analyzer_stdout(completed.stdout)
    except ScriptError as exc:
        return ValidationResult(
            candidate_id=candidate_id,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            status="failed",
            exit_code=completed.returncode,
            error=str(exc),
        )

    return ValidationResult(
        candidate_id=candidate_id,
        config_path=config_path,
        report_path=report_path,
        log_path=log_path,
        command=command,
        config=eval_config,
        status="passed",
        metrics=metrics,
        objective=validation_objective(metrics, config),
        exit_code=completed.returncode,
    )


def with_deltas(
    results: list[ValidationResult],
    base_result: ValidationResult,
) -> list[ValidationResult]:
    if base_result.objective is None:
        return results
    updated = []
    for result in results:
        delta = result.objective - base_result.objective if result.objective is not None else None
        updated.append(
            ValidationResult(
                candidate_id=result.candidate_id,
                config_path=result.config_path,
                report_path=result.report_path,
                log_path=result.log_path,
                command=result.command,
                config=result.config,
                status=result.status,
                metrics=result.metrics,
                objective=result.objective,
                delta_vs_base=delta,
                exit_code=result.exit_code,
                error=result.error,
            )
        )
    return updated


def result_sort_key(result: ValidationResult) -> tuple[int, int, int, int, str]:
    metrics = result.metrics
    return (
        -(result.objective if result.objective is not None else -10**18),
        -(metrics.sign_agreements if metrics else -1),
        metrics.wrong_direction if metrics else 10**18,
        metrics.high_confidence_wrong_direction if metrics else 10**18,
        result.candidate_id,
    )


def ranked_successes(results: list[ValidationResult]) -> list[ValidationResult]:
    return sorted([result for result in results if result.metrics is not None], key=result_sort_key)


def result_cells(result: ValidationResult) -> tuple[str, str, str, str, str, str]:
    if result.metrics is None or result.objective is None:
        return ("n/a", "n/a", "n/a", "n/a", "n/a", "n/a")
    return (
        str(result.objective),
        str(result.delta_vs_base) if result.delta_vs_base is not None else "n/a",
        str(result.metrics.analyzed),
        str(result.metrics.sign_agreements),
        str(result.metrics.wrong_direction),
        str(result.metrics.high_confidence_wrong_direction),
    )


def parse_train_summary(path: Path) -> TrainSummary:
    try:
        with path.open("r", encoding="utf-8", newline="") as input_file:
            rows = list(csv.DictReader(input_file, delimiter="\t"))
    except OSError as exc:
        raise ScriptError(f"failed to read train summary: {path}: {exc}") from exc

    parsed_rows: list[TrainSummaryRow] = []
    for index, row in enumerate(rows, start=2):
        missing = {"rank", "candidate", "objective", "config"} - set(row)
        if missing:
            raise ScriptError(
                f"{path}: missing required train summary columns on line {index}: "
                + ", ".join(sorted(missing))
            )
        parsed_rows.append(
            TrainSummaryRow(
                rank=row["rank"],
                candidate=row["candidate"],
                objective=row["objective"],
                config=row["config"],
            )
        )

    by_candidate: dict[str, list[TrainSummaryRow]] = {}
    by_config_stem: dict[str, list[TrainSummaryRow]] = {}
    for row in parsed_rows:
        by_candidate.setdefault(row.candidate, []).append(row)
        if row.config:
            by_config_stem.setdefault(Path(row.config).stem, []).append(row)

    return TrainSummary(
        rows=tuple(parsed_rows),
        by_candidate={key: tuple(value) for key, value in by_candidate.items()},
        by_config_stem={key: tuple(value) for key, value in by_config_stem.items()},
    )


def train_match_for(
    result: ValidationResult,
    train_summary: TrainSummary,
) -> tuple[TrainSummaryRow | None, str]:
    candidate_matches = train_summary.by_candidate.get(result.candidate_id, ())
    stem_matches = train_summary.by_config_stem.get(result.config_path.stem, ())
    unique_matches = {id(row): row for row in (*candidate_matches, *stem_matches)}
    if len(unique_matches) == 1:
        return next(iter(unique_matches.values())), ""
    if len(unique_matches) > 1:
        return None, "ambiguous"
    return None, "unmatched"


def render_train_vs_heldout(
    *,
    ranked_candidates: list[ValidationResult],
    train_summary: TrainSummary,
) -> list[str]:
    lines = [
        "## Train vs Held-Out",
        "",
        "| Held-Out Rank | Candidate | Train Rank | Train Objective | Held-Out Objective | Delta vs Base | Note |",
        "|---:|---|---:|---:|---:|---:|---|",
    ]
    matched_rows: set[int] = set()
    for rank, result in enumerate(ranked_candidates, start=1):
        row, note = train_match_for(result, train_summary)
        if row is not None:
            matched_rows.add(id(row))
            train_rank = row.rank or "n/a"
            train_objective = row.objective or "n/a"
            note = ""
        else:
            train_rank = "n/a"
            train_objective = "n/a"
        objective_text, delta, *_ = result_cells(result)
        lines.append(
            f"| {rank} | `{result.candidate_id}` | {train_rank} | {train_objective} | "
            f"{objective_text} | {delta} | {note or ''} |"
        )

    unmatched_train = [
        row
        for row in train_summary.rows
        if row.candidate != "base" and id(row) not in matched_rows
    ]
    if unmatched_train:
        lines.extend(["", "Unmatched train-summary rows:"])
        for row in unmatched_train:
            lines.append(
                f"- candidate `{row.candidate}` rank `{row.rank}` objective `{row.objective}` "
                f"config `{row.config}`"
            )
    lines.append("")
    return lines


def render_report(
    *,
    config: ValidationConfig,
    metadata: Metadata,
    base_config: EvalConfig,
    base_result: ValidationResult,
    candidate_results: list[ValidationResult],
    train_summary: TrainSummary | None,
) -> str:
    ranked_candidates = ranked_successes(candidate_results)
    best = ranked_candidates[0] if ranked_candidates else None
    failures = [result for result in [base_result, *candidate_results] if result.status == "failed"]
    displayed_candidates = ranked_candidates[: config.top]

    lines = [
        "# Held-Out Eval Config Validation Report",
        "",
        "No strength claim. This is diagnostic held-out exact-label validation only.",
        "",
        "## Metadata",
        "",
        f"- timestamp: `{metadata.timestamp}`",
        f"- git_sha: `{metadata.git_sha}`",
        f"- command: `{quote_command(config.invocation) if config.invocation else 'unknown'}`",
        f"- validation_labels_path: `{config.validation_labels}`",
        f"- validation_labels_sha256: `{metadata.validation_labels_sha256}`",
        f"- base_config_path: `{config.base_config}`",
        f"- base_config_sha256: `{metadata.base_config_sha256}`",
        f"- candidate_source: `{config.candidate_source}`",
        f"- candidate_count: `{len(config.candidate_configs)}`",
        f"- eval_vs_exact_path: `{config.eval_vs_exact}`",
        f"- objective_formula: `{OBJECTIVE_FORMULA}`",
        f"- wrong_direction_penalty: `{config.wrong_direction_penalty}`",
        f"- high_confidence_penalty: `{config.high_confidence_penalty}`",
        f"- high_confidence_threshold: `{config.high_confidence_threshold}`",
        f"- move_rank_analysis: `{str(config.move_rank_analysis).lower()}`",
        f"- top: `{config.top}`",
        "",
        "## Base Result",
        "",
    ]

    base_objective, _, base_analyzed, base_sign, base_wrong, base_high = result_cells(base_result)
    lines.extend(
        [
            f"- objective: `{base_objective}`",
            f"- analyzed: `{base_analyzed}`",
            f"- sign_agreements: `{base_sign}`",
            f"- wrong_direction: `{base_wrong}`",
            f"- high_confidence_wrong_direction: `{base_high}`",
            f"- report_path: `{base_result.report_path}`",
            "",
            "## Candidate Results",
            "",
            "| Rank | Candidate | Objective | Delta vs Base | Analyzed | Sign Agreements | Wrong Direction | High Confidence Wrong Direction | Report |",
            "|---|---|---:|---:|---:|---:|---:|---:|---|",
        ]
    )

    for rank, result in enumerate(displayed_candidates, start=1):
        objective_text, delta, analyzed, sign_agreements, wrong_direction, high_confidence = (
            result_cells(result)
        )
        lines.append(
            f"| {rank} | `{result.candidate_id}` | {objective_text} | {delta} | "
            f"{analyzed} | {sign_agreements} | {wrong_direction} | {high_confidence} | "
            f"`{result.report_path}` |"
        )
    if not displayed_candidates:
        lines.append("| - | no successful candidates | n/a | n/a | n/a | n/a | n/a | n/a | n/a |")
    if len(ranked_candidates) > config.top:
        lines.append("")
        lines.append(f"Showing top `{config.top}` of `{len(ranked_candidates)}` successful candidates.")

    if config.move_rank_analysis:
        lines.extend(
            [
                "",
                "## Move-Rank Metrics",
                "",
                "These metrics are recorded from `othello_eval_vs_exact --move-rank-analysis` for diagnostics only. The held-out objective still uses the sign-agreement formula above.",
                "",
                "| Candidate | Records With Scores | Missing Scores | No Legal Root Moves | Analyzed | Top Group Exact-Best | Top Group Non-Best | Exact-Best Rank Sum | Eval Gap Sum | Exact Gap Sum | Report |",
                "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
            ]
        )
        for result in [base_result, *ranked_candidates]:
            cells = move_rank_cells(result.metrics)
            lines.append(
                f"| `{result.candidate_id}` | "
                + " | ".join(cells)
                + f" | `{result.report_path}` |"
            )

    if train_summary is not None:
        lines.extend(["", *render_train_vs_heldout(ranked_candidates=displayed_candidates, train_summary=train_summary)])

    lines.extend(["", "## Best Held-Out Candidate", ""])
    if best is None:
        lines.append("- no successful candidate")
    else:
        lines.extend(
            [
                f"- path: `{best.config_path}`",
                f"- objective: `{best.objective}`",
                f"- delta_vs_base: `{best.delta_vs_base}`",
                "- changed_keys_vs_base:",
            ]
        )
        if best.config is None:
            lines.append("  - n/a")
        else:
            changes = changed_entries(best.config, base_config)
            if not changes:
                lines.append("  - none")
            else:
                for key, old, new in changes:
                    lines.append(f"  - `{key}`: `{old}` -> `{new}`")
        lines.append(
            "- caveat: held-out diagnostic improvement is not a strength claim or promotion gate"
        )

    if displayed_candidates:
        lines.extend(["", "## Candidate Diffs", ""])
        for result in displayed_candidates:
            lines.append(f"### {result.candidate_id}")
            if result.config is None:
                lines.append("- n/a")
            else:
                changes = changed_entries(result.config, base_config)
                if not changes:
                    lines.append("- no changes from base")
                else:
                    for key, old, new in changes:
                        lines.append(f"- `{key}`: `{old}` -> `{new}`")
            lines.append("")

    lines.extend(["## Commands", ""])
    for result in [base_result, *candidate_results]:
        lines.extend(
            [
                f"### {result.candidate_id}",
                "",
                f"- status: `{result.status}`",
                f"- log: `{result.log_path}`",
            ]
        )
        if result.exit_code is not None:
            lines.append(f"- exit_code: `{result.exit_code}`")
        lines.extend(["", "```sh", quote_command(result.command), "```", ""])

    if failures:
        lines.extend(["", "## Failures", ""])
        for result in failures:
            lines.append(
                f"- `{result.candidate_id}` failed; log `{result.log_path}`; error: {result.error}"
            )

    lines.extend(
        [
            "",
            "## Caveats",
            "",
            "- Evaluator scores are heuristic units and are not calibrated disc margins.",
            "- Exact labels are final disc margins from the side-to-move perspective.",
            "- The objective is diagnostic, not Elo or a strength claim.",
            "- This report is not a default-promotion gate or recommendation.",
            "- Validation labels may still be biased.",
            "- Random playout labels may not be representative training data.",
            "- Move-rank analysis is root move-quality evidence only; missing `move_scores` are reported as a caveat, not a workflow failure.",
            "- Candidates need search bench, match runner or base/head validation, and external sanity when appropriate before any promotion claim.",
        ]
    )
    return "\n".join(lines) + "\n"


def write_summary_tsv(
    path: Path,
    *,
    base_result: ValidationResult,
    candidate_results: list[ValidationResult],
) -> None:
    lines = [
        "rank\tcandidate\tstatus\tobjective\tdelta_vs_base\tanalyzed\tsign_agreements\twrong_direction\thigh_confidence_wrong_direction\t"
        + "\t".join(MOVE_RANK_METRIC_KEYS)
        + "\tconfig\treport"
    ]
    ranked = ranked_successes(candidate_results)
    ranked_ids = {result.candidate_id: rank for rank, result in enumerate(ranked, start=1)}
    for result in [base_result, *candidate_results]:
        objective_text, delta, analyzed, sign_agreements, wrong_direction, high_confidence = (
            result_cells(result)
        )
        lines.append(
            "\t".join(
                [
                    str(ranked_ids.get(result.candidate_id, "")),
                    result.candidate_id,
                    result.status,
                    objective_text,
                    delta,
                    analyzed,
                    sign_agreements,
                    wrong_direction,
                    high_confidence,
                    *move_rank_cells(result.metrics),
                    str(result.config_path),
                    str(result.report_path),
                ]
            )
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_validation(config: ValidationConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    (config.out_dir / "reports").mkdir(parents=True, exist_ok=True)
    (config.out_dir / "logs").mkdir(parents=True, exist_ok=True)

    base_config = load_config_file(config.base_config, "base")
    candidate_configs = {
        path: load_config_file(path, "candidate")
        for path in config.candidate_configs
    }
    metadata = collect_metadata(config)
    train_summary = parse_train_summary(config.train_summary) if config.train_summary else None

    base_report = config.out_dir / "reports" / "base.md"
    base_log = config.out_dir / "logs" / "base.log"
    base_command = analyzer_command(
        config,
        eval_config_path=config.base_config,
        report_path=base_report,
    )
    base_result = run_analyzer_result(
        candidate_id="base",
        eval_config=base_config,
        config_path=config.base_config,
        report_path=base_report,
        log_path=base_log,
        command=base_command,
        config=config,
    )

    candidate_results: list[ValidationResult] = []
    for candidate_path, candidate_config in candidate_configs.items():
        candidate_id = candidate_path.stem
        report_path = config.out_dir / "reports" / f"{candidate_id}.md"
        log_path = config.out_dir / "logs" / f"{candidate_id}.log"
        command = analyzer_command(
            config,
            eval_config_path=candidate_path,
            report_path=report_path,
        )
        candidate_results.append(
            run_analyzer_result(
                candidate_id=candidate_id,
                eval_config=candidate_config,
                config_path=candidate_path,
                report_path=report_path,
                log_path=log_path,
                command=command,
                config=config,
            )
        )

    updated = with_deltas([base_result, *candidate_results], base_result)
    base_result = updated[0]
    candidate_results = updated[1:]

    report = render_report(
        config=config,
        metadata=metadata,
        base_config=base_config,
        base_result=base_result,
        candidate_results=candidate_results,
        train_summary=train_summary,
    )
    (config.out_dir / "validation_report.md").write_text(report, encoding="utf-8")
    write_summary_tsv(
        config.out_dir / "summary.tsv",
        base_result=base_result,
        candidate_results=candidate_results,
    )

    failed = any(result.status == "failed" for result in [base_result, *candidate_results])
    if failed and not config.allow_failures:
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_validation(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
