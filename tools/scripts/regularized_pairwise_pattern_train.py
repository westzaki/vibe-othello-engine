#!/usr/bin/env python3
"""Train phase-specific pattern tables from teacher-vs-engine preferences.

This is a tooling-only trainer. It reads reusable teacher/exact JSONL artifacts,
derives pairwise preferences between the teacher move and the current engine
choice, and writes local candidate artifacts under runs/.
"""

from __future__ import annotations

import argparse
import collections
import csv
import datetime as dt
import hashlib
import json
import math
import random
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from common import ScriptError, parse_csv_values, quote_command
from dataset_paths import (
    DATASET_ROOT_ENV,
    is_dataset_reference,
    resolve_dataset_root,
    resolve_path_references,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EVAL_CONFIG = "data/eval/pattern_reboot_v0.eval"
DEFAULT_ANALYSIS_DEPTH = 1
PHASES = ("opening", "midgame", "late")
DEFAULT_PHASE_CUTOFFS = (20, 44)
SENTINEL_FAMILY = "corner_2x3"
SENTINEL_ENTRY = (0, 0)
INT16_LIMIT = 32767

CORNER_2X3_SPECS = (
    (0, 1, 2, 8, 9, 10),
    (7, 6, 5, 15, 14, 13),
    (56, 57, 58, 48, 49, 50),
    (63, 62, 61, 55, 54, 53),
)
CORNER_3X3_SPECS = (
    (0, 1, 2, 8, 9, 10, 16, 17, 18),
    (7, 6, 5, 15, 14, 13, 23, 22, 21),
    (56, 57, 58, 48, 49, 50, 40, 41, 42),
    (63, 62, 61, 55, 54, 53, 47, 46, 45),
)
EDGE_8_SPECS = (
    (0, 1, 2, 3, 4, 5, 6, 7),
    (56, 57, 58, 59, 60, 61, 62, 63),
    (0, 8, 16, 24, 32, 40, 48, 56),
    (7, 15, 23, 31, 39, 47, 55, 63),
)
EDGE_X_10_SPECS = (
    (0, 1, 2, 3, 4, 5, 6, 7, 9, 14),
    (56, 57, 58, 59, 60, 61, 62, 63, 49, 54),
    (0, 8, 16, 24, 32, 40, 48, 56, 9, 49),
    (7, 15, 23, 31, 39, 47, 55, 63, 14, 54),
)
DIAGONAL_8_SPECS = (
    (0, 9, 18, 27, 36, 45, 54, 63),
    (7, 14, 21, 28, 35, 42, 49, 56),
)
INNER_ROW_8_SPECS = (
    (8, 9, 10, 11, 12, 13, 14, 15),
    (48, 49, 50, 51, 52, 53, 54, 55),
    (1, 9, 17, 25, 33, 41, 49, 57),
    (6, 14, 22, 30, 38, 46, 54, 62),
)

PATTERN_SPECS: dict[str, tuple[tuple[int, ...], ...]] = {
    "corner_2x3": CORNER_2X3_SPECS,
    "corner_3x3": CORNER_3X3_SPECS,
    "edge_8": EDGE_8_SPECS,
    "edge_x_10": EDGE_X_10_SPECS,
    "diagonal_8": DIAGONAL_8_SPECS,
    "inner_row_8": INNER_ROW_8_SPECS,
}
FAMILY_ORDER = tuple(PATTERN_SPECS)
FAMILY_ALIASES: dict[str, tuple[str, ...]] = {
    "legacy": ("corner_2x3", "edge_8"),
    "broad_all": ("corner_3x3", "edge_8", "edge_x_10", "diagonal_8", "inner_row_8"),
    "all": FAMILY_ORDER,
    "corner_only": ("corner_2x3", "corner_3x3"),
    "edge_only": ("edge_8", "edge_x_10"),
}

FeatureKey = tuple[str, int]
WeightsByPhase = dict[str, dict[FeatureKey, float]]


@dataclass(frozen=True)
class LabelSource:
    path: Path
    line_number: int
    record: dict[str, Any]
    split_hint: str | None


@dataclass(frozen=True)
class PhaseCutoffs:
    opening_max_occupied: int
    midgame_max_occupied: int


@dataclass(frozen=True)
class AnalyzeResult:
    best_move: str | None
    root_scores: dict[str, int]


@dataclass(frozen=True)
class TrainerConfig:
    teacher_labels: tuple[Path, ...]
    exact_labels: tuple[Path, ...]
    eval_config: Path
    analyze_position: Path
    out_dir: Path
    families: tuple[str, ...]
    split: str
    loss: str
    l2: float
    epochs: int
    learning_rate: float
    max_abs_weight: float
    output_scale: float
    max_abs_output_weight: int
    seed: int
    phase_cutoffs: PhaseCutoffs
    dataset_root: dict[str, str] | None
    invocation: list[str]


@dataclass(frozen=True)
class PreferencePair:
    position_id: str
    source_path: Path
    source_line: int
    split: str
    phase: str
    board_text: str
    teacher_move: str
    engine_move: str
    features: dict[FeatureKey, int]
    exact_best_moves: tuple[str, ...]
    teacher_score: int | None
    engine_score: int | None


@dataclass(frozen=True)
class ValidationRecord:
    position_id: str
    source_path: Path
    source_line: int
    split: str
    phase: str
    status: str
    teacher_move: str
    engine_move: str
    exact_best_moves: tuple[str, ...]
    teacher_exact_best: bool | None
    engine_exact_best: bool | None
    model_margin: float | None
    loss: float | None


@dataclass(frozen=True)
class TrainingResult:
    summary: dict[str, Any]
    table_paths: dict[str, Path]
    candidate_eval_path: Path
    validation_path: Path
    report_path: Path


Analyzer = Callable[[TrainerConfig, str], AnalyzeResult]


def parse_non_negative_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a non-negative number") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be a non-negative number")
    return parsed


