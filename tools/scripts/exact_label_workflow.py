#!/usr/bin/env python3
"""Sample positions, dump exact labels, and optionally run eval-vs-exact analysis."""

from __future__ import annotations

import argparse
import collections
import datetime as dt
import hashlib
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from common import ScriptError, parse_csv_values, quote_command
from dataset_paths import resolve_path_references
from pattern_training.board9 import (
    board_key,
    legal_moves_for_board,
    normalize_move,
    occupied_count,
)


DEFAULT_SEED = 20260531
DEFAULT_COUNT = 20
DEFAULT_TARGET_EMPTIES = "8,10,12"
DEFAULT_MAX_EMPTIES = 14
REPO_ROOT = Path(__file__).resolve().parents[2]
PHASES = ("opening", "midgame", "late")
DEFAULT_PHASE_CUTOFFS = (20, 44)


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
    allow_shortage: bool
    position_sampler: Path
    exact_label_dump: Path
    eval_vs_exact: Path
    dataset_root: str | None
    exact_phase_targets: dict[str, int]
    exact_source_teacher_labels: tuple[Path, ...]
    exact_require_complete_move_scores: bool
    exact_phase_balanced_out_dir: Path | None
    exact_split: str
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


@dataclass(frozen=True)
class PhaseCutoffs:
    opening_max_occupied: int
    midgame_max_occupied: int


@dataclass(frozen=True)
class TeacherCandidate:
    record: dict[str, Any]
    path: Path
    line_number: int
    board_text: str
    board_key: str
    split: str
    phase: str
    occupied_count: int
    stable_key: str


@dataclass(frozen=True)
class PhaseExactSummary:
    phase_targets: dict[str, int]
    phase_selected_for_exact: dict[str, int]
    phase_exact_rows: dict[str, int]
    phase_complete_move_scores: dict[str, int]
    phase_shortage: dict[str, int]
    phase_complete_move_scores_shortage: dict[str, int]
    exact_duplicate_boards: int
    exact_runtime: dict[str, int | float]


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


def parse_phase_targets(value: str | None) -> dict[str, int]:
    targets = {phase: 0 for phase in PHASES}
    if value is None or not value.strip():
        return targets
    seen: set[str] = set()
    for segment in parse_csv_values(value, error_label="phase target map"):
        if "=" not in segment:
            raise argparse.ArgumentTypeError("phase targets must use PHASE=COUNT entries")
        phase, count_text = segment.split("=", 1)
        phase = phase.strip()
        if phase not in PHASES:
            known = ", ".join(PHASES)
            raise argparse.ArgumentTypeError(f"unknown phase {phase!r}; expected one of: {known}")
        if phase in seen:
            raise argparse.ArgumentTypeError(f"duplicate phase target: {phase}")
        seen.add(phase)
        try:
            count = int(count_text)
        except ValueError as exc:
            raise argparse.ArgumentTypeError("phase target counts must be non-negative integers") from exc
        if count < 0:
            raise argparse.ArgumentTypeError("phase target counts must be non-negative integers")
        targets[phase] = count
    return targets


def _line_key_value(line: str) -> tuple[str, str] | None:
    body = line.split("#", 1)[0].strip()
    if not body or "=" not in body:
        return None
    key, value = body.split("=", 1)
    return key.strip(), value.strip()


def _parse_int_key(entries: dict[str, str], key: str, default: int) -> int:
    value = entries.get(key)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError as exc:
        raise ScriptError(f"{key} must be an integer in eval config") from exc


def phase_cutoffs_from_eval_config(path: Path) -> PhaseCutoffs:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to read eval config {path}: {exc}") from exc
    entries: dict[str, str] = {}
    for line in text.splitlines():
        parsed = _line_key_value(line)
        if parsed is not None:
            key, value = parsed
            entries[key] = value
    opening, midgame = DEFAULT_PHASE_CUTOFFS
    return PhaseCutoffs(
        opening_max_occupied=_parse_int_key(entries, "opening_max_occupied", opening),
        midgame_max_occupied=_parse_int_key(entries, "midgame_max_occupied", midgame),
    )


