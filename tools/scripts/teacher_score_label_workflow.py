#!/usr/bin/env python3
"""Generate teacher_score_label.v1 JSONL from board9 positions or teacher labels."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from common import ScriptError, quote_command, read_jsonl, sha256_file
from dataset_paths import resolve_dataset_root, resolve_path_reference
from external_teacher_label_workflow import parse_board9_positions
from pattern_training.board9 import board_key, empty_count, legal_moves_for_board, normalize_move


SCHEMA_NAME = "teacher_score_label.v1"
SCORE_KIND = "teacher_search_score"
SCORE_PERSPECTIVE = "side_to_move"
SCORE_KEY = "teacher_score_side_to_move"
DEFAULT_ENGINE_NAME = "teacher"
DEFAULT_DEPTH = 0
SPLITS = ("train", "validation", "holdout")
REPO_ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class SourcePosition:
    index: int
    board: str
    position_id: str
    position_split: str
    source_bucket: str
    source: str
    source_line: int | None = None


@dataclass(frozen=True)
class WorkflowConfig:
    teacher_labels: Path | None
    positions: Path | None
    out_dir: Path
    teacher_engine: str
    teacher_depth: int
    teacher_command: list[str]
    max_rows: int | None
    split: str
    allow_partial: bool
    seed: int | None
    invocation: list[str] = field(default_factory=list)

    @property
    def labels_path(self) -> Path:
        return self.out_dir / "labels.jsonl"

    @property
    def summary_path(self) -> Path:
        return self.out_dir / "summary.json"

    @property
    def report_path(self) -> Path:
        return self.out_dir / "report.md"

    @property
    def commands_path(self) -> Path:
        return self.out_dir / "commands.sh"

    @property
    def manifest_path(self) -> Path:
        return self.out_dir / "manifest.json"


def parse_non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a non-negative integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be a non-negative integer")
    return parsed


def default_out_dir() -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("runs") / "teacher-score-labels" / timestamp


def _split_command_args(values: list[str]) -> tuple[list[str], list[str]]:
    if "--teacher-score-command" not in values:
        raise ScriptError("--teacher-score-command is required")
    index = values.index("--teacher-score-command")
    workflow_args = values[:index]
    command_args = values[index + 1 :]
    if not command_args or command_args[0] != "--" or len(command_args) == 1:
        raise ScriptError("--teacher-score-command must be followed by '--' and a command")
    return workflow_args, command_args[1:]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    values = list(sys.argv[1:] if argv is None else argv)
    workflow_args, command = _split_command_args(values)

    parser = argparse.ArgumentParser(
        description="Generate teacher_score_label.v1 labels from teacher labels or board9 positions."
    )
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument("--teacher-labels", help="teacher_label.v1 JSONL input")
    input_group.add_argument("--positions", help="board9 text input")
    parser.add_argument("--dataset-root")
    output_group = parser.add_mutually_exclusive_group()
    output_group.add_argument(
        "--out-dir",
        help="local output directory under runs/ (default: runs/teacher-score-labels/<timestamp>)",
    )
    output_group.add_argument(
        "--dataset-output",
        help="output directory relative to the shared dataset root",
    )
    parser.add_argument("--teacher-score-engine-name", default=DEFAULT_ENGINE_NAME)
    parser.add_argument("--teacher-score-depth", type=parse_non_negative_int, default=DEFAULT_DEPTH)
    parser.add_argument("--max-rows", type=parse_non_negative_int)
    parser.add_argument("--split", choices=(*SPLITS, "all"), default="all")
    parser.add_argument("--allow-partial", action="store_true")
    parser.add_argument("--seed", type=parse_non_negative_int)
    args = parser.parse_args(workflow_args)
    args.teacher_command = command
    return args


def _ensure_runs_out_dir(path: Path) -> Path:
    candidate = path
    if candidate.is_absolute():
        try:
            candidate.relative_to(REPO_ROOT / "runs")
        except ValueError as exc:
            raise ScriptError("--out-dir must be under repository runs/") from exc
        return candidate
    if not candidate.parts or candidate.parts[0] != "runs":
        raise ScriptError("--out-dir must be under runs/; use --dataset-output for shared artifacts")
    return candidate


def _dataset_output_dir(relative: str, *, dataset_root: str | None) -> Path:
    root = resolve_dataset_root(dataset_root, require_exists=False).path
    path = Path(relative)
    if path.is_absolute() or any(part == ".." for part in path.parts):
        raise ScriptError("--dataset-output must be relative to the dataset root and must not contain '..'")
    return root / path


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> WorkflowConfig:
    out_dir = (
        _dataset_output_dir(args.dataset_output, dataset_root=args.dataset_root)
        if args.dataset_output
        else _ensure_runs_out_dir(Path(args.out_dir) if args.out_dir else default_out_dir())
    )
    return WorkflowConfig(
        teacher_labels=(
            resolve_path_reference(args.teacher_labels, explicit_root=args.dataset_root)
            if args.teacher_labels
            else None
        ),
        positions=(
            resolve_path_reference(args.positions, explicit_root=args.dataset_root)
            if args.positions
            else None
        ),
        out_dir=out_dir,
        teacher_engine=args.teacher_score_engine_name,
        teacher_depth=args.teacher_score_depth,
        teacher_command=list(args.teacher_command),
        max_rows=args.max_rows,
        split=args.split,
        allow_partial=args.allow_partial,
        seed=args.seed,
        invocation=invocation or [],
    )


def _record_board(record: dict[str, Any]) -> str | None:
    board = record.get("board")
    if isinstance(board, str):
        return board
    board_text = record.get("board_text")
    if isinstance(board_text, str):
        return board_text
    return None


def _record_split(record: dict[str, Any]) -> str:
    for key in ("position_split", "split"):
        value = record.get(key)
        if isinstance(value, str) and value:
            return value
    return "unspecified"


def load_teacher_label_positions(path: Path) -> list[SourcePosition]:
    positions: list[SourcePosition] = []
    for index, record in enumerate(read_jsonl(path, require_object=True)):
        board = _record_board(record)
        if board is None:
            continue
        positions.append(
            SourcePosition(
                index=index,
                board=board,
                position_id=str(record.get("position_id") or f"row-{index:06d}"),
                position_split=_record_split(record),
                source_bucket=str(record.get("source_bucket") or "unspecified"),
                source=str(path),
            )
        )
    return positions


def load_board9_positions(path: Path) -> list[SourcePosition]:
    text = path.read_text(encoding="utf-8")
    parsed = parse_board9_positions(text, source_name=str(path))
    return [
        SourcePosition(
            index=position.position_index,
            board=position.board_text or "",
            position_id=position.name or f"position-{position.position_index:06d}",
            position_split=position.metadata.get("split", "unspecified"),
            source_bucket=position.metadata.get("source_bucket", "board9"),
            source=str(path),
            source_line=position.source_line,
        )
        for position in parsed
    ]


def phase_for_board(board: str) -> str:
    occupied = 64 - empty_count(board)
    if occupied <= 20:
        return "opening"
    if occupied <= 44:
        return "midgame"
    return "late"


def _valid_int(value: Any) -> bool:
    return not isinstance(value, bool) and isinstance(value, int)


def parse_engine_scores(payload: Any) -> tuple[dict[str, int], int]:
    scores: dict[str, int] = {}
    invalid_scores = 0
    if not isinstance(payload, dict):
        raise ScriptError("teacher score command must output a JSON object")
    root_scores = payload.get("root_scores")
    if isinstance(root_scores, dict):
        for raw_move, raw_score in root_scores.items():
            move = normalize_move(raw_move)
            if move is None or not _valid_int(raw_score):
                invalid_scores += 1
                continue
            scores[move] = raw_score
    move_scores = payload.get("move_scores")
    if isinstance(move_scores, list):
        for item in move_scores:
            if not isinstance(item, dict):
                invalid_scores += 1
                continue
            move = normalize_move(item.get("move"))
            raw_score = item.get(SCORE_KEY)
            if raw_score is None:
                raw_score = item.get("score")
            if move is None or not _valid_int(raw_score):
                invalid_scores += 1
                continue
            scores[move] = raw_score
    return scores, invalid_scores


def request_teacher_scores(config: WorkflowConfig, board: str) -> tuple[dict[str, int], int, str | None]:
    completed = subprocess.run(
        config.teacher_command,
        input=board,
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        return {}, 0, f"command exited {completed.returncode}: {completed.stderr.strip()}"
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        return {}, 0, f"invalid JSON from teacher score command: {exc.msg}"
    try:
        scores, invalid_scores = parse_engine_scores(payload)
    except ScriptError as exc:
        return {}, 0, str(exc)
    return scores, invalid_scores, None


def _empty_group() -> dict[str, int]:
    return {"rows": 0, "complete_rows": 0, "partial_rows": 0, "failed_rows": 0}


def _bump_group(summary: dict[str, dict[str, int]], key: str, status: str) -> None:
    group = summary.setdefault(key, _empty_group())
    group["rows"] += 1
    if status == "complete":
        group["complete_rows"] += 1
    elif status == "partial":
        group["partial_rows"] += 1
    else:
        group["failed_rows"] += 1


def build_output_row(
    config: WorkflowConfig,
    position: SourcePosition,
    legal_moves: set[str],
    scores: dict[str, int],
    *,
    status: str,
) -> dict[str, Any]:
    return {
        "schema": SCHEMA_NAME,
        "board": position.board,
        "position_id": position.position_id,
        "position_split": position.position_split,
        "source_bucket": position.source_bucket,
        "score_kind": SCORE_KIND,
        "score_perspective": SCORE_PERSPECTIVE,
        "teacher_engine": config.teacher_engine,
        "teacher_depth": config.teacher_depth,
        "not_exact": True,
        "score_status": status,
        "legal_moves": sorted(legal_moves),
        "move_scores": [
            {"move": move, SCORE_KEY: scores[move]}
            for move in sorted(legal_moves)
            if move in scores
        ],
    }


def collect_metadata() -> dict[str, str]:
    completed = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=False)
    return {
        "generated_at": dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "git_sha": completed.stdout.strip() if completed.returncode == 0 else "unknown",
    }


def _input_manifest(config: WorkflowConfig) -> dict[str, Any]:
    input_path = config.teacher_labels or config.positions
    manifest: dict[str, Any] = {
        "teacher_labels": str(config.teacher_labels) if config.teacher_labels else None,
        "positions": str(config.positions) if config.positions else None,
    }
    if input_path is not None and input_path.exists():
        manifest["input_sha256"] = sha256_file(input_path)
    return manifest


def summarize_command(config: WorkflowConfig) -> dict[str, Any]:
    return {
        "teacher_score_command": config.teacher_command,
        "teacher_score_command_quoted": quote_command(config.teacher_command),
        "invocation": config.invocation,
    }


def run_workflow(config: WorkflowConfig) -> int:
    if not config.teacher_command:
        raise ScriptError("--teacher-score-command is required")
    input_positions = (
        load_teacher_label_positions(config.teacher_labels)
        if config.teacher_labels is not None
        else load_board9_positions(config.positions)  # type: ignore[arg-type]
    )
    if config.split != "all":
        input_positions = [position for position in input_positions if position.position_split == config.split]
    if config.max_rows is not None:
        input_positions = input_positions[: config.max_rows]

    config.out_dir.mkdir(parents=True, exist_ok=True)
    metadata = collect_metadata()
    rows: list[dict[str, Any]] = []
    seen_boards: set[str] = set()
    duplicate_board_count = 0
    skipped_rows = 0
    failed_rows = 0
    partial_rows = 0
    complete_rows = 0
    legal_moves_total = 0
    present_scores_total = 0
    invalid_score_count = 0
    illegal_move_score_count = 0
    legal_move_count_distribution: Counter[str] = Counter()
    by_split: dict[str, dict[str, int]] = {}
    by_phase: dict[str, dict[str, int]] = {}
    by_source_bucket: dict[str, dict[str, int]] = {}
    failure_reasons: Counter[str] = Counter()

    for position in input_positions:
        key = board_key(position.board)
        if key in seen_boards:
            duplicate_board_count += 1
            skipped_rows += 1
            failure_reasons["duplicate_board_skipped"] += 1
            continue
        seen_boards.add(key)

        phase = phase_for_board(position.board)
        try:
            legal_moves = legal_moves_for_board(position.board)
        except ScriptError as exc:
            failed_rows += 1
            failure_reasons[f"invalid_board: {exc}"] += 1
            continue
        scores, invalid_scores, error = request_teacher_scores(config, position.board)
        invalid_score_count += invalid_scores
        legal_moves_total += len(legal_moves)
        legal_move_count_distribution[str(len(legal_moves))] += 1
        if error is not None:
            status = "failed"
            failed_rows += 1
            failure_reasons[error] += 1
            _bump_group(by_split, position.position_split, status)
            _bump_group(by_phase, phase, status)
            _bump_group(by_source_bucket, position.source_bucket, status)
            continue

        illegal_moves = sorted(set(scores) - legal_moves)
        illegal_move_score_count += len(illegal_moves)
        legal_scores = {move: score for move, score in scores.items() if move in legal_moves}
        present_scores_total += len(legal_scores)
        if len(legal_scores) == len(legal_moves):
            status = "complete"
            complete_rows += 1
        elif legal_scores:
            status = "partial"
            partial_rows += 1
        else:
            status = "failed"
            failed_rows += 1
        if status == "partial" and not config.allow_partial:
            skipped_rows += 1
            failure_reasons["partial_row_skipped"] += 1
        elif status == "failed":
            failure_reasons["no_legal_scores"] += 1
        else:
            rows.append(build_output_row(config, position, legal_moves, legal_scores, status=status))

        if illegal_moves:
            failure_reasons["illegal_move_scores_ignored"] += len(illegal_moves)
        _bump_group(by_split, position.position_split, status)
        _bump_group(by_phase, phase, status)
        _bump_group(by_source_bucket, position.source_bucket, status)

    with config.labels_path.open("w", encoding="utf-8") as output:
        for row in rows:
            output.write(json.dumps(row, sort_keys=True) + "\n")

    accepted_rows = len(rows)
    summary: dict[str, Any] = {
        "schema": "teacher_score_label_workflow_summary.v1",
        **metadata,
        "input_rows": len(input_positions),
        "accepted_rows": accepted_rows,
        "output_rows": len(rows),
        "complete_rows": complete_rows,
        "partial_rows": partial_rows,
        "failed_rows": failed_rows,
        "skipped_rows": skipped_rows,
        "legal_moves_total": legal_moves_total,
        "present_scores_total": present_scores_total,
        "score_coverage_rate": (
            present_scores_total / legal_moves_total if legal_moves_total else 0.0
        ),
        "by_split": dict(sorted(by_split.items())),
        "by_phase": dict(sorted(by_phase.items())),
        "by_source_bucket": dict(sorted(by_source_bucket.items())),
        "legal_move_count_distribution": dict(sorted(legal_move_count_distribution.items())),
        "duplicate_board_count": duplicate_board_count,
        "invalid_score_count": invalid_score_count,
        "illegal_move_score_count": illegal_move_score_count,
        "failure_reasons": dict(sorted(failure_reasons.items())),
        "command_manifest": summarize_command(config),
        "teacher_engine": config.teacher_engine,
        "teacher_depth": config.teacher_depth,
        "score_kind": SCORE_KIND,
        "score_perspective": SCORE_PERSPECTIVE,
        "allow_partial": config.allow_partial,
        "no_strength_claim": True,
    }
    config.summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_report(config, summary)
    write_manifest(config, summary)
    write_commands(config)
    return 0


def write_manifest(config: WorkflowConfig, summary: dict[str, Any]) -> None:
    manifest = {
        "schema": "teacher_score_label_workflow_manifest.v1",
        "input": _input_manifest(config),
        "outputs": {
            "labels": str(config.labels_path),
            "summary": str(config.summary_path),
            "report": str(config.report_path),
            "commands": str(config.commands_path),
        },
        "summary": {
            "output_rows": summary["output_rows"],
            "complete_rows": summary["complete_rows"],
            "partial_rows": summary["partial_rows"],
            "failed_rows": summary["failed_rows"],
            "no_strength_claim": True,
        },
        "command_manifest": summarize_command(config),
    }
    config.manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_commands(config: WorkflowConfig) -> None:
    lines = [
        "#!/usr/bin/env sh",
        "# Re-run command for teacher_score_label.v1 workflow.",
        "# This workflow generates dataset labels only and makes no strength claim.",
    ]
    if config.invocation:
        lines.append(quote_command(config.invocation))
    else:
        lines.append("# invocation was not recorded")
    config.commands_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_report(config: WorkflowConfig, summary: dict[str, Any]) -> None:
    lines = [
        "# Teacher Score Label Workflow",
        "",
        "This report describes teacher search score label generation only. It does not train, "
        "promote, or claim playing strength.",
        "",
        "## Summary",
        "",
        f"- teacher_engine: `{config.teacher_engine}`",
        f"- teacher_depth: `{config.teacher_depth}`",
        f"- score_kind: `{SCORE_KIND}`",
        f"- no_strength_claim: `{summary['no_strength_claim']}`",
        f"- input_rows: `{summary['input_rows']}`",
        f"- output_rows: `{summary['output_rows']}`",
        f"- complete_rows: `{summary['complete_rows']}`",
        f"- partial_rows: `{summary['partial_rows']}`",
        f"- failed_rows: `{summary['failed_rows']}`",
        f"- skipped_rows: `{summary['skipped_rows']}`",
        f"- score_coverage_rate: `{summary['score_coverage_rate']}`",
        "",
        "## Output Schema",
        "",
        "- rows use `teacher_score_label.v1`.",
        "- move scores use `teacher_score_side_to_move`.",
        "- rows set `not_exact=true` and do not emit `exact_score_side_to_move`.",
        "- partial rows are written only when `--allow-partial` is set.",
        "",
        "## Files",
        "",
        f"- labels: `{config.labels_path}`",
        f"- summary: `{config.summary_path}`",
        f"- manifest: `{config.manifest_path}`",
        f"- commands: `{config.commands_path}`",
        "",
    ]
    config.report_path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    try:
        parsed = parse_args(argv)
        config = config_from_args(
            parsed,
            invocation=["python3", "tools/scripts/teacher_score_label_workflow.py", *(argv or sys.argv[1:])],
        )
        return run_workflow(config)
    except ScriptError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