def parse_positive_float(value: str) -> float:
    parsed = parse_non_negative_float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive number")
    return parsed


def parse_non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a non-negative integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be a non-negative integer")
    return parsed


def parse_positive_int(value: str) -> int:
    parsed = parse_non_negative_int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Train regularized pairwise pattern tables from teacher labels."
    )
    parser.add_argument("--teacher-labels", required=True, help="comma-separated teacher JSONL paths")
    parser.add_argument(
        "--dataset-root",
        help=f"shared dataset root for dataset: references; overrides {DATASET_ROOT_ENV}",
    )
    parser.add_argument("--exact-labels", help="optional comma-separated exact-label JSONL paths")
    parser.add_argument(
        "--eval-config",
        default=DEFAULT_EVAL_CONFIG,
        help=f"pattern-first base .eval config (default: {DEFAULT_EVAL_CONFIG})",
    )
    parser.add_argument(
        "--analyze-position",
        default=str(Path("build") / "othello_analyze_position"),
        help="othello_analyze_position executable used to get the engine choice",
    )
    parser.add_argument("--out-dir", required=True, help="output directory under runs/")
    parser.add_argument(
        "--families",
        default="broad_all",
        help=(
            "comma-separated pattern families or aliases. Families: "
            f"{','.join(FAMILY_ORDER)}. Aliases: {','.join(sorted(FAMILY_ALIASES))}"
        ),
    )
    parser.add_argument(
        "--split",
        choices=("train", "validation", "holdout", "all"),
        default="train",
        help="teacher split to train/evaluate",
    )
    parser.add_argument("--loss", choices=("logistic", "hinge"), default="logistic")
    parser.add_argument("--l2", type=parse_non_negative_float, default=0.001)
    parser.add_argument("--epochs", type=parse_positive_int, default=5)
    parser.add_argument("--learning-rate", type=parse_positive_float, default=0.1)
    parser.add_argument("--max-abs-weight", type=parse_positive_float, default=8.0)
    parser.add_argument(
        "--output-scale",
        type=parse_positive_float,
        default=1.0,
        help="scale applied when quantizing learned float weights to integer TSV values",
    )
    parser.add_argument(
        "--max-abs-output-weight",
        type=parse_positive_int,
        default=INT16_LIMIT,
        help=f"integer TSV clamp after output scaling (default: {INT16_LIMIT})",
    )
    parser.add_argument("--seed", type=parse_non_negative_int, default=20260601)
    return parser.parse_args(argv)


def parse_label_paths(value: str, *, dataset_root: str | None = None) -> tuple[Path, ...]:
    return tuple(
        resolve_path_references(
            parse_csv_values(value, error_label="label path list"),
            explicit_root=dataset_root,
        )
    )


def parse_families(text: str) -> tuple[str, ...]:
    families: list[str] = []
    for part in parse_csv_values(text, error_label="pattern family list"):
        expanded = FAMILY_ALIASES.get(part, (part,))
        for family in expanded:
            if family not in PATTERN_SPECS:
                raise ScriptError(f"unknown pattern family: {family}")
            if family not in families:
                families.append(family)
    if not families:
        raise ScriptError("--families selected no pattern families")
    return tuple(families)


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


def read_eval_config_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to read eval config {path}: {exc}") from exc


def _parse_int_key(entries: dict[str, str], key: str, default: int) -> int:
    value = entries.get(key)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError as exc:
        raise ScriptError(f"{key} must be an integer in eval config") from exc


def phase_cutoffs_from_eval_config(path: Path) -> PhaseCutoffs:
    entries = eval_config_entries(read_eval_config_text(path))
    opening, midgame = DEFAULT_PHASE_CUTOFFS
    return PhaseCutoffs(
        opening_max_occupied=_parse_int_key(entries, "opening_max_occupied", opening),
        midgame_max_occupied=_parse_int_key(entries, "midgame_max_occupied", midgame),
    )


def phase_for_occupied(occupied_count: int, cutoffs: PhaseCutoffs) -> str:
    if occupied_count <= cutoffs.opening_max_occupied:
        return "opening"
    if occupied_count <= cutoffs.midgame_max_occupied:
        return "midgame"
    return "late"


def resolve_out_dir(path_text: str) -> Path:
    path = Path(path_text)
    resolved = path.resolve(strict=False)
    source_data = (REPO_ROOT / "data").resolve(strict=False)
    try:
        resolved.relative_to(source_data)
    except ValueError:
        pass
    else:
        raise ScriptError("--out-dir must not be under source-controlled data/")
    if "runs" not in path.parts and "runs" not in resolved.parts:
        raise ScriptError("--out-dir must be under runs/")
    return path