def phase_for_occupied(value: int, cutoffs: PhaseCutoffs) -> str:
    if value <= cutoffs.opening_max_occupied:
        return "opening"
    if value <= cutoffs.midgame_max_occupied:
        return "midgame"
    return "late"


def resolve_runs_out_dir(path_text: str, *, option_name: str) -> Path:
    path = Path(path_text)
    if not path.is_absolute():
        path = REPO_ROOT / path
    resolved = path.resolve(strict=False)
    repo_runs = (REPO_ROOT / "runs").resolve(strict=False)
    try:
        resolved.relative_to(repo_runs)
    except ValueError as exc:
        raise ScriptError(f"{option_name} must be under repository runs/") from exc
    return path


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
    parser.add_argument("--dataset-root")
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
    parser.add_argument(
        "--allow-shortage",
        action="store_true",
        help="write partial phase-balanced exact outputs when requested coverage is short",
    )
    parser.add_argument(
        "--exact-phase-targets",
        type=parse_phase_targets,
        help="phase-aware exact targets, e.g. opening=100,midgame=100,late=100",
    )
    parser.add_argument(
        "--exact-source-teacher-labels",
        help="comma-separated teacher label paths or dataset: references for phase-aware exact sampling",
    )
    parser.add_argument(
        "--exact-require-complete-move-scores",
        action="store_true",
        help="require complete root move_scores coverage for each requested phase target",
    )
    parser.add_argument(
        "--exact-phase-balanced-out-dir",
        help="phase-aware exact output directory; must be under repository runs/",
    )
    parser.add_argument(
        "--exact-split",
        choices=("train", "validation", "holdout", "all"),
        default="train",
    )
    parser.add_argument("--position-sampler", help="override othello_position_sampler path")
    parser.add_argument("--exact-label-dump", help="override othello_exact_label_dump path")
    parser.add_argument("--eval-vs-exact", help="override othello_eval_vs_exact path")
    return parser.parse_args(argv)


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> WorkflowConfig:
    if args.max_empties > 64:
        raise ScriptError("--max-empties must be in [0, 64]")

    exact_phase_mode = any(
        (
            args.exact_phase_targets,
            args.exact_source_teacher_labels,
            args.exact_require_complete_move_scores,
            args.exact_phase_balanced_out_dir,
        )
    )
    exact_phase_targets = args.exact_phase_targets or {phase: 0 for phase in PHASES}
    exact_source_teacher_labels: tuple[Path, ...] = ()
    exact_phase_balanced_out_dir: Path | None = None
    if exact_phase_mode:
        if not args.exact_phase_targets:
            raise ScriptError("--exact-phase-targets is required for phase-aware exact sampling")
        if sum(exact_phase_targets.values()) <= 0:
            raise ScriptError("--exact-phase-targets must request at least one row")
        if not args.exact_source_teacher_labels:
            raise ScriptError("--exact-source-teacher-labels is required for phase-aware exact sampling")
        if not args.exact_phase_balanced_out_dir:
            raise ScriptError("--exact-phase-balanced-out-dir is required for phase-aware exact sampling")
        if not args.eval_config:
            raise ScriptError("--eval-config is required for phase-aware exact sampling")
        exact_source_teacher_labels = tuple(
            resolve_path_references(
                parse_csv_values(args.exact_source_teacher_labels, error_label="exact source teacher labels"),
                explicit_root=args.dataset_root,
            )
        )
        exact_phase_balanced_out_dir = resolve_runs_out_dir(
            args.exact_phase_balanced_out_dir,
            option_name="--exact-phase-balanced-out-dir",
        )

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
        allow_shortage=args.allow_shortage,
        position_sampler=Path(args.position_sampler)
        if args.position_sampler
        else default_tool(build_dir, "othello_position_sampler"),
        exact_label_dump=Path(args.exact_label_dump)
        if args.exact_label_dump
        else default_tool(build_dir, "othello_exact_label_dump"),
        eval_vs_exact=Path(args.eval_vs_exact)
        if args.eval_vs_exact
        else default_tool(build_dir, "othello_eval_vs_exact"),
        dataset_root=args.dataset_root,
        exact_phase_targets=exact_phase_targets,
        exact_source_teacher_labels=exact_source_teacher_labels,
        exact_require_complete_move_scores=args.exact_require_complete_move_scores,
        exact_phase_balanced_out_dir=exact_phase_balanced_out_dir,
        exact_split=args.exact_split,
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


