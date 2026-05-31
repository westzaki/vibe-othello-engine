#!/usr/bin/env python3
"""Run a small exact-label diagnostic search over fully expanded .eval configs."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import random
import re
import subprocess
import sys
from dataclasses import dataclass, replace
from pathlib import Path

from common import ScriptError, parse_csv_values, quote_command


DEFAULT_SEED = 20260531
DEFAULT_ROUNDS = 1
DEFAULT_STEP = 1
DEFAULT_MAX_CANDIDATES = 64
DEFAULT_MIN_WEIGHT = -100
DEFAULT_MAX_WEIGHT = 100
DEFAULT_WRONG_DIRECTION_PENALTY = 2
DEFAULT_HIGH_CONFIDENCE_PENALTY = 1
DEFAULT_HIGH_CONFIDENCE_THRESHOLD = 250
PHASE_BOUNDARY_KEYS = {"opening_max_occupied", "midgame_max_occupied"}
OBJECTIVE_FORMULA = (
    "sign_agreements - wrong_direction_penalty * wrong_direction_count - "
    "high_confidence_penalty * high_confidence_wrong_direction_count"
)
MOVE_RANK_METRIC_KEYS = (
    "move_rank_records_with_scores",
    "move_rank_records_missing_scores",
    "move_rank_records_no_legal_root_moves",
    "move_rank_analyzed",
    "move_rank_top_exact_best",
    "move_rank_top_non_best",
    "move_rank_exact_best_rank_sum",
    "move_rank_eval_score_gap_sum",
    "move_rank_exact_score_gap_sum",
)


@dataclass(frozen=True)
class EvalEntry:
    key: str
    value: int


@dataclass(frozen=True)
class EvalConfig:
    name: str | None
    entries: tuple[EvalEntry, ...]

    def values(self) -> dict[str, int]:
        return {entry.key: entry.value for entry in self.entries}

    def with_value(self, key: str, value: int) -> "EvalConfig":
        return replace(
            self,
            entries=tuple(
                EvalEntry(entry.key, value if entry.key == key else entry.value)
                for entry in self.entries
            ),
        )

    def signature(self) -> tuple[tuple[str, int], ...]:
        return tuple((entry.key, entry.value) for entry in self.entries)


@dataclass(frozen=True)
class TunerConfig:
    labels: Path
    base_config: Path
    build_dir: Path
    out_dir: Path
    rounds: int
    step: int
    max_candidates: int
    seed: int
    keys: list[str] | None
    include_phase_boundaries: bool
    min_weight: int
    max_weight: int
    wrong_direction_penalty: int
    high_confidence_penalty: int
    high_confidence_threshold: int
    move_rank_analysis: bool
    eval_vs_exact: Path
    dry_run: bool
    allow_failures: bool
    invocation: list[str]


@dataclass(frozen=True)
class Metadata:
    timestamp: str
    git_sha: str
    labels_sha256: str
    base_config_sha256: str


@dataclass(frozen=True)
class AnalyzerMetrics:
    records_read: int
    analyzed: int
    sign_agreements: int
    wrong_direction: int
    high_confidence_wrong_direction: int
    move_rank_records_with_scores: int | None = None
    move_rank_records_missing_scores: int | None = None
    move_rank_records_no_legal_root_moves: int | None = None
    move_rank_analyzed: int | None = None
    move_rank_top_exact_best: int | None = None
    move_rank_top_non_best: int | None = None
    move_rank_exact_best_rank_sum: int | None = None
    move_rank_eval_score_gap_sum: int | None = None
    move_rank_exact_score_gap_sum: int | None = None


@dataclass(frozen=True)
class CandidateSpec:
    candidate_id: str
    round_index: int
    config: EvalConfig
    changed_key: str
    delta: int
    config_path: Path
    report_path: Path
    log_path: Path


@dataclass(frozen=True)
class CandidateResult:
    candidate_id: str
    round_index: int
    config_path: Path
    report_path: Path
    log_path: Path
    command: list[str]
    config: EvalConfig
    changed_key: str | None
    delta: int | None
    status: str
    metrics: AnalyzerMetrics | None = None
    objective: int | None = None
    exit_code: int | None = None
    error: str = ""


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


def parse_int(value: str) -> int:
    try:
        return int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc


def parse_csv(value: str) -> list[str]:
    return parse_csv_values(
        value,
        empty_segment_message="CSV values must not contain empty segments",
    )


def default_out_dir() -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("runs") / "eval-config-tuner" / timestamp


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Tune .eval weights against exact-label JSONL.")
    parser.add_argument("--labels", required=True, help="exact_label.v1 JSONL labels")
    parser.add_argument("--base-config", required=True, help="fully expanded .eval base config")
    parser.add_argument("--build-dir", default="build", help="directory containing C++ tools")
    parser.add_argument(
        "--out",
        default=None,
        help="output directory (default: runs/eval-config-tuner/<timestamp>)",
    )
    parser.add_argument("--rounds", type=parse_positive_int, default=DEFAULT_ROUNDS)
    parser.add_argument("--step", type=parse_positive_int, default=DEFAULT_STEP)
    parser.add_argument(
        "--max-candidates",
        type=parse_positive_int,
        default=DEFAULT_MAX_CANDIDATES,
        help="maximum coordinate perturbation candidates per round",
    )
    parser.add_argument("--seed", type=parse_non_negative_int, default=DEFAULT_SEED)
    parser.add_argument("--keys", help="comma-separated .eval keys to tune")
    parser.add_argument(
        "--include-phase-boundaries",
        action="store_true",
        help="allow opening_max_occupied and midgame_max_occupied to be tuned",
    )
    parser.add_argument("--min-weight", type=parse_int, default=DEFAULT_MIN_WEIGHT)
    parser.add_argument("--max-weight", type=parse_int, default=DEFAULT_MAX_WEIGHT)
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
    parser.add_argument("--eval-vs-exact", help="override othello_eval_vs_exact path")
    parser.add_argument("--dry-run", action="store_true", help="write planned commands only")
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="return zero while recording analyzer failures",
    )
    return parser.parse_args(argv)


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> TunerConfig:
    if args.min_weight > args.max_weight:
        raise ScriptError("--min-weight must be <= --max-weight")
    build_dir = Path(args.build_dir)
    keys = parse_csv(args.keys) if args.keys else None
    return TunerConfig(
        labels=Path(args.labels),
        base_config=Path(args.base_config),
        build_dir=build_dir,
        out_dir=Path(args.out) if args.out else default_out_dir(),
        rounds=args.rounds,
        step=args.step,
        max_candidates=args.max_candidates,
        seed=args.seed,
        keys=keys,
        include_phase_boundaries=args.include_phase_boundaries,
        min_weight=args.min_weight,
        max_weight=args.max_weight,
        wrong_direction_penalty=args.wrong_direction_penalty,
        high_confidence_penalty=args.high_confidence_penalty,
        high_confidence_threshold=args.high_confidence_threshold,
        move_rank_analysis=args.move_rank_analysis,
        eval_vs_exact=Path(args.eval_vs_exact)
        if args.eval_vs_exact
        else build_dir / "othello_eval_vs_exact",
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        invocation=invocation or [],
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as input_file:
            for chunk in iter(lambda: input_file.read(65536), b""):
                digest.update(chunk)
    except OSError as exc:
        raise ScriptError(f"failed to read file for SHA256: {path}: {exc}") from exc
    return digest.hexdigest()


def collect_metadata(config: TunerConfig) -> Metadata:
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
        labels_sha256=sha256_file(config.labels),
        base_config_sha256=sha256_file(config.base_config),
    )


def parse_eval_config_text(text: str) -> EvalConfig:
    name: str | None = None
    entries: list[EvalEntry] = []
    seen_numeric: set[str] = set()
    seen_name = False

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        if "=" not in line:
            raise ScriptError(f"line {line_number}: expected key=value")
        key, value = (part.strip() for part in line.split("=", 1))
        if not key:
            raise ScriptError(f"line {line_number}: empty key")
        if key == "name":
            if seen_name:
                raise ScriptError(f"line {line_number}: duplicate key: name")
            seen_name = True
            name = value
            continue
        if key in seen_numeric:
            raise ScriptError(f"line {line_number}: duplicate numeric key: {key}")
        try:
            parsed_value = int(value)
        except ValueError as exc:
            raise ScriptError(f"line {line_number}: invalid integer for key: {key}") from exc
        seen_numeric.add(key)
        entries.append(EvalEntry(key=key, value=parsed_value))

    if not entries:
        raise ScriptError("evaluation config has no numeric keys")
    return EvalConfig(name=name, entries=tuple(entries))


def load_eval_config(path: Path) -> EvalConfig:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to read base config: {path}: {exc}") from exc
    return parse_eval_config_text(text)


def is_default_tunable_key(key: str) -> bool:
    return key not in PHASE_BOUNDARY_KEYS and "." in key


def select_tunable_keys(eval_config: EvalConfig, config: TunerConfig) -> list[str]:
    values = eval_config.values()
    if config.keys is not None:
        selected = config.keys
        missing = [key for key in selected if key not in values]
        if missing:
            raise ScriptError("requested --keys not found in base config: " + ", ".join(missing))
    else:
        selected = [entry.key for entry in eval_config.entries if is_default_tunable_key(entry.key)]
        if config.include_phase_boundaries:
            selected.extend(
                entry.key for entry in eval_config.entries if entry.key in PHASE_BOUNDARY_KEYS
            )

    blocked = [
        key for key in selected if key in PHASE_BOUNDARY_KEYS and not config.include_phase_boundaries
    ]
    if blocked:
        raise ScriptError(
            "phase boundary keys require --include-phase-boundaries: " + ", ".join(blocked)
        )
    if not selected:
        raise ScriptError("no tunable keys selected")
    return selected


def clamp(value: int, minimum: int, maximum: int) -> int:
    return max(minimum, min(maximum, value))


def generate_round_candidates(
    center: EvalConfig,
    selected_keys: list[str],
    config: TunerConfig,
    *,
    round_index: int,
    next_candidate_index: int,
    existing_signatures: set[tuple[tuple[str, int], ...]] | None = None,
) -> tuple[list[CandidateSpec], int, int]:
    generated: list[tuple[str, int, EvalConfig]] = []
    seen = set(existing_signatures or ())
    seen.add(center.signature())
    values = center.values()
    for key in selected_keys:
        for delta in (config.step, -config.step):
            next_value = clamp(values[key] + delta, config.min_weight, config.max_weight)
            if next_value == values[key]:
                continue
            candidate = center.with_value(key, next_value)
            signature = candidate.signature()
            if signature in seen:
                continue
            seen.add(signature)
            generated.append((key, delta, candidate))

    generated_count = len(generated)
    if len(generated) > config.max_candidates:
        rng = random.Random(config.seed + round_index)
        indices = sorted(rng.sample(range(len(generated)), config.max_candidates))
        generated = [generated[index] for index in indices]

    specs: list[CandidateSpec] = []
    candidate_index = next_candidate_index
    for key, delta, candidate in generated:
        candidate_id = f"candidate_{candidate_index:04d}"
        specs.append(
            CandidateSpec(
                candidate_id=candidate_id,
                round_index=round_index,
                config=replace(candidate, name=f"tuned_{candidate_id}"),
                changed_key=key,
                delta=delta,
                config_path=config.out_dir / "configs" / f"{candidate_id}.eval",
                report_path=config.out_dir / "reports" / f"{candidate_id}.md",
                log_path=config.out_dir / "logs" / f"{candidate_id}.log",
            )
        )
        candidate_index += 1

    return specs, generated_count, candidate_index


def render_eval_config(
    eval_config: EvalConfig,
    *,
    config: TunerConfig,
    metadata: Metadata,
    candidate_id: str,
) -> str:
    lines = [
        "# generated_by=tools/scripts/eval_config_tuner.py",
        f"# base_config={config.base_config}",
        f"# base_config_sha256={metadata.base_config_sha256}",
        f"# labels={config.labels}",
        f"# labels_sha256={metadata.labels_sha256}",
        f"# seed={config.seed}",
        f"# objective={OBJECTIVE_FORMULA}",
        "# caveat=experimental local candidate; no strength claim; not default promotion",
        f"name={eval_config.name or candidate_id}",
        "",
    ]
    lines.extend(f"{entry.key}={entry.value}" for entry in eval_config.entries)
    return "\n".join(lines) + "\n"


def write_candidate_config(
    spec: CandidateSpec,
    *,
    config: TunerConfig,
    metadata: Metadata,
) -> None:
    spec.config_path.parent.mkdir(parents=True, exist_ok=True)
    spec.config_path.write_text(
        render_eval_config(
            spec.config,
            config=config,
            metadata=metadata,
            candidate_id=spec.candidate_id,
        ),
        encoding="utf-8",
    )


def analyzer_command(
    config: TunerConfig,
    *,
    eval_config_path: Path,
    report_path: Path,
) -> list[str]:
    command = [
        str(config.eval_vs_exact),
        "--labels",
        str(config.labels),
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


def parse_metric(stdout: str, key: str) -> int:
    match = re.search(rf"\b{re.escape(key)}=(\d+)\b", stdout)
    if not match:
        raise ScriptError(f"analyzer stdout missing required metric: {key}")
    return int(match.group(1))


def parse_optional_metric(stdout: str, key: str) -> int | None:
    match = re.search(rf"\b{re.escape(key)}=(-?\d+)\b", stdout)
    return int(match.group(1)) if match else None


def parse_analyzer_stdout(stdout: str) -> AnalyzerMetrics:
    return AnalyzerMetrics(
        records_read=parse_metric(stdout, "records_read"),
        analyzed=parse_metric(stdout, "analyzed"),
        sign_agreements=parse_metric(stdout, "sign_agreements"),
        wrong_direction=parse_metric(stdout, "wrong_direction"),
        high_confidence_wrong_direction=parse_metric(
            stdout,
            "high_confidence_wrong_direction",
        ),
        **{key: parse_optional_metric(stdout, key) for key in MOVE_RANK_METRIC_KEYS},
    )


def objective(metrics: AnalyzerMetrics, config: TunerConfig) -> int:
    return (
        metrics.sign_agreements
        - config.wrong_direction_penalty * metrics.wrong_direction
        - config.high_confidence_penalty * metrics.high_confidence_wrong_direction
    )


def write_log(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run_analyzer_result(
    *,
    candidate_id: str,
    round_index: int,
    eval_config: EvalConfig,
    config_path: Path,
    report_path: Path,
    log_path: Path,
    command: list[str],
    config: TunerConfig,
    changed_key: str | None,
    delta: int | None,
) -> CandidateResult:
    if config.dry_run:
        write_log(log_path, f"dry run\ncommand: {quote_command(command)}\n")
        return CandidateResult(
            candidate_id=candidate_id,
            round_index=round_index,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            changed_key=changed_key,
            delta=delta,
            status="planned",
        )

    try:
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
    except OSError as exc:
        output = f"command: {quote_command(command)}\nfailed to start: {exc}\n"
        write_log(log_path, output)
        return CandidateResult(
            candidate_id=candidate_id,
            round_index=round_index,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            changed_key=changed_key,
            delta=delta,
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
        return CandidateResult(
            candidate_id=candidate_id,
            round_index=round_index,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            changed_key=changed_key,
            delta=delta,
            status="failed",
            exit_code=completed.returncode,
            error=completed.stderr.strip(),
        )

    try:
        metrics = parse_analyzer_stdout(completed.stdout)
    except ScriptError as exc:
        return CandidateResult(
            candidate_id=candidate_id,
            round_index=round_index,
            config_path=config_path,
            report_path=report_path,
            log_path=log_path,
            command=command,
            config=eval_config,
            changed_key=changed_key,
            delta=delta,
            status="failed",
            exit_code=completed.returncode,
            error=str(exc),
        )

    return CandidateResult(
        candidate_id=candidate_id,
        round_index=round_index,
        config_path=config_path,
        report_path=report_path,
        log_path=log_path,
        command=command,
        config=eval_config,
        changed_key=changed_key,
        delta=delta,
        status="passed",
        metrics=metrics,
        objective=objective(metrics, config),
        exit_code=completed.returncode,
    )


def result_sort_key(result: CandidateResult) -> tuple[int, int, int, int, str]:
    metrics = result.metrics
    return (
        -(result.objective if result.objective is not None else -10**18),
        -(metrics.sign_agreements if metrics else -1),
        metrics.wrong_direction if metrics else 10**18,
        metrics.high_confidence_wrong_direction if metrics else 10**18,
        result.candidate_id,
    )


def successful_results(results: list[CandidateResult]) -> list[CandidateResult]:
    return [result for result in results if result.metrics is not None]


def ranked_successes(results: list[CandidateResult]) -> list[CandidateResult]:
    return sorted(successful_results(results), key=result_sort_key)


def changed_entries(candidate: EvalConfig, base: EvalConfig) -> list[tuple[str, int, int]]:
    base_values = base.values()
    changes = []
    for entry in candidate.entries:
        base_value = base_values.get(entry.key)
        if base_value is not None and base_value != entry.value:
            changes.append((entry.key, base_value, entry.value))
    return changes


def result_cells(result: CandidateResult) -> tuple[str, str, str, str, str]:
    if result.metrics is None or result.objective is None:
        return ("n/a", "n/a", "n/a", "n/a", "n/a")
    return (
        str(result.objective),
        str(result.metrics.analyzed),
        str(result.metrics.sign_agreements),
        str(result.metrics.wrong_direction),
        str(result.metrics.high_confidence_wrong_direction),
    )


def move_rank_cells(metrics: AnalyzerMetrics | None) -> list[str]:
    if metrics is None:
        return ["n/a"] * len(MOVE_RANK_METRIC_KEYS)
    return [
        str(value) if (value := getattr(metrics, key)) is not None else "n/a"
        for key in MOVE_RANK_METRIC_KEYS
    ]


def render_report(
    *,
    config: TunerConfig,
    metadata: Metadata,
    base_config: EvalConfig,
    selected_keys: list[str],
    results: list[CandidateResult],
    generated_counts: list[tuple[int, int, int]],
) -> str:
    ranked = ranked_successes(results)
    base_result = next(result for result in results if result.candidate_id == "base")
    best = ranked[0] if ranked else None

    lines = [
        "# Eval Config Tuner Report",
        "",
        "No strength claim. This is a diagnostic exact-label fitting experiment only.",
        "",
        "## Metadata",
        "",
        f"- timestamp: `{metadata.timestamp}`",
        f"- git_sha: `{metadata.git_sha}`",
        f"- command: `{quote_command(config.invocation) if config.invocation else 'unknown'}`",
        f"- labels_path: `{config.labels}`",
        f"- labels_sha256: `{metadata.labels_sha256}`",
        f"- base_config_path: `{config.base_config}`",
        f"- base_config_sha256: `{metadata.base_config_sha256}`",
        f"- eval_vs_exact_path: `{config.eval_vs_exact}`",
        f"- rounds: `{config.rounds}`",
        f"- step: `{config.step}`",
        f"- max_candidates: `{config.max_candidates}`",
        f"- seed: `{config.seed}`",
        f"- selected_keys: `{','.join(selected_keys)}`",
        f"- objective_formula: `{OBJECTIVE_FORMULA}`",
        f"- wrong_direction_penalty: `{config.wrong_direction_penalty}`",
        f"- high_confidence_penalty: `{config.high_confidence_penalty}`",
        f"- high_confidence_threshold: `{config.high_confidence_threshold}`",
        f"- move_rank_analysis: `{str(config.move_rank_analysis).lower()}`",
        "",
        "## Results",
        "",
        "| Rank | Candidate | Objective | Analyzed | Sign Agreements | Wrong Direction | High Confidence Wrong Direction | Report |",
        "|---|---|---:|---:|---:|---:|---:|---|",
    ]

    for rank, result in enumerate(ranked, start=1):
        objective_text, analyzed, sign_agreements, wrong_direction, high_confidence = result_cells(
            result
        )
        lines.append(
            f"| {rank} | `{result.candidate_id}` | {objective_text} | {analyzed} | "
            f"{sign_agreements} | {wrong_direction} | {high_confidence} | "
            f"`{result.report_path}` |"
        )
    if not ranked:
        lines.append("| - | no successful candidates | n/a | n/a | n/a | n/a | n/a | n/a |")

    if config.move_rank_analysis:
        lines.extend(
            [
                "",
                "## Move-Rank Metrics",
                "",
                "These metrics are recorded from `othello_eval_vs_exact --move-rank-analysis` for diagnostics only. The tuner objective still uses the sign-agreement formula above.",
                "",
                "| Candidate | Records With Scores | Missing Scores | No Legal Root Moves | Analyzed | Top Group Exact-Best | Top Group Non-Best | Exact-Best Rank Sum | Eval Gap Sum | Exact Gap Sum | Report |",
                "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
            ]
        )
        for result in ranked:
            cells = move_rank_cells(result.metrics)
            lines.append(
                f"| `{result.candidate_id}` | "
                + " | ".join(cells)
                + f" | `{result.report_path}` |"
            )
        if not ranked:
            lines.append("| - | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a | n/a |")

    lines.extend(["", "## Best Candidate", ""])
    if best is None:
        lines.append("- no successful candidate")
    else:
        lines.extend(
            [
                f"- path: `{best.config_path}`",
                f"- objective: `{best.objective}`",
                "- diff_from_base:",
            ]
        )
        changes = changed_entries(best.config, base_config)
        if not changes:
            lines.append("  - none")
        else:
            for key, old, new in changes:
                lines.append(f"  - `{key}`: `{old}` -> `{new}`")

    base_objective, base_analyzed, base_sign, base_wrong, base_high = result_cells(base_result)
    lines.extend(
        [
            "",
            "## Base Result",
            "",
            f"- objective: `{base_objective}`",
            f"- analyzed: `{base_analyzed}`",
            f"- sign_agreements: `{base_sign}`",
            f"- wrong_direction: `{base_wrong}`",
            f"- high_confidence_wrong_direction: `{base_high}`",
            f"- report: `{base_result.report_path}`",
            "",
            "## Candidate Diffs",
            "",
        ]
    )

    for result in ranked[:10]:
        lines.append(f"### {result.candidate_id}")
        changes = changed_entries(result.config, base_config)
        if not changes:
            lines.append("- no changes from base")
        else:
            for key, old, new in changes:
                lines.append(f"- `{key}`: `{old}` -> `{new}`")
        lines.append("")

    lines.extend(["## Candidate Generation", ""])
    for round_index, generated, selected in generated_counts:
        lines.append(
            f"- round {round_index}: generated `{generated}`, selected `{selected}` candidates"
        )

    lines.extend(["", "## Commands", ""])
    for result in results:
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

    failures = [result for result in results if result.status == "failed"]
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
            "- The objective is a diagnostic heuristic, not Elo or a strength claim.",
            "- This report is not a default-promotion gate or recommendation.",
            "- Results depend on the input exact-label distribution.",
            "- Random playout labels may not be representative training data.",
            "- Move-rank analysis is root move-quality evidence only; missing `move_scores` are reported as a caveat, not a workflow failure.",
            "- Generated configs are local experiment artifacts under `runs/`.",
            "- Follow-up validation should use held-out exact labels, search bench, match runner or base/head comparison, and external sanity when appropriate.",
        ]
    )
    return "\n".join(lines) + "\n"


def write_summary_tsv(path: Path, results: list[CandidateResult]) -> None:
    lines = [
        "rank\tcandidate\tstatus\tobjective\tanalyzed\tsign_agreements\twrong_direction\thigh_confidence_wrong_direction\t"
        + "\t".join(MOVE_RANK_METRIC_KEYS)
        + "\tconfig\treport"
    ]
    ranked = ranked_successes(results)
    ranked_ids = {result.candidate_id: rank for rank, result in enumerate(ranked, start=1)}
    for result in results:
        objective_text, analyzed, sign_agreements, wrong_direction, high_confidence = result_cells(
            result
        )
        lines.append(
            "\t".join(
                [
                    str(ranked_ids.get(result.candidate_id, "")),
                    result.candidate_id,
                    result.status,
                    objective_text,
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


def run_tuner(config: TunerConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    (config.out_dir / "configs").mkdir(parents=True, exist_ok=True)
    (config.out_dir / "reports").mkdir(parents=True, exist_ok=True)
    (config.out_dir / "logs").mkdir(parents=True, exist_ok=True)

    base_config = load_eval_config(config.base_config)
    selected_keys = select_tunable_keys(base_config, config)
    metadata = collect_metadata(config)
    results: list[CandidateResult] = []
    generated_counts: list[tuple[int, int, int]] = []
    seen_signatures = {base_config.signature()}

    base_report = config.out_dir / "reports" / "base.md"
    base_log = config.out_dir / "logs" / "base.log"
    base_command = analyzer_command(config, eval_config_path=config.base_config, report_path=base_report)
    results.append(
        run_analyzer_result(
            candidate_id="base",
            round_index=0,
            eval_config=base_config,
            config_path=config.base_config,
            report_path=base_report,
            log_path=base_log,
            command=base_command,
            config=config,
            changed_key=None,
            delta=None,
        )
    )

    center = base_config
    next_candidate_index = 1
    for round_index in range(1, config.rounds + 1):
        specs, generated_count, next_candidate_index = generate_round_candidates(
            center,
            selected_keys,
            config,
            round_index=round_index,
            next_candidate_index=next_candidate_index,
            existing_signatures=seen_signatures,
        )
        generated_counts.append((round_index, generated_count, len(specs)))
        round_results: list[CandidateResult] = []
        for spec in specs:
            seen_signatures.add(spec.config.signature())
            write_candidate_config(spec, config=config, metadata=metadata)
            command = analyzer_command(
                config,
                eval_config_path=spec.config_path,
                report_path=spec.report_path,
            )
            result = run_analyzer_result(
                candidate_id=spec.candidate_id,
                round_index=spec.round_index,
                eval_config=spec.config,
                config_path=spec.config_path,
                report_path=spec.report_path,
                log_path=spec.log_path,
                command=command,
                config=config,
                changed_key=spec.changed_key,
                delta=spec.delta,
            )
            round_results.append(result)
            results.append(result)

        round_successes = ranked_successes(round_results)
        if round_successes:
            best_round = round_successes[0]
            best_so_far = ranked_successes(results)[0]
            if best_round.candidate_id == best_so_far.candidate_id:
                center = best_round.config

    report = render_report(
        config=config,
        metadata=metadata,
        base_config=base_config,
        selected_keys=selected_keys,
        results=results,
        generated_counts=generated_counts,
    )
    (config.out_dir / "tuner_report.md").write_text(report, encoding="utf-8")
    write_summary_tsv(config.out_dir / "summary.tsv", results)

    failed = any(result.status == "failed" for result in results)
    if failed and not config.allow_failures:
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_tuner(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