def config_from_args(
    args: argparse.Namespace,
    invocation: list[str] | None = None,
) -> TrainerConfig:
    if args.max_abs_output_weight > INT16_LIMIT:
        raise ScriptError(f"--max-abs-output-weight must be <= {INT16_LIMIT}")
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
    eval_config = Path(args.eval_config)
    return TrainerConfig(
        teacher_labels=teacher_labels,
        exact_labels=exact_labels,
        eval_config=eval_config,
        analyze_position=Path(args.analyze_position),
        out_dir=resolve_out_dir(args.out_dir),
        families=parse_families(args.families),
        split=args.split,
        loss=args.loss,
        l2=args.l2,
        epochs=args.epochs,
        learning_rate=args.learning_rate,
        max_abs_weight=args.max_abs_weight,
        output_scale=args.output_scale,
        max_abs_output_weight=args.max_abs_output_weight,
        seed=args.seed,
        phase_cutoffs=phase_cutoffs_from_eval_config(eval_config),
        dataset_root=dataset_root,
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


def read_jsonl_sources(paths: tuple[Path, ...]) -> list[LabelSource]:
    sources: list[LabelSource] = []
    for path in paths:
        split_hint = split_hint_from_path(path)
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


def split_hint_from_path(path: Path) -> str | None:
    for part in reversed(path.parts):
        lowered = part.lower()
        stem = Path(lowered).stem
        for split in ("train", "validation", "holdout"):
            if lowered == split or stem == split or lowered.startswith(f"{split}."):
                return split
    return None


def board_key(board_text: str) -> str:
    return "\n".join(line.rstrip() for line in board_text.strip().splitlines())


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


def load_exact_by_board(paths: tuple[Path, ...]) -> dict[str, dict[str, Any]]:
    exact: dict[str, dict[str, Any]] = {}
    for source in read_jsonl_sources(paths):
        board = source.record.get("board")
        if isinstance(board, str):
            exact[board_key(board)] = source.record
    return exact


def exact_best_moves(record: dict[str, Any] | None) -> tuple[str, ...]:
    if record is None:
        return ()
    moves = record.get("best_moves")
    if isinstance(moves, list):
        return tuple(sorted(move for move in (normalize_move(value) for value in moves) if move))
    move = normalize_move(record.get("best_move"))
    return (move,) if move else ()


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


def normalize_move(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text:
        return None
    if text in {"pass", "pa", "--", "-"}:
        return "pass"
    return text


def accepted_teacher_record(record: dict[str, Any]) -> bool:
    if record.get("status") not in (None, "ok"):
        return False
    if record.get("legal_move_valid") is False:
        return False
    if record.get("move_token_valid") is False:
        return False
    return board_text_from_record(record) is not None and normalize_move(teacher_move_from_record(record)) is not None


def position_id_for_source(source: LabelSource) -> str:
    record = source.record
    for key in ("position_id", "position_name", "source_id"):
        value = record.get(key)
        if value is not None:
            return str(value)
    if record.get("position_index") is not None:
        return f"position-{record['position_index']}"
    return f"{source.path.name}:{source.line_number}"


def parse_board(board_text: str) -> tuple[list[list[str]], str]:
    lines = [line.strip() for line in board_text.strip().splitlines() if line.strip()]
    if len(lines) != 9:
        raise ScriptError("expected board9 text with 8 rows plus side")
    rows = [list(line) for line in lines[:8]]
    for row in rows:
        if len(row) != 8 or any(cell not in {"B", "W", "."} for cell in row):
            raise ScriptError("expected 8 board rows containing only B, W, or .")
    side_line = lines[8]
    if not side_line.startswith("side=") or len(side_line) != 6 or side_line[-1] not in {"B", "W"}:
        raise ScriptError("expected board9 side line")
    return rows, side_line[-1]


def board_to_text(rows: list[list[str]], side: str) -> str:
    return "\n".join("".join(row) for row in rows) + f"\nside={side}"


def opponent(side: str) -> str:
    return "W" if side == "B" else "B"


def occupied_count(board_text: str) -> int:
    rows, _ = parse_board(board_text)
    return sum(1 for row in rows for cell in row if cell in {"B", "W"})


def empty_count(board_text: str) -> int:
    rows, _ = parse_board(board_text)
    return sum(row.count(".") for row in rows)


def phase_for_board(board_text: str, cutoffs: PhaseCutoffs) -> str:
    return phase_for_occupied(occupied_count(board_text), cutoffs)


def _inside(row: int, col: int) -> bool:
    return 0 <= row < 8 and 0 <= col < 8


def _move_to_coord(move: str) -> tuple[int, int] | None:
    if len(move) != 2 or move[0] < "a" or move[0] > "h" or move[1] < "1" or move[1] > "8":
        return None
    return 8 - int(move[1]), ord(move[0]) - ord("a")


def _coord_to_move(row: int, col: int) -> str:
    return f"{chr(ord('a') + col)}{8 - row}"


DIRECTIONS = (
    (-1, -1),
    (-1, 0),
    (-1, 1),
    (0, -1),
    (0, 1),
    (1, -1),
    (1, 0),
    (1, 1),
)


def _flips_for_move(rows: list[list[str]], side: str, row: int, col: int) -> list[tuple[int, int]]:
    if not _inside(row, col) or rows[row][col] != ".":
        return []
    other = opponent(side)
    flips: list[tuple[int, int]] = []
    for dr, dc in DIRECTIONS:
        current: list[tuple[int, int]] = []
        r = row + dr
        c = col + dc
        while _inside(r, c) and rows[r][c] == other:
            current.append((r, c))
            r += dr
            c += dc
        if current and _inside(r, c) and rows[r][c] == side:
            flips.extend(current)
    return flips


def legal_moves_for_board(board_text: str) -> set[str]:
    rows, side = parse_board(board_text)
    moves: set[str] = set()
    for row in range(8):
        for col in range(8):
            if _flips_for_move(rows, side, row, col):
                moves.add(_coord_to_move(row, col))
    if not moves:
        moves.add("pass")
    return moves


def apply_move_to_board(board_text: str, move: str) -> str:
    rows, side = parse_board(board_text)
    normalized = normalize_move(move)
    if normalized == "pass":
        if any(_flips_for_move(rows, side, row, col) for row in range(8) for col in range(8)):
            raise ScriptError("pass is not legal while a board move exists")
        return board_to_text(rows, opponent(side))
    if normalized is None:
        raise ScriptError("move cannot be empty")
    coord = _move_to_coord(normalized)
    if coord is None:
        raise ScriptError(f"invalid move coordinate: {move}")
    row, col = coord
    flips = _flips_for_move(rows, side, row, col)
    if not flips:
        raise ScriptError(f"illegal move for board: {move}")
    rows[row][col] = side
    for r, c in flips:
        rows[r][c] = side
    return board_to_text(rows, opponent(side))


def board_cell(rows: list[list[str]], square_index: int) -> str:
    return rows[square_index // 8][square_index % 8]


def pattern_index(rows: list[list[str]], side: str, spec: tuple[int, ...]) -> int:
    other = opponent(side)
    index = 0
    place = 1
    for square_index in spec:
        cell = board_cell(rows, square_index)
        state = 1 if cell == side else 2 if cell == other else 0
        index += state * place
        place *= 3
    return index


def pattern_counts(board_text: str, root_side: str, families: tuple[str, ...]) -> collections.Counter[FeatureKey]:
    rows, _ = parse_board(board_text)
    counts: collections.Counter[FeatureKey] = collections.Counter()
    for family in families:
        for spec in PATTERN_SPECS[family]:
            counts[(family, pattern_index(rows, root_side, spec))] += 1
    return counts


def preference_features(
    *,
    root_board_text: str,
    teacher_child_board: str,
    engine_child_board: str,
    families: tuple[str, ...],
) -> dict[FeatureKey, int]:
    _, root_side = parse_board(root_board_text)
    teacher_counts = pattern_counts(teacher_child_board, root_side, families)
    engine_counts = pattern_counts(engine_child_board, root_side, families)
    delta: dict[FeatureKey, int] = {}
    for key in set(teacher_counts) | set(engine_counts):
        value = teacher_counts[key] - engine_counts[key]
        if value:
            delta[key] = value
    return delta


def parse_int_value(value: str) -> int | None:
    try:
        return int(value)
    except ValueError:
        return None


def parse_analysis_stdout(text: str) -> AnalyzeResult:
    best_move: str | None = None
    root_scores: dict[str, int] = {}
    current_candidate: str | None = None
    in_root_candidates = False
    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped:
            continue
        if stripped == "root_candidates:":
            in_root_candidates = True
            current_candidate = None
            continue
        if not line.startswith(" "):
            key, separator, value = stripped.partition(":")
            if separator and key == "best_move":
                best_move = normalize_move(value)
            if stripped.endswith(":") and stripped != "root_candidates:":
                in_root_candidates = False
                current_candidate = None
            continue
        if not in_root_candidates:
            continue
        if line.startswith("  - move:"):
            current_candidate = normalize_move(stripped.split(":", 1)[1]) or "pass"
        elif current_candidate is not None and line.startswith("    score:"):
            score = parse_int_value(stripped.split(":", 1)[1].strip())
            if score is not None:
                root_scores[current_candidate] = score
    if best_move is None and root_scores:
        best_move = sorted(root_scores.items(), key=lambda item: (-item[1], item[0]))[0][0]
    return AnalyzeResult(best_move=best_move, root_scores=root_scores)


def analyze_command(config: TrainerConfig) -> list[str]:
    return [
        str(config.analyze_position),
        "--stdin",
        "--depth",
        str(DEFAULT_ANALYSIS_DEPTH),
        "--exact-endgame-threshold",
        "0",
        "--eval-config",
        str(config.eval_config),
        "--root-candidates",
    ]


def run_analysis(config: TrainerConfig, board_text: str) -> AnalyzeResult:
    command = analyze_command(config)
    completed = subprocess.run(
        command,
        input=board_text,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise ScriptError(
            f"analysis failed: {quote_command(command)}\n{completed.stderr}",
            exit_code=1,
        )
    return parse_analysis_stdout(completed.stdout)


def precomputed_engine_move(record: dict[str, Any], legal_moves: set[str]) -> str | None:
    for key in ("selected_move", "engine_selected_move", "search_best_move", "vibe_move", "engine_best_move"):
        move = normalize_move(record.get(key))
        if move is not None and move in legal_moves:
            return move
    return None


def _exact_bool(move: str, exact_best: tuple[str, ...]) -> bool | None:
    if not exact_best:
        return None
    return move in exact_best


def make_validation_record(
    *,
    source: LabelSource,
    split: str,
    phase: str,
    status: str,
    teacher_move: str,
    engine_move: str,
    exact_best: tuple[str, ...],
    model_margin: float | None = None,
    loss: float | None = None,
) -> ValidationRecord:
    return ValidationRecord(
        position_id=position_id_for_source(source),
        source_path=source.path,
        source_line=source.line_number,
        split=split,
        phase=phase,
        status=status,
        teacher_move=teacher_move,
        engine_move=engine_move,
        exact_best_moves=exact_best,
        teacher_exact_best=_exact_bool(teacher_move, exact_best),
        engine_exact_best=_exact_bool(engine_move, exact_best),
        model_margin=model_margin,
        loss=loss,
    )


def collect_pairs(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> tuple[list[PreferencePair], list[ValidationRecord], dict[str, int]]:
    sources = read_jsonl_sources(config.teacher_labels)
    exact_by_board = load_exact_by_board(config.exact_labels) if config.exact_labels else {}
    pairs: list[PreferencePair] = []
    validation_records: list[ValidationRecord] = []
    stats: collections.Counter[str] = collections.Counter()
    stats["teacher_rows"] = len(sources)

    for source in sources:
        record = source.record
        if not accepted_teacher_record(record):
            stats["unusable_teacher_rows"] += 1
            continue
        stats["accepted_teacher_rows"] += 1
        split = split_for_source(source, config.seed)
        stats[f"{split}_rows"] += 1
        if config.split != "all" and split != config.split:
            stats["split_skipped"] += 1
            continue

        board_text = board_text_from_record(record)
        assert board_text is not None
        phase = phase_for_board(board_text, config.phase_cutoffs)
        teacher_move = normalize_move(teacher_move_from_record(record))
        if teacher_move is None:
            stats["missing_teacher_move_skipped"] += 1
            continue
        legal_moves = legal_moves_for_board(board_text)
        if teacher_move not in legal_moves:
            stats["illegal_teacher_move_skipped"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="illegal_teacher_move",
                    teacher_move=teacher_move,
                    engine_move="",
                    exact_best=(),
                )
            )
            continue

        exact_record = exact_by_board.get(board_key(board_text))
        exact_best = exact_best_moves(exact_record)
        if exact_best and teacher_move not in exact_best:
            stats["teacher_exact_disagreements_skipped"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="teacher_exact_disagreement",
                    teacher_move=teacher_move,
                    engine_move="",
                    exact_best=exact_best,
                )
            )
            continue

        analysis: AnalyzeResult | None = None
        engine_move = precomputed_engine_move(record, legal_moves)
        if engine_move is None:
            analysis = analyzer(config, board_text)
            engine_move = analysis.best_move
        if engine_move is None or engine_move not in legal_moves:
            stats["missing_engine_move_skipped"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="missing_engine_move",
                    teacher_move=teacher_move,
                    engine_move=engine_move or "",
                    exact_best=exact_best,
                )
            )
            continue
        if engine_move == teacher_move:
            stats["already_agreed"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="already_agreed",
                    teacher_move=teacher_move,
                    engine_move=engine_move,
                    exact_best=exact_best,
                )
            )
            continue

        try:
            teacher_child = apply_move_to_board(board_text, teacher_move)
            engine_child = apply_move_to_board(board_text, engine_move)
        except ScriptError as exc:
            stats["apply_move_skipped"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status=f"apply_move_error: {exc}",
                    teacher_move=teacher_move,
                    engine_move=engine_move,
                    exact_best=exact_best,
                )
            )
            continue

        features = preference_features(
            root_board_text=board_text,
            teacher_child_board=teacher_child,
            engine_child_board=engine_child,
            families=config.families,
        )
        if not features:
            stats["zero_delta_skipped"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="zero_feature_delta",
                    teacher_move=teacher_move,
                    engine_move=engine_move,
                    exact_best=exact_best,
                )
            )
            continue

        root_scores = analysis.root_scores if analysis is not None else {}
        pair = PreferencePair(
            position_id=position_id_for_source(source),
            source_path=source.path,
            source_line=source.line_number,
            split=split,
            phase=phase,
            board_text=board_text,
            teacher_move=teacher_move,
            engine_move=engine_move,
            features=features,
            exact_best_moves=exact_best,
            teacher_score=root_scores.get(teacher_move),
            engine_score=root_scores.get(engine_move),
        )
        pairs.append(pair)
        validation_records.append(
            make_validation_record(
                source=source,
                split=split,
                phase=phase,
                status="paired",
                teacher_move=teacher_move,
                engine_move=engine_move,
                exact_best=exact_best,
            )
        )
        stats["preference_pairs"] += 1
        stats[f"{phase}_pairs"] += 1

    for key in (
        "accepted_teacher_rows",
        "already_agreed",
        "preference_pairs",
        "split_skipped",
        "teacher_exact_disagreements_skipped",
        "unusable_teacher_rows",
    ):
        stats.setdefault(key, 0)
    for split in ("train", "validation", "holdout"):
        stats.setdefault(f"{split}_rows", 0)
    for phase in PHASES:
        stats.setdefault(f"{phase}_pairs", 0)
    return pairs, validation_records, {key: int(stats[key]) for key in sorted(stats)}