def phase_exact_label_command(config: WorkflowConfig, positions_path: Path, labels_path: Path) -> list[str]:
    command = [
        str(config.exact_label_dump),
        "--input",
        str(positions_path),
        "--output",
        str(labels_path),
        "--max-empties",
        str(config.max_empties),
    ]
    if config.exact_require_complete_move_scores:
        command.append("--include-move-scores")
    return command


def board_text_from_record(record: dict[str, Any]) -> str | None:
    board = record.get("board")
    if isinstance(board, str):
        return board
    board_text = record.get("board_text")
    if isinstance(board_text, str):
        return board_text
    rows = record.get("rows")
    side = record.get("side_to_move", record.get("side"))
    if isinstance(rows, list) and isinstance(side, str) and len(rows) == 8:
        rendered = [str(row) for row in rows]
        return "\n".join(rendered) + f"\nside={side.strip().upper()[:1]}"
    return None


def record_split(record: dict[str, Any], *, path: Path, board: str, seed: int) -> str:
    value = record.get("position_split", record.get("split"))
    if isinstance(value, str) and value in {"train", "validation", "holdout"}:
        return value
    path_text = str(path)
    if "/validation/" in path_text or "validation" in path.stem:
        return "validation"
    if "/holdout/" in path_text or "holdout" in path.stem:
        return "holdout"
    digest = hashlib.sha256(f"{seed}:{board_key(board)}".encode("utf-8")).digest()
    bucket = int.from_bytes(digest[:8], "big") % 100
    if bucket < 70:
        return "train"
    if bucket < 85:
        return "validation"
    return "holdout"


def teacher_record_accepted(record: dict[str, Any]) -> bool:
    status = record.get("status")
    if isinstance(status, str) and status not in {"ok", "selected"}:
        return False
    if record.get("legal_move_valid") is False or record.get("move_token_valid") is False:
        return False
    move = normalize_move(record.get("move", record.get("teacher_move")))
    return move is not None


