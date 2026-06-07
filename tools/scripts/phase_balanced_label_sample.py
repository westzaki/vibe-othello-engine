#!/usr/bin/env python3
"""Build phase-balanced diagnostic teacher/exact label subsets for PatternOnly."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from common import ScriptError, parse_csv_values
from dataset_paths import is_dataset_reference, resolve_dataset_root, resolve_path_references
from pattern_training.board9 import (
    board_key,
    empty_count,
    legal_moves_for_board,
    normalize_move,
    occupied_count,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
PHASES = ("opening", "midgame", "late")
DEFAULT_PHASE_CUTOFFS = (20, 44)
DEFAULT_SEED = 20260607
DEFAULT_BUCKET_FIELD = "source_bucket"
DEFAULT_LEGAL_MOVE_BUCKETS = "1,2,3-4,5-8,9+"


@dataclass(frozen=True)
class PhaseCutoffs:
    opening_max_occupied: int
    midgame_max_occupied: int


@dataclass(frozen=True)
class LabelSource:
    path: Path
    line_number: int
    record: dict[str, Any]
    split_hint: str | None


@dataclass(frozen=True)
class LegalMoveBucket:
    label: str
    minimum: int
    maximum: int | None


@dataclass(frozen=True)
class PreparedTeacherRow:
    source: LabelSource
    board_key: str
    board_text: str
    teacher_move: str
    split: str
    phase: str
    occupied_count: int
    empty_count: int
    empties_bucket: str
    source_bucket: str
    legal_move_count: int
    legal_move_bucket: str
    exact_status: str
    teacher_exact_status: str
    stratum_key: tuple[str, str, str, str, str, str]
    exact_record: dict[str, Any] | None
    has_complete_move_scores: bool


@dataclass(frozen=True)
class ExactLoadResult:
    by_board: dict[str, dict[str, Any]]
    stats: dict[str, int]


@dataclass(frozen=True)
class SampleConfig:
    teacher_labels: tuple[Path, ...]
    exact_labels: tuple[Path, ...]
    eval_config: Path
    out_dir: Path
    rows: int
    seed: int
    split: str
    phase_targets: dict[str, int]
    bucket_field: str
    legal_move_buckets: tuple[LegalMoveBucket, ...]
    empties_bucket_size: int
    allow_shortage: bool
    dataset_root: dict[str, str] | None
    invocation: list[str]


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


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build phase-balanced diagnostic teacher/exact label subsets for PatternOnly."
    )
    parser.add_argument("--teacher-labels", required=True, help="comma-separated paths or dataset: references")
    parser.add_argument("--exact-labels", help="comma-separated paths or dataset: references")
    parser.add_argument("--dataset-root")
    parser.add_argument("--eval-config", required=True)
    parser.add_argument("--out-dir", required=True, help="output directory; must be under repository runs/")
    parser.add_argument("--rows", required=True, type=parse_positive_int)
    parser.add_argument("--seed", type=parse_non_negative_int, default=DEFAULT_SEED)
    parser.add_argument(
        "--split",
        choices=("train", "validation", "holdout", "all"),
        default="train",
    )
    parser.add_argument(
        "--phase-targets",
        help="optional comma-separated targets, e.g. opening=3334,midgame=3333,late=3333",
    )
    parser.add_argument(
        "--allow-shortage",
        action="store_true",
        help="write a partial subset when phase targets cannot be filled",
    )
    parser.add_argument("--bucket-field", default=DEFAULT_BUCKET_FIELD)
    parser.add_argument("--legal-move-buckets", default=DEFAULT_LEGAL_MOVE_BUCKETS)
    parser.add_argument("--empties-bucket-size", type=parse_positive_int, default=4)
    return parser.parse_args(argv)


def _line_key_value(line: str) -> tuple[str, str] | None:
    body = line.split("#", 1)[0].strip()
    if not body or "=" not in body:
        return None
    key, value = body.split("=", 1)
    return key.strip(), value.strip()


def eval_config_entries(text: str) -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in text.splitlines():
        parsed = _line_key_value(line)
        if parsed is not None:
            key, value = parsed
            entries[key] = value
    return entries


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
    entries = eval_config_entries(text)
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


def parse_label_paths(value: str, *, dataset_root: str | None = None) -> tuple[Path, ...]:
    return tuple(
        resolve_path_references(
            parse_csv_values(value, error_label="label path list"),
            explicit_root=dataset_root,
        )
    )


def resolve_out_dir(path_text: str) -> Path:
    path = Path(path_text)
    resolved = path.resolve(strict=False)
    repo_runs = (REPO_ROOT / "runs").resolve(strict=False)
    try:
        resolved.relative_to(repo_runs)
    except ValueError as exc:
        raise ScriptError("--out-dir must be under repository runs/") from exc
    return path


def default_phase_targets(rows: int) -> dict[str, int]:
    base = rows // len(PHASES)
    remainder = rows % len(PHASES)
    return {
        phase: base + (1 if index < remainder else 0)
        for index, phase in enumerate(PHASES)
    }


def parse_phase_targets(value: str | None, *, rows: int) -> dict[str, int]:
    if value is None:
        return default_phase_targets(rows)
    targets = {phase: 0 for phase in PHASES}
    seen: set[str] = set()
    for part in parse_csv_values(value, error_label="phase target list"):
        phase, separator, count_text = part.partition("=")
        if not separator:
            raise ScriptError("--phase-targets entries must be PHASE=COUNT")
        phase = phase.strip()
        if phase not in PHASES:
            raise ScriptError(f"unknown phase target: {phase}")
        if phase in seen:
            raise ScriptError(f"duplicate phase target: {phase}")
        seen.add(phase)
        try:
            count = int(count_text)
        except ValueError as exc:
            raise ScriptError(f"phase target for {phase} must be an integer") from exc
        if count < 0:
            raise ScriptError(f"phase target for {phase} must be non-negative")
        targets[phase] = count
    if sum(targets.values()) != rows:
        raise ScriptError("--phase-targets total must match --rows")
    return targets


def parse_legal_move_buckets(value: str) -> tuple[LegalMoveBucket, ...]:
    buckets: list[LegalMoveBucket] = []
    for part in parse_csv_values(value, error_label="legal move bucket list"):
        if part.endswith("+"):
            try:
                minimum = int(part[:-1])
            except ValueError as exc:
                raise ScriptError(f"invalid legal move bucket: {part}") from exc
            maximum = None
        elif "-" in part:
            left, right = part.split("-", 1)
            try:
                minimum = int(left)
                maximum = int(right)
            except ValueError as exc:
                raise ScriptError(f"invalid legal move bucket: {part}") from exc
            if maximum < minimum:
                raise ScriptError(f"invalid legal move bucket range: {part}")
        else:
            try:
                minimum = int(part)
            except ValueError as exc:
                raise ScriptError(f"invalid legal move bucket: {part}") from exc
            maximum = minimum
        if minimum < 0:
            raise ScriptError(f"invalid legal move bucket: {part}")
        buckets.append(LegalMoveBucket(label=part, minimum=minimum, maximum=maximum))
    return tuple(buckets)


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> SampleConfig:
    teacher_labels = parse_label_paths(args.teacher_labels, dataset_root=args.dataset_root)
    exact_labels = (
        parse_label_paths(args.exact_labels, dataset_root=args.dataset_root)
        if args.exact_labels
        else ()
    )
    raw_label_values = parse_csv_values(args.teacher_labels, error_label="teacher label path list")
    if args.exact_labels:
        raw_label_values.extend(parse_csv_values(args.exact_labels, error_label="exact label path list"))
    dataset_root = None
    if args.dataset_root or any(is_dataset_reference(value) for value in raw_label_values):
        root = resolve_dataset_root(args.dataset_root, require_exists=False)
        dataset_root = {"path": str(root.path), "source": root.source}
    return SampleConfig(
        teacher_labels=teacher_labels,
        exact_labels=exact_labels,
        eval_config=Path(args.eval_config),
        out_dir=resolve_out_dir(args.out_dir),
        rows=args.rows,
        seed=args.seed,
        split=args.split,
        phase_targets=parse_phase_targets(args.phase_targets, rows=args.rows),
        bucket_field=args.bucket_field,
        legal_move_buckets=parse_legal_move_buckets(args.legal_move_buckets),
        empties_bucket_size=args.empties_bucket_size,
        allow_shortage=args.allow_shortage,
        dataset_root=dataset_root,
        invocation=invocation or [],
    )


def split_hint_for_path(path: Path) -> str | None:
    for part in reversed(path.parts):
        lowered = part.lower()
        stem = Path(part).stem.lower()
        for split in ("train", "validation", "holdout"):
            if lowered == split or stem == split or lowered.startswith(f"{split}."):
                return split
    return None


def read_jsonl_sources(paths: Iterable[Path]) -> list[LabelSource]:
    sources: list[LabelSource] = []
    for path in paths:
        split_hint = split_hint_for_path(path)
        try:
            with path.open("r", encoding="utf-8") as input_file:
                for line_number, line in enumerate(input_file, start=1):
                    if not line.strip():
                        continue
                    try:
                        record = json.loads(line)
                    except json.JSONDecodeError as exc:
                        raise ScriptError(f"{path}:{line_number}: invalid JSON: {exc.msg}") from exc
                    if not isinstance(record, dict):
                        raise ScriptError(f"{path}:{line_number}: record must be an object")
                    sources.append(
                        LabelSource(
                            path=path,
                            line_number=line_number,
                            record=record,
                            split_hint=split_hint,
                        )
                    )
        except OSError as exc:
            raise ScriptError(f"failed to read {path}: {exc}") from exc
    return sources


def board_text_from_record(record: dict[str, Any]) -> str | None:
    for key in ("board_text", "board"):
        value = record.get(key)
        if isinstance(value, str) and value.strip():
            return value
    return None


def teacher_move_from_record(record: dict[str, Any]) -> Any:
    for key in ("move", "teacher_move", "best_move"):
        if key in record:
            return record[key]
    return None


def accepted_teacher_record(record: dict[str, Any]) -> bool:
    if record.get("status") not in (None, "ok"):
        return False
    if record.get("legal_move_valid") is False:
        return False
    if record.get("move_token_valid") is False:
        return False
    return board_text_from_record(record) is not None and normalize_move(teacher_move_from_record(record)) is not None


def split_for_source(source: LabelSource, seed: int) -> str:
    explicit = source.record.get("position_split") or source.record.get("split")
    if explicit in {"train", "validation", "holdout"}:
        return str(explicit)
    if source.split_hint is not None:
        return source.split_hint
    board_text = board_text_from_record(source.record) or ""
    move = normalize_move(teacher_move_from_record(source.record)) or ""
    material = f"{seed}\n{board_key(board_text)}\n{move}".encode("utf-8")
    bucket = int.from_bytes(hashlib.sha256(material).digest()[:8], "big") % 100
    if bucket < 70:
        return "train"
    if bucket < 85:
        return "validation"
    return "holdout"


def load_exact_by_board(paths: tuple[Path, ...]) -> ExactLoadResult:
    exact: dict[str, dict[str, Any]] = {}
    ranks: dict[str, int] = {}
    stats: collections.Counter[str] = collections.Counter()
    for source in read_jsonl_sources(paths):
        board = source.record.get("board")
        if isinstance(board, str):
            stats["exact_rows"] += 1
            key = board_key(board)
            rank = exact_record_rank(source.record)
            if key in exact:
                stats["duplicate_exact_boards"] += 1
                if rank > ranks[key]:
                    exact[key] = source.record
                    ranks[key] = rank
                    stats["duplicate_exact_complete_replacements"] += 1
                else:
                    stats["duplicate_exact_ignored"] += 1
                continue
            exact[key] = source.record
            ranks[key] = rank
    stats["exact_unique_boards"] = len(exact)
    return ExactLoadResult(by_board=exact, stats=dict(stats))


def exact_best_moves(record: dict[str, Any] | None) -> tuple[str, ...]:
    if record is None:
        return ()
    moves = record.get("best_moves")
    if isinstance(moves, list):
        return tuple(sorted(move for move in (normalize_move(value) for value in moves) if move))
    move = normalize_move(record.get("best_move"))
    return (move,) if move else ()


def exact_move_scores(record: dict[str, Any] | None) -> dict[str, int]:
    if record is None:
        return {}
    scores = record.get("move_scores")
    if not isinstance(scores, list):
        return {}
    parsed: dict[str, int] = {}
    for item in scores:
        if not isinstance(item, dict):
            continue
        move = normalize_move(item.get("move"))
        score = item.get("exact_score_side_to_move")
        if move is None or isinstance(score, bool) or not isinstance(score, int):
            continue
        parsed[move] = score
    return parsed


def exact_record_rank(record: dict[str, Any]) -> int:
    board = record.get("board")
    if isinstance(board, str):
        try:
            legal_moves = legal_moves_for_board(board)
        except ScriptError:
            legal_moves = set()
        scores = exact_move_scores(record)
        if legal_moves and scores and legal_moves.issubset(scores.keys()):
            return 2
    if exact_best_moves(record):
        return 1
    return 0


def legal_move_bucket_for_count(count: int, buckets: tuple[LegalMoveBucket, ...]) -> str:
    for bucket in buckets:
        if count >= bucket.minimum and (bucket.maximum is None or count <= bucket.maximum):
            return bucket.label
    return f"{count}+"


def empties_bucket_for_count(count: int, bucket_size: int) -> str:
    start = (count // bucket_size) * bucket_size
    end = start + bucket_size - 1
    return f"{start}-{end}"


def exact_status_for(
    exact_record: dict[str, Any] | None,
    *,
    legal_moves: set[str],
) -> tuple[str, bool]:
    if exact_record is None:
        return "no_exact", False
    move_scores = exact_move_scores(exact_record)
    has_complete = bool(move_scores) and legal_moves.issubset(move_scores.keys())
    if has_complete:
        return "complete_move_scores", True
    if exact_best_moves(exact_record):
        return "exact_best_only", False
    return "no_exact", False


def teacher_exact_status_for(teacher_move: str, exact_record: dict[str, Any] | None) -> str:
    best = exact_best_moves(exact_record)
    if not best:
        return "no_exact"
    if teacher_move in best:
        return "in_exact_best"
    return "not_in_exact_best"


def prepare_teacher_rows(config: SampleConfig) -> tuple[list[PreparedTeacherRow], dict[str, Any]]:
    cutoffs = phase_cutoffs_from_eval_config(config.eval_config)
    exact_load = (
        load_exact_by_board(config.exact_labels)
        if config.exact_labels
        else ExactLoadResult(by_board={}, stats={})
    )
    exact_by_board = exact_load.by_board
    sources = read_jsonl_sources(config.teacher_labels)
    stats: collections.Counter[str] = collections.Counter()
    selected_by_board: dict[str, PreparedTeacherRow] = {}
    phase_available: collections.Counter[str] = collections.Counter()

    stats["teacher_rows"] = len(sources)
    for source in sources:
        if not accepted_teacher_record(source.record):
            stats["unusable_teacher_rows"] += 1
            continue
        board_text = board_text_from_record(source.record)
        teacher_move = normalize_move(teacher_move_from_record(source.record))
        assert board_text is not None
        assert teacher_move is not None
        key = board_key(board_text)
        if key in selected_by_board:
            stats["duplicate_teacher_boards"] += 1
            continue
        split = split_for_source(source, config.seed)
        if config.split != "all" and split != config.split:
            stats["split_skipped"] += 1
            continue
        legal_moves = legal_moves_for_board(board_text)
        if teacher_move not in legal_moves:
            stats["illegal_teacher_move_skipped"] += 1
            continue
        occupied = occupied_count(board_text)
        empties = empty_count(board_text)
        phase = phase_for_occupied(occupied, cutoffs)
        source_bucket_value = source.record.get(config.bucket_field, "unknown")
        source_bucket = str(source_bucket_value) if source_bucket_value is not None else "unknown"
        exact_record = exact_by_board.get(key)
        exact_status, has_complete = exact_status_for(exact_record, legal_moves=legal_moves)
        teacher_exact_status = teacher_exact_status_for(teacher_move, exact_record)
        legal_move_count = len(legal_moves)
        row = PreparedTeacherRow(
            source=source,
            board_key=key,
            board_text=board_text,
            teacher_move=teacher_move,
            split=split,
            phase=phase,
            occupied_count=occupied,
            empty_count=empties,
            empties_bucket=empties_bucket_for_count(empties, config.empties_bucket_size),
            source_bucket=source_bucket,
            legal_move_count=legal_move_count,
            legal_move_bucket=legal_move_bucket_for_count(legal_move_count, config.legal_move_buckets),
            exact_status=exact_status,
            teacher_exact_status=teacher_exact_status,
            stratum_key=(
                phase,
                empties_bucket_for_count(empties, config.empties_bucket_size),
                source_bucket,
                legal_move_bucket_for_count(legal_move_count, config.legal_move_buckets),
                exact_status,
                teacher_exact_status,
            ),
            exact_record=exact_record,
            has_complete_move_scores=has_complete,
        )
        selected_by_board[key] = row
        phase_available[phase] += 1

    stats["usable_teacher_rows"] = len(selected_by_board)
    return list(selected_by_board.values()), {
        "stats": dict(stats),
        "exact_stats": exact_load.stats,
        "phase_available": {phase: phase_available[phase] for phase in PHASES},
        "phase_cutoffs": {
            "opening_max_occupied": cutoffs.opening_max_occupied,
            "midgame_max_occupied": cutoffs.midgame_max_occupied,
        },
    }


def stable_row_hash(row: PreparedTeacherRow, seed: int) -> str:
    material = "\n".join(
        (
            str(seed),
            row.board_key,
            row.teacher_move,
            str(row.source.path),
            str(row.source.line_number),
        )
    )
    return hashlib.sha256(material.encode("utf-8")).hexdigest()


def sample_rows(
    rows: list[PreparedTeacherRow],
    *,
    seed: int,
    phase_targets: dict[str, int],
) -> tuple[list[PreparedTeacherRow], dict[str, Any]]:
    by_phase_stratum: dict[str, dict[tuple[str, str, str, str, str, str], list[PreparedTeacherRow]]] = {
        phase: collections.defaultdict(list) for phase in PHASES
    }
    for row in rows:
        by_phase_stratum[row.phase][row.stratum_key].append(row)

    stratum_available: collections.Counter[str] = collections.Counter()
    for phase in PHASES:
        for stratum_key, stratum_rows in by_phase_stratum[phase].items():
            label = "|".join(stratum_key)
            stratum_available[label] = len(stratum_rows)
            stratum_rows.sort(key=lambda row: stable_row_hash(row, seed))

    selected: list[PreparedTeacherRow] = []
    selected_board_keys: set[str] = set()
    phase_selected: collections.Counter[str] = collections.Counter()
    stratum_selected: collections.Counter[str] = collections.Counter()

    for phase in PHASES:
        strata = by_phase_stratum[phase]
        ordered_keys = sorted(strata)
        target = phase_targets[phase]
        while phase_selected[phase] < target:
            progressed = False
            for key in ordered_keys:
                if phase_selected[phase] >= target:
                    break
                bucket = strata[key]
                while bucket and bucket[0].board_key in selected_board_keys:
                    bucket.pop(0)
                if not bucket:
                    continue
                row = bucket.pop(0)
                selected.append(row)
                selected_board_keys.add(row.board_key)
                phase_selected[phase] += 1
                stratum_selected["|".join(key)] += 1
                progressed = True
            if not progressed:
                break

    phase_shortage = {
        phase: max(0, phase_targets[phase] - phase_selected[phase])
        for phase in PHASES
    }
    underfilled = []
    if any(phase_shortage.values()):
        for label, available in stratum_available.items():
            phase = label.split("|", 1)[0]
            if phase_shortage.get(phase, 0) <= 0:
                continue
            underfilled.append(
                {
                    "stratum": label,
                    "available": available,
                    "selected": stratum_selected[label],
                    "phase_shortage": phase_shortage[phase],
                }
            )
    underfilled.sort(key=lambda item: (-int(item["phase_shortage"]), -int(item["available"]), item["stratum"]))
    return selected, {
        "phase_selected": {phase: phase_selected[phase] for phase in PHASES},
        "phase_shortage": phase_shortage,
        "stratum_count": len(stratum_available),
        "top_underfilled_strata": underfilled[:20],
    }


def _metadata_for_row(row: PreparedTeacherRow) -> dict[str, Any]:
    return {
        "phase": row.phase,
        "occupied_count": row.occupied_count,
        "empty_count": row.empty_count,
        "empties_bucket": row.empties_bucket,
        "source_bucket": row.source_bucket,
        "legal_move_count": row.legal_move_count,
        "legal_move_bucket": row.legal_move_bucket,
        "exact_status": row.exact_status,
        "teacher_exact_status": row.teacher_exact_status,
    }


def teacher_output_record(row: PreparedTeacherRow) -> dict[str, Any]:
    record = dict(row.source.record)
    record.update(_metadata_for_row(row))
    return record


def write_jsonl(path: Path, records: Iterable[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("w", encoding="utf-8") as output_file:
            for record in records:
                output_file.write(json.dumps(record, sort_keys=True) + "\n")
    except OSError as exc:
        raise ScriptError(f"failed to write {path}: {exc}") from exc


def _phase_counter_template() -> dict[str, int]:
    return {phase: 0 for phase in PHASES}


def _nested_distribution() -> dict[str, collections.Counter[str]]:
    return {phase: collections.Counter() for phase in PHASES}


def _counter_to_dict(counter: collections.Counter[Any]) -> dict[str, int]:
    return {str(key): int(value) for key, value in sorted(counter.items(), key=lambda item: str(item[0]))}


def phase_distributions(rows: list[PreparedTeacherRow]) -> dict[str, Any]:
    exact_coverage = _phase_counter_template()
    complete_coverage = _phase_counter_template()
    disagreement = _phase_counter_template()
    source_bucket_distribution = _nested_distribution()
    empties_bucket_distribution = _nested_distribution()
    legal_move_count_distribution: dict[str, collections.Counter[int]] = {
        phase: collections.Counter() for phase in PHASES
    }
    legal_move_bucket_distribution = _nested_distribution()

    for row in rows:
        if row.exact_record is not None:
            exact_coverage[row.phase] += 1
        if row.has_complete_move_scores:
            complete_coverage[row.phase] += 1
        if row.teacher_exact_status == "not_in_exact_best":
            disagreement[row.phase] += 1
        source_bucket_distribution[row.phase][row.source_bucket] += 1
        empties_bucket_distribution[row.phase][row.empties_bucket] += 1
        legal_move_count_distribution[row.phase][row.legal_move_count] += 1
        legal_move_bucket_distribution[row.phase][row.legal_move_bucket] += 1

    return {
        "exact_coverage": exact_coverage,
        "complete_move_scores_coverage": complete_coverage,
        "teacher_exact_disagreement": disagreement,
        "source_bucket_distribution": {
            phase: _counter_to_dict(source_bucket_distribution[phase]) for phase in PHASES
        },
        "empties_bucket_distribution": {
            phase: _counter_to_dict(empties_bucket_distribution[phase]) for phase in PHASES
        },
        "legal_move_count_distribution": {
            phase: _counter_to_dict(legal_move_count_distribution[phase]) for phase in PHASES
        },
        "legal_move_bucket_distribution": {
            phase: _counter_to_dict(legal_move_bucket_distribution[phase]) for phase in PHASES
        },
    }


def build_summary(
    config: SampleConfig,
    *,
    prepare_info: dict[str, Any],
    sample_info: dict[str, Any],
    selected: list[PreparedTeacherRow],
    teacher_output: Path,
    exact_output: Path | None,
    report_output: Path,
) -> dict[str, Any]:
    distributions = phase_distributions(selected)
    output_paths: dict[str, str] = {
        "teacher": str(teacher_output),
        "summary": str(config.out_dir / "summary.json"),
        "report": str(report_output),
    }
    if exact_output is not None:
        output_paths["exact"] = str(exact_output)
    return {
        "schema": "phase_balanced_label_sample.v1",
        "requested_rows": config.rows,
        "selected_rows": len(selected),
        "phase_targets": config.phase_targets,
        "phase_available_rows": prepare_info["phase_available"],
        "phase_selected_rows": sample_info["phase_selected"],
        "phase_shortage": sample_info["phase_shortage"],
        "phase_exact_coverage": distributions["exact_coverage"],
        "phase_complete_move_scores_coverage": distributions["complete_move_scores_coverage"],
        "phase_teacher_exact_disagreement": distributions["teacher_exact_disagreement"],
        "phase_source_bucket_distribution": distributions["source_bucket_distribution"],
        "phase_empties_bucket_distribution": distributions["empties_bucket_distribution"],
        "phase_legal_move_count_distribution": distributions["legal_move_count_distribution"],
        "phase_legal_move_bucket_distribution": distributions["legal_move_bucket_distribution"],
        "stratum_count": sample_info["stratum_count"],
        "top_underfilled_strata": sample_info["top_underfilled_strata"],
        "output_paths": output_paths,
        "no_strength_claim": True,
        "default_promotion": False,
        "seed": config.seed,
        "split": config.split,
        "bucket_field": config.bucket_field,
        "legal_move_buckets": [bucket.label for bucket in config.legal_move_buckets],
        "empties_bucket_size": config.empties_bucket_size,
        "phase_cutoffs": prepare_info["phase_cutoffs"],
        "teacher_stats": prepare_info["stats"],
        "exact_stats": prepare_info["exact_stats"],
        "allow_shortage": config.allow_shortage,
        "dataset_root": config.dataset_root,
        "invocation": config.invocation,
    }


def _format_distribution(distribution: dict[str, int]) -> str:
    if not distribution:
        return "(none)"
    return ", ".join(f"{key}: {value}" for key, value in distribution.items())


def render_report(summary: dict[str, Any]) -> str:
    lines = [
        "# Phase-Balanced Label Sample",
        "",
        "This diagnostic subset builder makes no strength claim and does not promote a default evaluator.",
        "",
        "## Summary",
        "",
        f"- requested_rows: `{summary['requested_rows']}`",
        f"- selected_rows: `{summary['selected_rows']}`",
        f"- no_strength_claim: `{str(summary['no_strength_claim']).lower()}`",
        f"- default_promotion: `{str(summary['default_promotion']).lower()}`",
        f"- allow_shortage: `{str(summary['allow_shortage']).lower()}`",
        f"- split: `{summary['split']}`",
        f"- seed: `{summary['seed']}`",
        "",
        "## Phase Counts",
        "",
        "| phase | target | available | selected | shortage | exact | complete_move_scores | teacher/exact disagreement |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for phase in PHASES:
        lines.append(
            "| "
            f"{phase} | "
            f"{summary['phase_targets'][phase]} | "
            f"{summary['phase_available_rows'][phase]} | "
            f"{summary['phase_selected_rows'][phase]} | "
            f"{summary['phase_shortage'][phase]} | "
            f"{summary['phase_exact_coverage'][phase]} | "
            f"{summary['phase_complete_move_scores_coverage'][phase]} | "
            f"{summary['phase_teacher_exact_disagreement'][phase]} |"
        )
    lines.extend(
        [
            "",
            "## Distributions",
            "",
        ]
    )
    for phase in PHASES:
        lines.extend(
            [
                f"### {phase}",
                "",
                f"- source_bucket: `{_format_distribution(summary['phase_source_bucket_distribution'][phase])}`",
                f"- empties_bucket: `{_format_distribution(summary['phase_empties_bucket_distribution'][phase])}`",
                f"- legal_move_count: `{_format_distribution(summary['phase_legal_move_count_distribution'][phase])}`",
                f"- legal_move_bucket: `{_format_distribution(summary['phase_legal_move_bucket_distribution'][phase])}`",
                "",
            ]
        )
    lines.extend(
        [
            "## Strata",
            "",
            f"- stratum_count: `{summary['stratum_count']}`",
            "",
            "## Exact Labels",
            "",
            f"- exact_stats: `{_format_distribution(summary['exact_stats'])}`",
            "",
            "Top underfilled strata:",
            "",
        ]
    )
    if summary["top_underfilled_strata"]:
        for item in summary["top_underfilled_strata"]:
            lines.append(
                "- "
                f"`{item['stratum']}` "
                f"available={item['available']} selected={item['selected']} "
                f"phase_shortage={item['phase_shortage']}"
            )
    else:
        lines.append("- `(none)`")
    lines.extend(
        [
            "",
            "## Outputs",
            "",
        ]
    )
    for key, path in summary["output_paths"].items():
        lines.append(f"- {key}: `{path}`")
    lines.append("")
    return "\n".join(lines)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        path.write_text(text, encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to write {path}: {exc}") from exc


def reject_shortage_unless_allowed(config: SampleConfig, sample_info: dict[str, Any]) -> None:
    shortages = {
        phase: count
        for phase, count in sample_info["phase_shortage"].items()
        if count > 0
    }
    if shortages and not config.allow_shortage:
        formatted = ", ".join(f"{phase}={count}" for phase, count in sorted(shortages.items()))
        raise ScriptError(f"phase target shortage ({formatted}); pass --allow-shortage to write a partial subset")


def run(config: SampleConfig) -> dict[str, Any]:
    prepared, prepare_info = prepare_teacher_rows(config)
    selected, sample_info = sample_rows(
        prepared,
        seed=config.seed,
        phase_targets=config.phase_targets,
    )
    reject_shortage_unless_allowed(config, sample_info)
    teacher_output = config.out_dir / "teacher_phase_balanced.jsonl"
    exact_output = config.out_dir / "exact_phase_balanced.jsonl" if config.exact_labels else None
    summary_output = config.out_dir / "summary.json"
    report_output = config.out_dir / "report.md"

    write_jsonl(teacher_output, (teacher_output_record(row) for row in selected))
    if exact_output is not None:
        exact_records = [row.exact_record for row in selected if row.exact_record is not None]
        write_jsonl(exact_output, (record for record in exact_records if record is not None))

    summary = build_summary(
        config,
        prepare_info=prepare_info,
        sample_info=sample_info,
        selected=selected,
        teacher_output=teacher_output,
        exact_output=exact_output,
        report_output=report_output,
    )
    write_text(summary_output, json.dumps(summary, indent=2, sort_keys=True) + "\n")
    write_text(report_output, render_report(summary))
    return summary


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        config = config_from_args(
            args,
            invocation=["phase_balanced_label_sample.py", *(sys.argv[1:] if argv is None else argv)],
        )
        run(config)
    except ScriptError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.exit_code
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