def stable_sigmoid_negative_margin(margin: float) -> float:
    if margin >= 0:
        exp_neg = math.exp(-margin)
        return exp_neg / (1.0 + exp_neg)
    exp_pos = math.exp(margin)
    return 1.0 / (1.0 + exp_pos)


def pair_margin(weights: WeightsByPhase, pair: PreferencePair) -> float:
    phase_weights = weights.get(pair.phase, {})
    return sum(phase_weights.get(key, 0.0) * value for key, value in pair.features.items())


def pair_loss(loss_name: str, margin: float) -> float:
    if loss_name == "hinge":
        return max(0.0, 1.0 - margin)
    if margin > 50.0:
        return math.exp(-margin)
    if margin < -50.0:
        return -margin
    return math.log1p(math.exp(-margin))


def shrink_phase_weights(phase_weights: dict[FeatureKey, float], *, learning_rate: float, l2: float) -> None:
    if l2 == 0.0 or not phase_weights:
        return
    factor = max(0.0, 1.0 - learning_rate * l2)
    for key in list(phase_weights):
        phase_weights[key] *= factor
        if abs(phase_weights[key]) < 1.0e-12:
            del phase_weights[key]


def clamp_weight(value: float, maximum: float) -> float:
    return max(-maximum, min(maximum, value))