def read_teacher_candidates(config: WorkflowConfig, cutoffs: PhaseCutoffs) -> tuple[list[TeacherCandidate], int]:
    deduped: dict[str, TeacherCandidate] = {}
    duplicates = 0
    for path in config.exact_source_teacher_labels:
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError as exc:
            raise ScriptError(f"failed to read teacher labels {path}: {exc}") from exc
        for line_number, line in enumerate(lines, start=1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ScriptError(f"failed to parse JSONL {path}:{line_number}: {exc}") from exc
            if not isinstance(record, dict) or not teacher_record_accepted(record):
                continue
            board = board_text_from_record(record)
            if board is None:
                continue
            key = board_key(board)
            if key in deduped:
                duplicates += 1
                continue
            split = record_split(record, path=path, board=board, seed=config.seed)
            if config.exact_split != "all" and split != config.exact_split:
                continue
            occupied = occupied_count(board)
            phase = phase_for_occupied(occupied, cutoffs)
            stable_key = hashlib.sha256(
                f"{config.seed}:{phase}:{key}:{path}:{line_number}".encode("utf-8")
            ).hexdigest()
            deduped[key] = TeacherCandidate(
                record=record,
                path=path,
                line_number=line_number,
                board_text=board,
                board_key=key,
                split=split,
                phase=phase,
                occupied_count=occupied,
                stable_key=stable_key,
            )
    return list(deduped.values()), duplicates


def select_phase_candidates(
    candidates: list[TeacherCandidate],
    targets: dict[str, int],
) -> tuple[list[TeacherCandidate], dict[str, int], dict[str, int]]:
    by_phase: dict[str, list[TeacherCandidate]] = {phase: [] for phase in PHASES}
    for candidate in candidates:
        by_phase[candidate.phase].append(candidate)
    selected: list[TeacherCandidate] = []
    counts: dict[str, int] = {}
    shortages: dict[str, int] = {}
    for phase in PHASES:
        rows = sorted(by_phase[phase], key=lambda item: item.stable_key)
        target = targets[phase]
        chosen = rows[:target]
        selected.extend(chosen)
        counts[phase] = len(chosen)
        shortages[phase] = max(0, target - len(chosen))
    selected.sort(key=lambda item: (item.phase, item.stable_key))
    return selected, counts, shortages


def write_teacher_selected_jsonl(path: Path, selected: list[TeacherCandidate]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as output:
        for candidate in selected:
            payload = dict(candidate.record)
            payload.update(
                {
                    "board": candidate.board_text,
                    "phase": candidate.phase,
                    "occupied_count": candidate.occupied_count,
                    "position_split": candidate.split,
                    "source_path": str(candidate.path),
                    "source_line": candidate.line_number,
                }
            )
            output.write(json.dumps(payload, sort_keys=True) + "\n")


def write_exact_positions(path: Path, selected: list[TeacherCandidate]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as output:
        for index, candidate in enumerate(selected, start=1):
            output.write(f"# name: phase_exact_{index:06d}_{candidate.phase}\n")
            output.write(candidate.board_text.strip() + "\n\n")


def exact_board_key(record: dict[str, Any]) -> str | None:
    board = board_text_from_record(record)
    if board is not None:
        return board_key(board)
    return None


def exact_move_scores(record: dict[str, Any]) -> dict[str, Any]:
    value = record.get("move_scores")
    scores: dict[str, Any] = {}
    if isinstance(value, dict):
        for move, score in value.items():
            normalized = normalize_move(move)
            if normalized is not None:
                scores[normalized] = score
        return scores
    if not isinstance(value, list):
        return scores
    for item in value:
        if not isinstance(item, dict):
            continue
        normalized = normalize_move(item.get("move"))
        if normalized is not None:
            scores[normalized] = item.get("exact_score_side_to_move")
    return scores


def has_complete_move_scores(record: dict[str, Any], board: str) -> bool:
    legal_moves = legal_moves_for_board(board)
    scores = exact_move_scores(record)
    return bool(legal_moves) and legal_moves.issubset(scores.keys())


def analyze_exact_phase_labels(
    labels_path: Path,
    selected: list[TeacherCandidate],
    targets: dict[str, int],
    selection_shortage: dict[str, int],
    duplicate_teacher_boards: int,
) -> PhaseExactSummary:
    selected_by_board = {candidate.board_key: candidate for candidate in selected}
    seen_exact_boards: set[str] = set()
    exact_duplicates = duplicate_teacher_boards
    phase_exact_rows = {phase: 0 for phase in PHASES}
    phase_complete_move_scores = {phase: 0 for phase in PHASES}
    runtime_rows_elapsed = 0
    total_elapsed_ms = 0.0
    max_elapsed_ms = 0.0
    runtime_rows_nodes = 0
    total_nodes = 0
    max_nodes = 0
    try:
        lines = labels_path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ScriptError(f"failed to read exact labels {labels_path}: {exc}") from exc
    for line_number, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ScriptError(f"failed to parse exact labels {labels_path}:{line_number}: {exc}") from exc
        if not isinstance(record, dict):
            continue
        key = exact_board_key(record)
        if key is None or key not in selected_by_board:
            continue
        if key in seen_exact_boards:
            exact_duplicates += 1
            continue
        seen_exact_boards.add(key)
        candidate = selected_by_board[key]
        phase_exact_rows[candidate.phase] += 1
        if has_complete_move_scores(record, candidate.board_text):
            phase_complete_move_scores[candidate.phase] += 1
        elapsed_value = record.get("elapsed_ms", record.get("exact_elapsed_ms"))
        if isinstance(elapsed_value, int | float):
            runtime_rows_elapsed += 1
            total_elapsed_ms += float(elapsed_value)
            max_elapsed_ms = max(max_elapsed_ms, float(elapsed_value))
        nodes_value = record.get("nodes", record.get("exact_nodes"))
        if isinstance(nodes_value, int):
            runtime_rows_nodes += 1
            total_nodes += nodes_value
            max_nodes = max(max_nodes, nodes_value)
    phase_complete_shortage = {
        phase: max(0, targets[phase] - phase_complete_move_scores[phase])
        for phase in PHASES
    }
    return PhaseExactSummary(
        phase_targets=dict(targets),
        phase_selected_for_exact=collections.Counter(candidate.phase for candidate in selected),
        phase_exact_rows=phase_exact_rows,
        phase_complete_move_scores=phase_complete_move_scores,
        phase_shortage=dict(selection_shortage),
        phase_complete_move_scores_shortage=phase_complete_shortage,
        exact_duplicate_boards=exact_duplicates,
        exact_runtime={
            "rows_with_elapsed_ms": runtime_rows_elapsed,
            "total_elapsed_ms": total_elapsed_ms,
            "max_elapsed_ms": max_elapsed_ms,
            "rows_with_nodes": runtime_rows_nodes,
            "total_nodes": total_nodes,
            "max_nodes": max_nodes,
        },
    )


def phase_summary_to_json(config: WorkflowConfig, summary: PhaseExactSummary) -> dict[str, Any]:
    return {
        "schema": "phase_balanced_exact_workflow_summary.v1",
        "phase_targets": summary.phase_targets,
        "phase_selected_for_exact": {phase: summary.phase_selected_for_exact.get(phase, 0) for phase in PHASES},
        "phase_exact_rows": summary.phase_exact_rows,
        "phase_complete_move_scores": summary.phase_complete_move_scores,
        "phase_shortage": summary.phase_shortage,
        "phase_complete_move_scores_shortage": summary.phase_complete_move_scores_shortage,
        "exact_duplicate_boards": summary.exact_duplicate_boards,
        "exact_runtime": summary.exact_runtime,
        "exact_require_complete_move_scores": config.exact_require_complete_move_scores,
        "split": config.exact_split,
        "seed": config.seed,
        "max_empties": config.max_empties,
        "source_teacher_labels": [str(path) for path in config.exact_source_teacher_labels],
        "no_strength_claim": True,
        "default_promotion": False,
    }


def render_phase_exact_report(config: WorkflowConfig, summary: PhaseExactSummary, *, status: str) -> str:
    lines = [
        "# Phase-Balanced Exact Label Workflow",
        "",
        f"Status: {status}",
        "",
        "No strength claim. No default promotion recommendation.",
        "",
        "## Phase Exact Coverage",
        "",
        "| phase | target | selected_for_exact | exact_rows | complete_move_scores | shortage | complete_move_scores_shortage |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for phase in PHASES:
        lines.append(
            "| "
            f"{phase} | "
            f"{summary.phase_targets[phase]} | "
            f"{summary.phase_selected_for_exact.get(phase, 0)} | "
            f"{summary.phase_exact_rows[phase]} | "
            f"{summary.phase_complete_move_scores[phase]} | "
            f"{summary.phase_shortage[phase]} | "
            f"{summary.phase_complete_move_scores_shortage[phase]} |"
        )
    out_dir = config.exact_phase_balanced_out_dir or config.out_dir
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            f"- exact_phase_balanced: `{out_dir / 'exact_phase_balanced.jsonl'}`",
            f"- teacher_selected_for_exact: `{out_dir / 'teacher_selected_for_exact.jsonl'}`",
            f"- summary: `{out_dir / 'summary.json'}`",
            f"- exact log: `{out_dir / 'logs' / 'exact-label-dump.log'}`",
            "",
            "## Caveats",
            "",
            "- This is dataset/evidence infrastructure for PatternOnly diagnostics, not evaluator strength evidence.",
            "- Generated artifacts belong under repository `runs/` and should not be committed.",
            "- Default promotion remains false unless a separate strength-evidence PR changes that.",
        ]
    )
    return "\n".join(lines) + "\n"


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


def run_phase_balanced_exact_workflow(config: WorkflowConfig) -> int:
    out_dir = config.exact_phase_balanced_out_dir
    if out_dir is None:
        raise ScriptError("--exact-phase-balanced-out-dir is required for phase-aware exact sampling")
    cutoffs = phase_cutoffs_from_eval_config(config.eval_config) if config.eval_config else None
    if cutoffs is None:
        raise ScriptError("--eval-config is required for phase-aware exact sampling")

    candidates, duplicate_teacher_boards = read_teacher_candidates(config, cutoffs)
    selected, selected_counts, selection_shortage = select_phase_candidates(
        candidates,
        config.exact_phase_targets,
    )
    if any(selection_shortage.values()) and not config.allow_shortage:
        shortage_text = ", ".join(
            f"{phase}={selection_shortage[phase]}" for phase in PHASES if selection_shortage[phase]
        )
        raise ScriptError(f"phase-aware exact target shortage: {shortage_text}")

    out_dir.mkdir(parents=True, exist_ok=True)
    teacher_selected_path = out_dir / "teacher_selected_for_exact.jsonl"
    positions_path = out_dir / "positions.txt"
    labels_path = out_dir / "exact_phase_balanced.jsonl"
    summary_path = out_dir / "summary.json"
    report_path = out_dir / "report.md"
    logs_dir = out_dir / "logs"
    write_teacher_selected_jsonl(teacher_selected_path, selected)
    write_exact_positions(positions_path, selected)

    step = WorkflowStep(
        name="dump-phase-balanced-exact-labels",
        command=phase_exact_label_command(config, positions_path, labels_path),
        log_path=logs_dir / "exact-label-dump.log",
    )
    result = run_step(step, dry_run=config.dry_run)
    if result.status == "failed" and not config.allow_failures:
        summary = PhaseExactSummary(
            phase_targets=dict(config.exact_phase_targets),
            phase_selected_for_exact=selected_counts,
            phase_exact_rows={phase: 0 for phase in PHASES},
            phase_complete_move_scores={phase: 0 for phase in PHASES},
            phase_shortage=selection_shortage,
            phase_complete_move_scores_shortage=dict(config.exact_phase_targets),
            exact_duplicate_boards=duplicate_teacher_boards,
            exact_runtime={
                "rows_with_elapsed_ms": 0,
                "total_elapsed_ms": 0.0,
                "max_elapsed_ms": 0.0,
                "rows_with_nodes": 0,
                "total_nodes": 0,
                "max_nodes": 0,
            },
        )
        summary_path.write_text(
            json.dumps(phase_summary_to_json(config, summary), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        report_path.write_text(
            render_phase_exact_report(config, summary, status="completed with failures"),
            encoding="utf-8",
        )
        return 1

    if config.dry_run:
        labels_path.parent.mkdir(parents=True, exist_ok=True)
        labels_path.write_text("", encoding="utf-8")

    summary = analyze_exact_phase_labels(
        labels_path,
        selected,
        config.exact_phase_targets,
        selection_shortage,
        duplicate_teacher_boards,
    )
    complete_shortage_failed = (
        config.exact_require_complete_move_scores
        and any(summary.phase_complete_move_scores_shortage.values())
    )
    status = "completed with shortage" if any(selection_shortage.values()) else "completed"
    if complete_shortage_failed:
        status = "completed with complete-move-scores shortage"
    summary_path.write_text(
        json.dumps(phase_summary_to_json(config, summary), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    report_path.write_text(render_phase_exact_report(config, summary, status=status), encoding="utf-8")
    if complete_shortage_failed and not config.allow_shortage:
        return 1
    return 0


def run_workflow(config: WorkflowConfig) -> int:
    if config.exact_phase_balanced_out_dir is not None:
        return run_phase_balanced_exact_workflow(config)

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
