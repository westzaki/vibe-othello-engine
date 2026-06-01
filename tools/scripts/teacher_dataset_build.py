#!/usr/bin/env python3
"""Build reusable teacher/exact dataset artifacts under a dataset root."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from common import ScriptError, quote_command
from dataset_paths import DatasetRoot, resolve_dataset_root
import external_teacher_label_workflow as teacher_workflow


SCHEMA_MANIFEST = "teacher_dataset_manifest.v1"
SCHEMA_POSITION = "teacher_position.v1"
SCHEMA_SPLIT_MANIFEST = "teacher_split_manifest.v1"
DEFAULT_SPLIT_RATIOS = (70, 15, 15)
DEFAULT_SPLIT_SEED = 20260601
DEFAULT_SHARD_SIZE = 1000
DEFAULT_EXACT_MAX_EMPTIES = 14
DEFAULT_LABEL_JOBS = 1
DEFAULT_POSITION_LOG_MODE = "all"
REPO_ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class PositionRecord:
    position_id: str
    board_text: str
    empties: int
    source: str
    source_detail: str
    source_seed: int
    source_file: str
    source_line: int
    source_id: str | None
    git_sha: str
    split: str = ""


@dataclass(frozen=True)
class DuplicateRecord:
    position_id: str
    first_source_detail: str
    duplicate_source_detail: str


@dataclass(frozen=True)
class BuildConfig:
    dataset_id: str
    dataset_root: DatasetRoot
    out_dir: Path
    positions: list[Path]
    input_format: str
    split_seed: int
    split_ratios: tuple[int, int, int]
    shard_size: int
    source_seed: int
    resume: bool
    write_dataset_card: bool
    write_qc: bool
    teacher_adapter: str
    teacher_protocol: str
    teacher_depth: int | None
    teacher_timeout_ms: int
    label_jobs: int
    position_log_mode: str
    teacher_workdir: str | None
    teacher_env: dict[str, str]
    teacher_engine_name: str | None
    teacher_engine_cmd: list[str]
    legal_validator: Path | None
    build_exact_overlap: bool
    exact_label_dump: Path
    exact_max_empties: int
    include_move_scores: bool
    dry_run: bool
    allow_failures: bool
    invocation: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class BuildSummary:
    total_positions: int = 0
    unique_positions: int = 0
    duplicates: int = 0
    train_count: int = 0
    validation_count: int = 0
    holdout_count: int = 0
    labels_requested: int = 0
    labels_ok: int = 0
    labels_failed: int = 0
    labels_usable: int = 0
    illegal_teacher_moves: int = 0
    invalid_move_tokens: int = 0
    exact_labels_generated: int = 0
    exact_skipped_too_many_empties: int = 0
    resumed_label_shards: int = 0


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


def parse_split_ratios(value: str) -> tuple[int, int, int]:
    parts = value.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("--split-ratios must be TRAIN,VALIDATION,HOLDOUT")
    try:
        parsed = tuple(int(part) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("--split-ratios values must be integers") from exc
    if any(part < 0 for part in parsed) or sum(parsed) <= 0:
        raise argparse.ArgumentTypeError("--split-ratios values must be non-negative and non-zero")
    return parsed  # type: ignore[return-value]


def _split_teacher_engine_args(values: list[str]) -> tuple[list[str], list[str]]:
    if "--teacher-engine-cmd" not in values:
        return values, []
    index = values.index("--teacher-engine-cmd")
    builder_args = values[:index]
    engine_args = values[index + 1 :]
    if not engine_args or engine_args[0] != "--" or len(engine_args) == 1:
        raise ScriptError("--teacher-engine-cmd must be followed by '--' and a command")
    return builder_args, engine_args[1:]


def _parse_teacher_env(values: list[str]) -> dict[str, str]:
    environment: dict[str, str] = {}
    for value in values:
        key, separator, env_value = value.partition("=")
        if not separator or key == "":
            raise ScriptError(f"--teacher-env must be KEY=VALUE, got: {value}")
        environment[key] = env_value
    return environment


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    values = list(sys.argv[1:] if argv is None else argv)
    builder_args, teacher_engine_cmd = _split_teacher_engine_args(values)

    parser = argparse.ArgumentParser(
        description="Build reusable teacher/exact dataset artifacts under a dataset root."
    )
    parser.add_argument("--dataset-id", required=True)
    parser.add_argument("--dataset-root")
    parser.add_argument(
        "--out",
        help="dataset output directory, absolute or relative to the dataset root "
        "(default: teacher/<dataset-id>)",
    )
    parser.add_argument(
        "--positions",
        action="append",
        required=True,
        help="board9 position file; repeat for multiple sources",
    )
    parser.add_argument(
        "--input-format",
        choices=("auto", "board9"),
        default="auto",
        help="position parser; only board9 data is written to reusable datasets",
    )
    parser.add_argument("--split-seed", type=parse_non_negative_int, default=DEFAULT_SPLIT_SEED)
    parser.add_argument(
        "--split-ratios",
        type=parse_split_ratios,
        default=DEFAULT_SPLIT_RATIOS,
        help="deterministic split ratios as TRAIN,VALIDATION,HOLDOUT",
    )
    parser.add_argument("--shard-size", type=parse_positive_int, default=DEFAULT_SHARD_SIZE)
    parser.add_argument(
        "--source-seed",
        type=parse_non_negative_int,
        help="seed to record on position rows; defaults to --split-seed for file inputs",
    )
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--write-dataset-card", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--write-qc", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument(
        "--teacher-adapter",
        choices=("one-shot", "ntest"),
        default="one-shot",
    )
    parser.add_argument(
        "--teacher-protocol",
        choices=("one-shot", "nboard"),
        help="defaults to one-shot for one-shot adapter and nboard for ntest",
    )
    parser.add_argument("--teacher-depth", type=parse_positive_int)
    parser.add_argument("--teacher-timeout-ms", type=parse_positive_int, default=1000)
    parser.add_argument(
        "--label-jobs",
        type=parse_positive_int,
        default=DEFAULT_LABEL_JOBS,
        help="maximum concurrent teacher-label requests per shard (default: 1)",
    )
    parser.add_argument(
        "--position-log-mode",
        choices=teacher_workflow.POSITION_LOG_MODES,
        default=DEFAULT_POSITION_LOG_MODE,
        help="per-position teacher log policy: all, failures, or none (default: all)",
    )
    parser.add_argument("--teacher-workdir", help="working directory for the teacher engine process")
    parser.add_argument(
        "--teacher-env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="environment override for the teacher engine process; repeatable",
    )
    parser.add_argument("--teacher-engine-name")
    parser.add_argument("--legal-validator", help="C++ move validator path")
    parser.add_argument("--build-exact-overlap", action="store_true")
    parser.add_argument(
        "--exact-label-dump",
        default=str(Path("build") / "othello_exact_label_dump"),
        help="path to C++ exact label dumper",
    )
    parser.add_argument("--exact-max-empties", type=parse_non_negative_int, default=DEFAULT_EXACT_MAX_EMPTIES)
    parser.add_argument("--include-move-scores", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-failures", action="store_true")

    args = parser.parse_args(builder_args)
    args.teacher_engine_cmd = teacher_engine_cmd
    if args.teacher_adapter == "one-shot":
        if args.teacher_protocol not in (None, "one-shot"):
            raise ScriptError("--teacher-adapter one-shot only allows --teacher-protocol one-shot")
        if args.teacher_depth is not None:
            raise ScriptError("--teacher-depth is only valid with --teacher-adapter ntest")
        args.teacher_protocol = "one-shot"
    else:
        args.teacher_protocol = args.teacher_protocol or "nboard"
        if args.teacher_protocol == "one-shot" and args.teacher_depth is not None:
            raise ScriptError("--teacher-depth is only valid with --teacher-adapter ntest --teacher-protocol nboard")
        if args.teacher_protocol == "nboard" and args.teacher_depth is None:
            args.teacher_depth = 26
    args.teacher_env_overrides = _parse_teacher_env(args.teacher_env)
    return args


def collect_git_sha() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return "unknown"
    return completed.stdout.strip() or "unknown"


def _resolve_out_dir(root: Path, dataset_id: str, out: str | None) -> Path:
    if out is None:
        return (root / "teacher" / dataset_id).resolve(strict=False)
    path = Path(out).expanduser()
    if not path.is_absolute():
        path = root / path
    return path.resolve(strict=False)


def _reject_source_controlled_output(path: Path) -> None:
    data_dir = (REPO_ROOT / "data").resolve(strict=False)
    try:
        path.resolve(strict=False).relative_to(data_dir)
    except ValueError:
        return
    raise ScriptError("dataset output must not be under source-controlled data/")


def _reject_outside_dataset_root(path: Path, root: Path) -> None:
    resolved_path = path.resolve(strict=False)
    resolved_root = root.resolve(strict=False)
    try:
        resolved_path.relative_to(resolved_root)
    except ValueError as exc:
        raise ScriptError(
            f"dataset output must be under dataset root; got {resolved_path}; "
            f"dataset root is {resolved_root}"
        ) from exc


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> BuildConfig:
    dataset_root = resolve_dataset_root(args.dataset_root, require_exists=False)
    out_dir = _resolve_out_dir(dataset_root.path, args.dataset_id, args.out)
    _reject_outside_dataset_root(out_dir, dataset_root.path)
    _reject_source_controlled_output(out_dir)
    return BuildConfig(
        dataset_id=args.dataset_id,
        dataset_root=dataset_root,
        out_dir=out_dir,
        positions=[Path(value) for value in args.positions],
        input_format=args.input_format,
        split_seed=args.split_seed,
        split_ratios=args.split_ratios,
        shard_size=args.shard_size,
        source_seed=args.source_seed if args.source_seed is not None else args.split_seed,
        resume=args.resume,
        write_dataset_card=args.write_dataset_card,
        write_qc=args.write_qc,
        teacher_adapter=args.teacher_adapter,
        teacher_protocol=args.teacher_protocol,
        teacher_depth=args.teacher_depth,
        teacher_timeout_ms=args.teacher_timeout_ms,
        label_jobs=args.label_jobs,
        position_log_mode=args.position_log_mode,
        teacher_workdir=args.teacher_workdir,
        teacher_env=args.teacher_env_overrides,
        teacher_engine_name=args.teacher_engine_name,
        teacher_engine_cmd=list(args.teacher_engine_cmd),
        legal_validator=Path(args.legal_validator) if args.legal_validator else None,
        build_exact_overlap=args.build_exact_overlap,
        exact_label_dump=Path(args.exact_label_dump),
        exact_max_empties=args.exact_max_empties,
        include_move_scores=args.include_move_scores,
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        invocation=invocation or [],
    )


def normalize_board9_text(board_text: str) -> str:
    lines = [
        line.strip()
        for line in board_text.splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]
    if len(lines) != 9:
        raise ScriptError("board9 text must contain 8 board rows plus side line")
    for row in lines[:8]:
        if len(row) != 8 or any(char not in ".BW" for char in row):
            raise ScriptError("board9 rows must be 8 characters from '.', 'B', and 'W'")
    if lines[8] not in {"side=B", "side=W"}:
        raise ScriptError("board9 side line must be side=B or side=W")
    return "\n".join(lines)


def position_id_for_board9(board_text: str) -> str:
    normalized = normalize_board9_text(board_text)
    digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()
    return f"sha256:{digest}"


def _source_detail(path: Path, source_line: int, source_id: str | None) -> str:
    detail = f"{path}:{source_line}"
    if source_id:
        detail += f" name={source_id}"
    return detail


def _read_source_positions(config: BuildConfig, git_sha: str) -> tuple[list[PositionRecord], list[DuplicateRecord]]:
    records: list[PositionRecord] = []
    duplicates: list[DuplicateRecord] = []
    seen: dict[str, PositionRecord] = {}

    for source_path in config.positions:
        parsed = teacher_workflow.parse_positions(
            source_path,
            "board9" if config.input_format == "board9" else "auto",
        )
        for position in parsed:
            if position.board_text is None:
                raise ScriptError(f"{source_path}: reusable datasets require board9 positions")
            board_text = normalize_board9_text(position.board_text)
            position_id = position_id_for_board9(board_text)
            source_id = position.metadata.get("name")
            source_detail = _source_detail(source_path, position.source_line, source_id)
            record = PositionRecord(
                position_id=position_id,
                board_text=board_text,
                empties=board_text.count("."),
                source="file",
                source_detail=source_detail,
                source_seed=config.source_seed,
                source_file=str(source_path),
                source_line=position.source_line,
                source_id=source_id,
                git_sha=git_sha,
            )
            first = seen.get(position_id)
            if first is not None:
                duplicates.append(
                    DuplicateRecord(
                        position_id=position_id,
                        first_source_detail=first.source_detail,
                        duplicate_source_detail=source_detail,
                    )
                )
                continue
            seen[position_id] = record
            records.append(record)

    records.sort(key=lambda row: row.position_id)
    return records, duplicates


def split_for_position(position_id: str, *, seed: int, ratios: tuple[int, int, int]) -> str:
    total = sum(ratios)
    digest = hashlib.sha256(f"{seed}:{position_id}".encode("utf-8")).hexdigest()
    bucket = int(digest, 16) % total
    if bucket < ratios[0]:
        return "train"
    if bucket < ratios[0] + ratios[1]:
        return "validation"
    return "holdout"


def assign_splits(records: Iterable[PositionRecord], *, seed: int, ratios: tuple[int, int, int]) -> list[PositionRecord]:
    assigned: list[PositionRecord] = []
    for record in records:
        assigned.append(
            PositionRecord(
                position_id=record.position_id,
                board_text=record.board_text,
                empties=record.empties,
                source=record.source,
                source_detail=record.source_detail,
                source_seed=record.source_seed,
                source_file=record.source_file,
                source_line=record.source_line,
                source_id=record.source_id,
                git_sha=record.git_sha,
                split=split_for_position(record.position_id, seed=seed, ratios=ratios),
            )
        )
    return assigned


def position_row(record: PositionRecord) -> dict[str, Any]:
    row: dict[str, Any] = {
        "schema": SCHEMA_POSITION,
        "position_id": record.position_id,
        "board_text": record.board_text,
        "empties": record.empties,
        "source": record.source,
        "source_detail": record.source_detail,
        "source_seed": record.source_seed,
        "split": record.split,
        "git_sha": record.git_sha,
        "source_file": record.source_file,
        "source_line": record.source_line,
    }
    if record.source_id:
        row["source_id"] = record.source_id
    return row


def write_jsonl(path: Path, rows: Iterable[dict[str, Any]]) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with path.open("w", encoding="utf-8") as output:
        for row in rows:
            output.write(json.dumps(row, sort_keys=True) + "\n")
            count += 1
    return count


def shard_path(directory: Path, prefix: str, index: int) -> Path:
    return directory / f"{prefix}-{index:04d}.jsonl"


def write_position_shards(records: list[PositionRecord], config: BuildConfig) -> list[Path]:
    shard_dir = config.out_dir / "positions" / "shards"
    shard_dir.mkdir(parents=True, exist_ok=True)
    paths: list[Path] = []
    for shard_index, start in enumerate(range(0, len(records), config.shard_size)):
        shard_records = records[start : start + config.shard_size]
        path = shard_path(shard_dir, "positions", shard_index)
        write_jsonl(path, [position_row(record) for record in shard_records])
        paths.append(path)
    if not paths:
        path = shard_path(shard_dir, "positions", 0)
        write_jsonl(path, [])
        paths.append(path)
    return paths


def write_splits(records: list[PositionRecord], config: BuildConfig) -> dict[str, int]:
    splits_dir = config.out_dir / "splits"
    splits_dir.mkdir(parents=True, exist_ok=True)
    counts: dict[str, int] = {}
    for split in ("train", "validation", "holdout"):
        ids = sorted(record.position_id for record in records if record.split == split)
        counts[split] = len(ids)
        (splits_dir / f"{split}.ids").write_text("\n".join(ids) + ("\n" if ids else ""), encoding="utf-8")
    manifest = {
        "schema": SCHEMA_SPLIT_MANIFEST,
        "split_seed": config.split_seed,
        "split_ratios": {
            "train": config.split_ratios[0],
            "validation": config.split_ratios[1],
            "holdout": config.split_ratios[2],
        },
        "counts": counts,
        "method": "sha256(split_seed + ':' + position_id) modulo ratio sum",
    }
    (splits_dir / "split_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return counts


def _teacher_name(config: BuildConfig) -> str:
    if config.teacher_engine_name:
        return "".join(
            char if char.isalnum() or char in ("-", "_") else "-"
            for char in config.teacher_engine_name
        ).strip("-") or "teacher"
    if config.teacher_engine_cmd:
        return Path(config.teacher_engine_cmd[0]).name or "teacher"
    return "none"


def _board9_input_for_records(path: Path, records: list[PositionRecord]) -> None:
    lines: list[str] = []
    for record in records:
        lines.append(f"# name: {record.position_id}")
        if record.source_id:
            lines.append(f"# note: source_id={record.source_id}")
        lines.append(record.board_text)
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def _valid_completed_label_shard(path: Path, expected_rows: int) -> bool:
    if not path.is_file():
        return False
    rows = read_jsonl(path)
    if len(rows) != expected_rows:
        return False
    return all(isinstance(row, dict) and row.get("schema") == "teacher_label.v1" for row in rows)


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as input_file:
        for line_number, line in enumerate(input_file, start=1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ScriptError(f"{path}: line {line_number}: invalid JSON: {exc.msg}") from exc
            if not isinstance(value, dict):
                raise ScriptError(f"{path}: line {line_number}: expected JSON object")
            rows.append(value)
    return rows


def _teacher_workflow_config(
    config: BuildConfig,
    *,
    shard_positions_path: Path,
    workflow_out_dir: Path,
) -> teacher_workflow.WorkflowConfig:
    engine_name = config.teacher_engine_name
    if not engine_name:
        engine_name = Path(config.teacher_engine_cmd[0]).name if config.teacher_engine_cmd else "unknown"
    return teacher_workflow.WorkflowConfig(
        positions_path=shard_positions_path,
        out_dir=workflow_out_dir,
        input_format="board9",
        limit=None,
        engine_name=engine_name,
        adapter=config.teacher_adapter,
        protocol=config.teacher_protocol,
        depth=config.teacher_depth,
        timeout_ms=config.teacher_timeout_ms,
        workdir=config.teacher_workdir,
        env=dict(config.teacher_env),
        engine_command=config.teacher_engine_cmd,
        legal_validator=config.legal_validator,
        dry_run=config.dry_run,
        allow_failures=True,
        jobs=config.label_jobs,
        position_log_mode=config.position_log_mode,
        invocation=redact_command(config.invocation),
        report_workdir=redact_token(config.teacher_workdir) if config.teacher_workdir else None,
        report_engine_command=redact_command(config.teacher_engine_cmd),
    )


def _augment_teacher_rows(rows: list[dict[str, Any]], shard_records: list[PositionRecord], shard_index: int) -> list[dict[str, Any]]:
    record_by_position_id = {record.position_id: record for record in shard_records}
    augmented: list[dict[str, Any]] = []
    for row in rows:
        position_id = str(row.get("position_name") or "")
        record = record_by_position_id.get(position_id)
        if record is None:
            board_text = row.get("board_text")
            if isinstance(board_text, str):
                position_id = position_id_for_board9(board_text)
                record = record_by_position_id.get(position_id)
        if record is None:
            raise ScriptError("teacher label row could not be matched back to a position_id")
        row = dict(row)
        row["position_id"] = record.position_id
        row["position_split"] = record.split
        row["position_shard"] = shard_index
        row["label_usable"] = row.get("status") == "ok" and row.get("legal_move_valid") is True
        augmented.append(row)
    return augmented


def run_teacher_labels(config: BuildConfig, records: list[PositionRecord]) -> tuple[BuildSummary, list[Path]]:
    if not config.teacher_engine_cmd:
        return BuildSummary(), []

    teacher_name = _teacher_name(config)
    label_root = config.out_dir / "labels" / teacher_name
    shard_dir = label_root / "shards"
    failed_path = label_root / "failed.jsonl"
    workflow_dir = label_root / "workflow-shards"
    input_dir = label_root / "input-shards"
    shard_dir.mkdir(parents=True, exist_ok=True)
    all_failed: list[dict[str, Any]] = []
    label_paths: list[Path] = []
    resumed = 0

    requested = ok = failed = usable = illegal = invalid = 0
    for shard_index, start in enumerate(range(0, len(records), config.shard_size)):
        shard_records = records[start : start + config.shard_size]
        labels_path = shard_path(shard_dir, "labels", shard_index)
        label_paths.append(labels_path)
        if config.resume and _valid_completed_label_shard(labels_path, len(shard_records)):
            resumed += 1
            rows = read_jsonl(labels_path)
        else:
            shard_positions = input_dir / f"positions-{shard_index:04d}.txt"
            _board9_input_for_records(shard_positions, shard_records)
            workflow_config = _teacher_workflow_config(
                config,
                shard_positions_path=shard_positions,
                workflow_out_dir=workflow_dir / f"shard-{shard_index:04d}",
            )
            teacher_workflow.run_workflow(workflow_config)
            rows = _augment_teacher_rows(read_jsonl(workflow_config.labels_path), shard_records, shard_index)
            write_jsonl(labels_path, rows)

        requested += len(rows)
        ok += sum(1 for row in rows if row.get("status") == "ok")
        failed_rows = [
            row
            for row in rows
            if row.get("status") != "ok" or row.get("legal_move_valid") is not True
        ]
        failed += len(failed_rows)
        usable += sum(1 for row in rows if row.get("label_usable") is True)
        illegal += sum(1 for row in rows if row.get("legal_move_valid") is False)
        invalid += sum(1 for row in rows if row.get("move_token_valid") is False)
        all_failed.extend(failed_rows)

    write_jsonl(failed_path, all_failed)
    manifest = {
        "schema": "teacher_label_manifest.v1",
        "teacher_name": teacher_name,
        "adapter": config.teacher_adapter,
        "protocol": config.teacher_protocol,
        "depth": config.teacher_depth,
        "timeout_ms": config.teacher_timeout_ms,
        "label_jobs": config.label_jobs,
        "position_log_mode": config.position_log_mode,
        "teacher_workdir": redact_token(config.teacher_workdir) if config.teacher_workdir else None,
        "teacher_env_keys": sorted(config.teacher_env),
        "command": redact_command(config.teacher_engine_cmd),
        "shard_count": len(label_paths),
        "resume": config.resume,
        "resumed_label_shards": resumed,
        "counts": {
            "requested": requested,
            "ok": ok,
            "failed": failed,
            "usable": usable,
            "illegal_teacher_moves": illegal,
            "invalid_move_tokens": invalid,
        },
    }
    (label_root / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return (
        BuildSummary(
            labels_requested=requested,
            labels_ok=ok,
            labels_failed=failed,
            labels_usable=usable,
            illegal_teacher_moves=illegal,
            invalid_move_tokens=invalid,
            resumed_label_shards=resumed,
        ),
        label_paths,
    )


def _exact_positions_file(path: Path, records: list[PositionRecord]) -> None:
    lines: list[str] = []
    for record in records:
        lines.append(f"# name: {record.position_id}")
        lines.append(record.board_text)
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def exact_label_command(config: BuildConfig, positions_path: Path, labels_path: Path) -> list[str]:
    command = [
        str(config.exact_label_dump),
        "--input",
        str(positions_path),
        "--output",
        str(labels_path),
        "--max-empties",
        str(config.exact_max_empties),
    ]
    if config.include_move_scores:
        command.append("--include-move-scores")
    return command


def run_exact_overlap(config: BuildConfig, records: list[PositionRecord]) -> tuple[int, int, Path | None]:
    exact_dir = config.out_dir / "exact-overlap"
    exact_dir.mkdir(parents=True, exist_ok=True)
    eligible = [record for record in records if record.empties <= config.exact_max_empties]
    skipped = [record for record in records if record.empties > config.exact_max_empties]
    write_jsonl(
        exact_dir / "skipped_too_many_empties.jsonl",
        [
            {
                "position_id": record.position_id,
                "empties": record.empties,
                "max_empties": config.exact_max_empties,
            }
            for record in skipped
        ],
    )
    labels_path = exact_dir / "labels.jsonl"
    positions_path = exact_dir / "positions.txt"
    command: list[str] = []
    status = "disabled"
    generated = 0

    if config.build_exact_overlap:
        _exact_positions_file(positions_path, eligible)
        command = exact_label_command(config, positions_path, labels_path)
        status = "planned" if config.dry_run else "completed"
        if config.dry_run:
            labels_path.write_text("", encoding="utf-8")
        elif eligible:
            log_path = config.out_dir / "logs" / "exact-overlap.log"
            log_path.parent.mkdir(parents=True, exist_ok=True)
            completed = subprocess.run(command, check=False, capture_output=True, text=True)
            log_path.write_text(
                f"command: {quote_command(command)}\n"
                f"exit_code: {completed.returncode}\n\n"
                f"stdout:\n{completed.stdout}\n"
                f"stderr:\n{completed.stderr}\n",
                encoding="utf-8",
            )
            if completed.returncode != 0 and not config.allow_failures:
                raise ScriptError(f"exact overlap generation failed with exit code {completed.returncode}")
        else:
            labels_path.write_text("", encoding="utf-8")
        if labels_path.is_file():
            generated = len([line for line in labels_path.read_text(encoding="utf-8").splitlines() if line.strip()])

    manifest = {
        "schema": "exact_overlap_manifest.v1",
        "enabled": config.build_exact_overlap,
        "status": status,
        "max_empties": config.exact_max_empties,
        "include_move_scores": config.include_move_scores,
        "eligible_positions": len(eligible),
        "skipped_too_many_empties": len(skipped),
        "labels_generated": generated,
        "command": redact_command(command),
    }
    (exact_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return generated, len(skipped), labels_path if config.build_exact_overlap else None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as input_file:
        for chunk in iter(lambda: input_file.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def redact_token(token: str) -> str:
    if token.startswith("--") and "=" in token:
        key, _, value = token.partition("=")
        return f"{key}={redact_token(value)}"
    path = Path(token)
    if path.is_absolute():
        return f"<absolute-path:{path.name}>"
    return token


def redact_env_assignment(token: str) -> str:
    key, separator, _ = token.partition("=")
    if not separator or not key:
        return "<env>"
    return f"{key}=<redacted>"


def redact_command(command: list[str]) -> list[str]:
    redacted: list[str] = []
    redact_next_env = False
    for part in command:
        if redact_next_env:
            redacted.append(redact_env_assignment(part))
            redact_next_env = False
            continue
        if part == "--teacher-env":
            redacted.append(part)
            redact_next_env = True
            continue
        if part.startswith("--teacher-env="):
            flag, _, value = part.partition("=")
            redacted.append(f"{flag}={redact_env_assignment(value)}")
            continue
        redacted.append(redact_token(part))
    return redacted


def sanitized_invocation(invocation: list[str]) -> str:
    return quote_command(redact_command(invocation)) if invocation else "unknown"


def dataset_ref_prefix(config: BuildConfig) -> str:
    try:
        relative = config.out_dir.resolve(strict=False).relative_to(
            config.dataset_root.path.resolve(strict=False)
        )
    except ValueError as exc:
        raise ScriptError("dataset output must be under dataset root to write dataset: references") from exc
    prefix = relative.as_posix()
    return "" if prefix == "." else prefix


def dataset_ref(prefix: str, suffix: str) -> str:
    clean_suffix = suffix.strip("/")
    if prefix:
        return f"dataset:{prefix}/{clean_suffix}"
    return f"dataset:{clean_suffix}"


def write_commands(config: BuildConfig) -> None:
    lines = [
        "#!/usr/bin/env sh",
        "# Recreate or adapt this dataset build. Local absolute paths are redacted.",
        sanitized_invocation(config.invocation),
        "",
    ]
    path = config.out_dir / "commands.sh"
    path.write_text("\n".join(lines), encoding="utf-8")
    path.chmod(0o755)


def write_qc_outputs(
    config: BuildConfig,
    records: list[PositionRecord],
    duplicates: list[DuplicateRecord],
    summary: BuildSummary,
) -> None:
    qc_dir = config.out_dir / "qc"
    qc_dir.mkdir(parents=True, exist_ok=True)
    (qc_dir / "summary.json").write_text(
        json.dumps(summary.__dict__, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    split_empties: dict[int, Counter[str]] = {}
    for record in records:
        split_empties.setdefault(record.empties, Counter())[record.split] += 1
    phase_lines = ["empties\ttrain\tvalidation\tholdout\ttotal"]
    for empties in sorted(split_empties):
        counts = split_empties[empties]
        total = sum(counts.values())
        phase_lines.append(
            f"{empties}\t{counts['train']}\t{counts['validation']}\t{counts['holdout']}\t{total}"
        )
    (qc_dir / "phase_distribution.tsv").write_text("\n".join(phase_lines) + "\n", encoding="utf-8")
    duplicate_lines = ["position_id\tfirst_source_detail\tduplicate_source_detail"]
    for duplicate in duplicates:
        duplicate_lines.append(
            f"{duplicate.position_id}\t{duplicate.first_source_detail}\t{duplicate.duplicate_source_detail}"
        )
    (qc_dir / "duplicate_report.tsv").write_text("\n".join(duplicate_lines) + "\n", encoding="utf-8")
    legality = {
        "labels_requested": summary.labels_requested,
        "labels_ok": summary.labels_ok,
        "labels_failed": summary.labels_failed,
        "labels_usable": summary.labels_usable,
        "illegal_teacher_moves": summary.illegal_teacher_moves,
        "invalid_move_tokens": summary.invalid_move_tokens,
    }
    (qc_dir / "legality_summary.json").write_text(json.dumps(legality, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    failures_path = config.out_dir / "labels" / _teacher_name(config) / "failed.jsonl"
    failure_lines = ["position_id\tstatus\terror\tmove\tlegal_move_valid\tmove_token_valid"]
    if failures_path.is_file():
        for row in read_jsonl(failures_path):
            failure_lines.append(
                "\t".join(
                    [
                        str(row.get("position_id", "")),
                        str(row.get("status", "")),
                        str(row.get("error", "")),
                        str(row.get("move", "")),
                        str(row.get("legal_move_valid", "")),
                        str(row.get("move_token_valid", "")),
                    ]
                )
            )
    (qc_dir / "failures.tsv").write_text("\n".join(failure_lines) + "\n", encoding="utf-8")


def merge_summaries(base: BuildSummary, teacher: BuildSummary, *, exact_generated: int, exact_skipped: int) -> BuildSummary:
    return BuildSummary(
        total_positions=base.total_positions,
        unique_positions=base.unique_positions,
        duplicates=base.duplicates,
        train_count=base.train_count,
        validation_count=base.validation_count,
        holdout_count=base.holdout_count,
        labels_requested=teacher.labels_requested,
        labels_ok=teacher.labels_ok,
        labels_failed=teacher.labels_failed,
        labels_usable=teacher.labels_usable,
        illegal_teacher_moves=teacher.illegal_teacher_moves,
        invalid_move_tokens=teacher.invalid_move_tokens,
        exact_labels_generated=exact_generated,
        exact_skipped_too_many_empties=exact_skipped,
        resumed_label_shards=teacher.resumed_label_shards,
    )


def write_manifest(
    config: BuildConfig,
    duplicates: list[DuplicateRecord],
    summary: BuildSummary,
    artifact_paths: list[Path],
) -> None:
    now = dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")
    manifest = {
        "schema": SCHEMA_MANIFEST,
        "dataset_id": config.dataset_id,
        "created_at": now,
        "repo_git_sha": collect_git_sha(),
        "command_line": sanitized_invocation(config.invocation),
        "dataset_root": str(config.dataset_root.path),
        "dataset_root_source": config.dataset_root.source,
        "output_dir": str(config.out_dir),
        "position_sources": [str(path) for path in config.positions],
        "split_seed": config.split_seed,
        "split_ratios": {
            "train": config.split_ratios[0],
            "validation": config.split_ratios[1],
            "holdout": config.split_ratios[2],
        },
        "shard_size": config.shard_size,
        "teacher_engine_settings": {
            "enabled": bool(config.teacher_engine_cmd),
            "name": config.teacher_engine_name,
            "adapter": config.teacher_adapter,
            "protocol": config.teacher_protocol,
            "depth": config.teacher_depth,
            "timeout_ms": config.teacher_timeout_ms,
            "label_jobs": config.label_jobs,
            "position_log_mode": config.position_log_mode,
            "teacher_workdir": redact_token(config.teacher_workdir) if config.teacher_workdir else None,
            "teacher_env_keys": sorted(config.teacher_env),
            "command": redact_command(config.teacher_engine_cmd),
            "legal_validator": redact_token(str(config.legal_validator)) if config.legal_validator else None,
        },
        "exact_label_settings": {
            "enabled": config.build_exact_overlap,
            "exact_label_dump": redact_token(str(config.exact_label_dump)),
            "max_empties": config.exact_max_empties,
            "include_move_scores": config.include_move_scores,
        },
        "counts": summary.__dict__,
        "duplicate_counts": {
            "duplicates": len(duplicates),
        },
        "per_file_sha256": {
            str(path.relative_to(config.out_dir)): sha256_file(path)
            for path in artifact_paths
            if path.is_file()
        },
    }
    (config.out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_dataset_card(config: BuildConfig, records: list[PositionRecord], summary: BuildSummary) -> None:
    empties_counts = Counter(record.empties for record in records)
    exact_command = exact_label_command(
        config,
        config.out_dir / "exact-overlap" / "positions.txt",
        config.out_dir / "exact-overlap" / "labels.jsonl",
    )
    reuse_prefix = dataset_ref_prefix(config)
    lines = [
        f"# Dataset Card: {config.dataset_id}",
        "",
        "## Purpose",
        "",
        "Reusable teacher/exact data foundation for future pattern-learning workflows.",
        "No strength claim. No default promotion.",
        "",
        "## Data Sources",
        "",
    ]
    for path in config.positions:
        lines.append(f"- `{redact_token(str(path))}`")
    lines.extend(
        [
            "",
            "## Generation Commands",
            "",
            "```sh",
            sanitized_invocation(config.invocation),
            "```",
            "",
            "## Position Distribution By Empties",
            "",
            "| empties | count |",
            "| --- | ---: |",
        ]
    )
    for empties in sorted(empties_counts):
        lines.append(f"| {empties} | {empties_counts[empties]} |")
    lines.extend(
        [
            "",
            "## Split Counts",
            "",
            f"- train: `{summary.train_count}`",
            f"- validation: `{summary.validation_count}`",
            f"- holdout: `{summary.holdout_count}`",
            "",
            "## Teacher Settings",
            "",
            f"- enabled: `{'true' if config.teacher_engine_cmd else 'false'}`",
            f"- engine_name: `{config.teacher_engine_name or _teacher_name(config)}`",
            f"- adapter: `{config.teacher_adapter}`",
            f"- protocol: `{config.teacher_protocol}`",
            f"- depth: `{config.teacher_depth if config.teacher_depth is not None else 'n/a'}`",
            f"- timeout_ms: `{config.teacher_timeout_ms}`",
            f"- label_jobs: `{config.label_jobs}`",
            f"- position_log_mode: `{config.position_log_mode}`",
            f"- teacher_workdir: `{redact_token(config.teacher_workdir) if config.teacher_workdir else 'n/a'}`",
            f"- teacher_env_keys: `{', '.join(sorted(config.teacher_env)) if config.teacher_env else 'n/a'}`",
            f"- command: `{quote_command(redact_command(config.teacher_engine_cmd)) if config.teacher_engine_cmd else 'n/a'}`",
            "",
            "## Legal Validation Summary",
            "",
            f"- labels_requested: `{summary.labels_requested}`",
            f"- labels_ok: `{summary.labels_ok}`",
            f"- labels_failed: `{summary.labels_failed}`",
            f"- labels_usable: `{summary.labels_usable}`",
            f"- illegal_teacher_moves: `{summary.illegal_teacher_moves}`",
            f"- invalid_move_tokens: `{summary.invalid_move_tokens}`",
            "",
            "## Exact Overlap Summary",
            "",
            f"- enabled: `{'true' if config.build_exact_overlap else 'false'}`",
            f"- exact_label_dump: `{redact_token(str(config.exact_label_dump))}`",
            f"- max_empties: `{config.exact_max_empties}`",
            f"- command: `{quote_command(redact_command(exact_command)) if config.build_exact_overlap else 'n/a'}`",
            f"- exact_labels_generated: `{summary.exact_labels_generated}`",
            f"- skipped_too_many_empties: `{summary.exact_skipped_too_many_empties}`",
            "",
            "## Reuse",
            "",
            f"- positions: `{dataset_ref(reuse_prefix, 'positions/shards/positions-0000.jsonl')}`",
            f"- teacher labels: `{dataset_ref(reuse_prefix, f'labels/{_teacher_name(config)}/shards/labels-0000.jsonl')}`",
            f"- exact overlap: `{dataset_ref(reuse_prefix, 'exact-overlap/labels.jsonl')}`",
            "",
            "## Known Caveats",
            "",
            "- Generated raw data is not committed to git.",
            "- Reusable artifacts should stay under the external dataset root.",
            "- `runs/` remains temporary per-worktree output.",
            "- Teacher labels are external-engine evidence, not exact truth.",
            "- Only rows with `status == ok` and `legal_move_valid == true` are usable for training.",
            "- Exact overlap labels are generated only for positions within the configured empty-count threshold.",
        ]
    )
    (config.out_dir / "dataset_card.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_build(config: BuildConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    git_sha = collect_git_sha()
    records, duplicates = _read_source_positions(config, git_sha)
    total_positions = len(records) + len(duplicates)
    records = assign_splits(records, seed=config.split_seed, ratios=config.split_ratios)
    position_shards = write_position_shards(records, config)
    split_counts = write_splits(records, config)
    write_commands(config)

    teacher_summary, label_paths = run_teacher_labels(config, records)
    exact_generated, exact_skipped, exact_path = run_exact_overlap(config, records)

    base_summary = BuildSummary(
        total_positions=total_positions,
        unique_positions=len(records),
        duplicates=len(duplicates),
        train_count=split_counts["train"],
        validation_count=split_counts["validation"],
        holdout_count=split_counts["holdout"],
    )
    summary = merge_summaries(
        base_summary,
        teacher_summary,
        exact_generated=exact_generated,
        exact_skipped=exact_skipped,
    )

    artifact_paths = [*position_shards, *label_paths]
    if exact_path is not None:
        artifact_paths.append(exact_path)
    if config.write_qc:
        write_qc_outputs(config, records, duplicates, summary)
    write_manifest(config, duplicates, summary, artifact_paths)
    if config.write_dataset_card:
        write_dataset_card(config, records, summary)

    if summary.labels_failed and not config.allow_failures:
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_build(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