def train_weights(config: TrainerConfig, pairs: list[PreferencePair]) -> tuple[WeightsByPhase, list[dict[str, Any]]]:
    weights: WeightsByPhase = {phase: {} for phase in PHASES}
    history: list[dict[str, Any]] = []
    if not pairs:
        for epoch in range(1, config.epochs + 1):
            history.append({"epoch": epoch, "pairs": 0, "loss": 0.0, "accuracy": 0.0})
        return weights, history

    rng = random.Random(config.seed)
    for epoch in range(1, config.epochs + 1):
        shuffled = list(pairs)
        rng.shuffle(shuffled)
        for pair in shuffled:
            phase_weights = weights[pair.phase]
            shrink_phase_weights(
                phase_weights,
                learning_rate=config.learning_rate,
                l2=config.l2,
            )
            margin = pair_margin(weights, pair)
            if config.loss == "hinge":
                factor = 1.0 if margin < 1.0 else 0.0
            else:
                factor = stable_sigmoid_negative_margin(margin)
            if factor == 0.0:
                continue
            for key, value in pair.features.items():
                current = phase_weights.get(key, 0.0)
                updated = current + config.learning_rate * factor * value
                phase_weights[key] = clamp_weight(updated, config.max_abs_weight)
        metrics = evaluate_pairs(config, weights, pairs)
        history.append({"epoch": epoch, **metrics})
    return weights, history


def evaluate_pairs(
    config: TrainerConfig,
    weights: WeightsByPhase,
    pairs: list[PreferencePair],
) -> dict[str, Any]:
    if not pairs:
        return {"pairs": 0, "loss": 0.0, "accuracy": 0.0, "avg_margin": 0.0}
    losses = []
    correct = 0
    margins = []
    for pair in pairs:
        margin = pair_margin(weights, pair)
        margins.append(margin)
        losses.append(pair_loss(config.loss, margin))
        if margin > 0.0:
            correct += 1
    l2_term = 0.0
    for phase_weights in weights.values():
        l2_term += sum(value * value for value in phase_weights.values())
    return {
        "pairs": len(pairs),
        "loss": sum(losses) / len(losses) + 0.5 * config.l2 * l2_term,
        "accuracy": correct / len(pairs),
        "avg_margin": sum(margins) / len(margins),
    }


def format_weight(value: float) -> str:
    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.6f}".rstrip("0").rstrip(".") if value != 0.0 else "0"


def quantize_output_weight(value: float, config: TrainerConfig) -> int:
    scaled = int(round(value * config.output_scale))
    clamp = min(config.max_abs_output_weight, INT16_LIMIT)
    return max(-clamp, min(clamp, scaled))


def phase_entries(weights: WeightsByPhase, phase: str) -> list[tuple[str, int, float]]:
    entries = [
        (family, index, value)
        for (family, index), value in weights.get(phase, {}).items()
        if abs(value) >= 1.0e-12
    ]
    order = {family: index for index, family in enumerate(FAMILY_ORDER)}
    return sorted(entries, key=lambda item: (order[item[0]], item[1]))


def render_phase_table(
    *,
    config: TrainerConfig,
    phase: str,
    entries: list[tuple[str, int, float]],
    stats: dict[str, Any],
) -> str:
    lines = [
        "# schema_version: pattern_table.v1",
        "# generated_by: tools/scripts/regularized_pairwise_pattern_train.py",
        f"# phase: {phase}",
        "# no_strength_claim: true",
        "# not_default_promotion: true",
        f"# loss: {config.loss}",
        f"# l2: {config.l2}",
        f"# epochs: {config.epochs}",
        f"# learning_rate: {config.learning_rate}",
        f"# max_abs_weight: {config.max_abs_weight}",
        f"# output_scale: {config.output_scale}",
        f"# max_abs_output_weight: {config.max_abs_output_weight}",
        f"# families: {','.join(config.families)}",
    ]
    for key in sorted(stats):
        lines.append(f"# {key}: {stats[key]}")
    lines.append("")
    data_entries = entries or [(SENTINEL_FAMILY, SENTINEL_ENTRY[0], SENTINEL_ENTRY[1])]
    for family, index, weight in data_entries:
        lines.append(f"{family}\t{index}\t{quantize_output_weight(weight, config)}")
    return "\n".join(lines) + "\n"


def render_candidate_eval(config: TrainerConfig) -> str:
    base_text = read_eval_config_text(config.eval_config)
    lines = [
        "# generated_by: tools/scripts/regularized_pairwise_pattern_train.py",
        "# no_strength_claim: true",
        "# not_default_promotion: true",
        "# trainer_foundation: true",
    ]
    inserted_tables = False
    for raw_line in base_text.splitlines():
        parsed = _line_key_value(raw_line)
        if parsed is not None:
            key, _ = parsed
            if key == "pattern_table" or key.startswith("pattern_table."):
                continue
            if key == "name":
                lines.append("name=regularized_pairwise_pattern_candidate")
                lines.extend(
                    (
                        "pattern_table.opening=tables/opening.tsv",
                        "pattern_table.midgame=tables/midgame.tsv",
                        "pattern_table.late=tables/late.tsv",
                    )
                )
                inserted_tables = True
                continue
        lines.append(raw_line)
    if not inserted_tables:
        lines.extend(
            (
                "name=regularized_pairwise_pattern_candidate",
                "pattern_table.opening=tables/opening.tsv",
                "pattern_table.midgame=tables/midgame.tsv",
                "pattern_table.late=tables/late.tsv",
            )
        )
    return "\n".join(lines).rstrip() + "\n"


def validation_rows_with_margins(
    config: TrainerConfig,
    weights: WeightsByPhase,
    pairs: list[PreferencePair],
    records: list[ValidationRecord],
) -> list[ValidationRecord]:
    pair_by_source = {
        (pair.source_path, pair.source_line): pair
        for pair in pairs
    }
    updated: list[ValidationRecord] = []
    for record in records:
        pair = pair_by_source.get((record.source_path, record.source_line))
        if pair is None:
            updated.append(record)
            continue
        margin = pair_margin(weights, pair)
        updated.append(
            ValidationRecord(
                position_id=record.position_id,
                source_path=record.source_path,
                source_line=record.source_line,
                split=record.split,
                phase=record.phase,
                status=record.status,
                teacher_move=record.teacher_move,
                engine_move=record.engine_move,
                exact_best_moves=record.exact_best_moves,
                teacher_exact_best=record.teacher_exact_best,
                engine_exact_best=record.engine_exact_best,
                model_margin=margin,
                loss=pair_loss(config.loss, margin),
            )
        )
    return updated


def write_validation_tsv(path: Path, records: list[ValidationRecord]) -> None:
    fieldnames = [
        "position_id",
        "source_path",
        "source_line",
        "split",
        "phase",
        "status",
        "teacher_move",
        "engine_move",
        "exact_best_moves",
        "teacher_exact_best",
        "engine_exact_best",
        "model_margin",
        "loss",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        for record in records:
            writer.writerow(
                {
                    "position_id": record.position_id,
                    "source_path": str(record.source_path),
                    "source_line": record.source_line,
                    "split": record.split,
                    "phase": record.phase,
                    "status": record.status,
                    "teacher_move": record.teacher_move,
                    "engine_move": record.engine_move,
                    "exact_best_moves": " ".join(record.exact_best_moves),
                    "teacher_exact_best": ""
                    if record.teacher_exact_best is None
                    else str(record.teacher_exact_best).lower(),
                    "engine_exact_best": ""
                    if record.engine_exact_best is None
                    else str(record.engine_exact_best).lower(),
                    "model_margin": "" if record.model_margin is None else format_weight(record.model_margin),
                    "loss": "" if record.loss is None else format_weight(record.loss),
                }
            )


def collect_git_sha() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def summarize_weights(config: TrainerConfig, weights: WeightsByPhase) -> dict[str, Any]:
    phases: dict[str, Any] = {}
    for phase in PHASES:
        entries = phase_entries(weights, phase)
        by_family = collections.Counter(family for family, _, _ in entries)
        quantized = [quantize_output_weight(weight, config) for _, _, weight in entries]
        phases[phase] = {
            "entries": len(entries),
            "entries_by_family": {family: int(by_family[family]) for family in config.families},
            "max_abs_weight": max((abs(weight) for _, _, weight in entries), default=0.0),
            "max_abs_output_weight": max((abs(weight) for weight in quantized), default=0),
        }
    return phases


def render_report(
    *,
    config: TrainerConfig,
    summary: dict[str, Any],
    table_paths: dict[str, Path],
    candidate_eval_path: Path,
    validation_path: Path,
) -> str:
    rows = summary["rows"]
    final_metrics = summary["training"]["final_metrics"]
    lines = [
        "# Regularized Pairwise Pattern Trainer Report",
        "",
        "This is a trainer foundation artifact. It is not a strength claim, not an Elo estimate, "
        "and not a default-promotion recommendation.",
        "",
        "## Command",
        "",
        f"`{quote_command(config.invocation) if config.invocation else 'unknown'}`",
        "",
        "## Inputs",
        "",
        f"- teacher_labels: `{', '.join(str(path) for path in config.teacher_labels)}`",
        f"- exact_labels: `{', '.join(str(path) for path in config.exact_labels) or 'none'}`",
        f"- dataset_root: `{json.dumps(config.dataset_root, sort_keys=True)}`",
        f"- eval_config: `{config.eval_config}`",
        f"- analyze_position: `{config.analyze_position}`",
        f"- analysis_depth: `{DEFAULT_ANALYSIS_DEPTH}`",
        "",
        "## Training",
        "",
        f"- split: `{config.split}`",
        f"- families: `{', '.join(config.families)}`",
        f"- loss: `{config.loss}`",
        f"- l2: `{config.l2}`",
        f"- epochs: `{config.epochs}`",
        f"- learning_rate: `{config.learning_rate}`",
        f"- max_abs_weight: `{config.max_abs_weight}`",
        f"- output_scale: `{config.output_scale}`",
        f"- max_abs_output_weight: `{config.max_abs_output_weight}`",
        f"- seed: `{config.seed}`",
        f"- phase_cutoffs: opening <= `{config.phase_cutoffs.opening_max_occupied}`, "
        f"midgame <= `{config.phase_cutoffs.midgame_max_occupied}`, else late",
        "",
        "## Counts",
        "",
    ]
    for key in sorted(rows):
        lines.append(f"- {key}: `{rows[key]}`")
    lines.extend(
        [
            "",
            "## Final Metrics",
            "",
            f"- pairs: `{final_metrics['pairs']}`",
            f"- loss: `{final_metrics['loss']:.6f}`",
            f"- accuracy: `{final_metrics['accuracy']:.6f}`",
            f"- avg_margin: `{final_metrics['avg_margin']:.6f}`",
            "",
            "## Outputs",
            "",
        ]
    )
    for phase in PHASES:
        lines.append(f"- {phase}_table: `{table_paths[phase]}`")
    lines.extend(
        [
            f"- candidate_eval: `{candidate_eval_path}`",
            f"- summary_json: `{config.out_dir / 'summary.json'}`",
            f"- validation_tsv: `{validation_path}`",
            "- `validation.tsv` contains diagnostics for the selected split; it is not "
            "necessarily held-out validation.",
            "",
            "## Caveats",
            "",
            "- Generated `.eval` and TSV files belong under `runs/` and should not be copied into `data/eval`.",
            "- `candidate.eval` is a local experiment candidate and does not change `current_default.eval`.",
            "- The trainer learns pattern-table deltas only from the selected teacher-vs-engine pairs.",
            "- Follow-up validation must use held-out labels, search/match checks, and external sanity before any strength claim.",
        ]
    )
    return "\n".join(lines) + "\n"


def train_pairwise_tables(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> TrainingResult:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    table_dir = config.out_dir / "tables"
    table_dir.mkdir(parents=True, exist_ok=True)

    pairs, validation_records, row_stats = collect_pairs(config, analyzer=analyzer)
    weights, history = train_weights(config, pairs)
    validation_records = validation_rows_with_margins(config, weights, pairs, validation_records)
    final_metrics = evaluate_pairs(config, weights, pairs)

    table_paths = {phase: table_dir / f"{phase}.tsv" for phase in PHASES}
    for phase in PHASES:
        entries = phase_entries(weights, phase)
        phase_stats = {
            "paired_rows": row_stats.get(f"{phase}_pairs", 0),
            "entries": len(entries),
        }
        table_paths[phase].write_text(
            render_phase_table(
                config=config,
                phase=phase,
                entries=entries,
                stats=phase_stats,
            ),
            encoding="utf-8",
        )

    candidate_eval_path = config.out_dir / "candidate.eval"
    candidate_eval_path.write_text(render_candidate_eval(config), encoding="utf-8")

    validation_path = config.out_dir / "validation.tsv"
    write_validation_tsv(validation_path, validation_records)

    summary = {
        "script": "tools/scripts/regularized_pairwise_pattern_train.py",
        "generated_at": dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "git_sha": collect_git_sha(),
        "command": quote_command(config.invocation) if config.invocation else "unknown",
        "teacher_label_paths": [str(path) for path in config.teacher_labels],
        "teacher_label_sha256": {str(path): sha256_file(path) for path in config.teacher_labels},
        "exact_label_paths": [str(path) for path in config.exact_labels],
        "exact_label_sha256": {str(path): sha256_file(path) for path in config.exact_labels},
        "dataset_root": config.dataset_root,
        "eval_config": str(config.eval_config),
        "eval_config_sha256": sha256_file(config.eval_config),
        "analyze_position": str(config.analyze_position),
        "analysis_depth": DEFAULT_ANALYSIS_DEPTH,
        "families": list(config.families),
        "split": config.split,
        "loss": config.loss,
        "l2": config.l2,
        "epochs": config.epochs,
        "learning_rate": config.learning_rate,
        "max_abs_weight": config.max_abs_weight,
        "output_scale": config.output_scale,
        "max_abs_output_weight": config.max_abs_output_weight,
        "seed": config.seed,
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
        "rows": row_stats,
        "training": {
            "history": history,
            "final_metrics": final_metrics,
            "weights": summarize_weights(config, weights),
        },
        "outputs": {
            "tables": {phase: str(path) for phase, path in table_paths.items()},
            "candidate_eval": str(candidate_eval_path),
            "summary_json": str(config.out_dir / "summary.json"),
            "report_md": str(config.out_dir / "report.md"),
            "validation_tsv": str(validation_path),
        },
        "no_strength_claim": True,
        "default_promotion": False,
    }

    summary_path = config.out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    report_path = config.out_dir / "report.md"
    report_path.write_text(
        render_report(
            config=config,
            summary=summary,
            table_paths=table_paths,
            candidate_eval_path=candidate_eval_path,
            validation_path=validation_path,
        ),
        encoding="utf-8",
    )
    return TrainingResult(
        summary=summary,
        table_paths=table_paths,
        candidate_eval_path=candidate_eval_path,
        validation_path=validation_path,
        report_path=report_path,
    )


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [
            "tools/scripts/regularized_pairwise_pattern_train.py",
            *(argv if argv is not None else sys.argv[1:]),
        ]
        config = config_from_args(args, invocation=invocation)
        result = train_pairwise_tables(config)
        print(f"wrote {config.out_dir} candidate_eval={result.candidate_eval_path}")
        return 0
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
