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
from pattern_specs import (
    COMMON_FAMILY_ALIASES,
    FAMILY_ORDER,
    PATTERN_SPECS,
    board9_rows_to_square_index_rows,
    pattern_index,
)
from pattern_training.analysis_cache import (
    AnalysisCacheConfig,
    AnalysisRequest,
    AnalysisRunnerConfig,
    analysis_cache_key,
    collect_git_sha,
    sha256_file,
    sha256_text,
    write_analysis_cache_row as shared_write_analysis_cache_row,
)
from pattern_training.analysis_cache import analyze_requests as shared_analyze_requests
from pattern_training.analyzer import AnalyzerConfig
from pattern_training.analyzer import analyze_command as shared_analyze_command
from pattern_training.analyzer import run_analysis as shared_run_analysis
from pattern_training.root_candidates import RootAnalysis as AnalyzeResult
from pattern_training.root_candidates import parse_analysis_stdout


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ANALYSIS_DEPTH = 1
PHASES = ("opening", "midgame", "late")
DEFAULT_PHASE_CUTOFFS = (20, 44)
SENTINEL_FAMILY = "corner_2x3"
SENTINEL_ENTRY = (0, 0)
INT16_LIMIT = 32767
PAIR_MODES = (
    "best-vs-engine",
    "best-vs-all",
    "rank-weighted",
    "teacher-vs-ranked-above",
    "exact-aware",
)
PAIR_WEIGHTINGS = ("uniform", "rank-margin", "score-margin", "exact-boost")
FAMILY_ALIASES: dict[str, tuple[str, ...]] = {
    **COMMON_FAMILY_ALIASES,
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
class PreparedPosition:
    source_index: int
    source: LabelSource
    split: str
    phase: str
    board_text: str
    teacher_move: str
    engine_move: str | None
    legal_moves: set[str]
    exact_best: tuple[str, ...]
    needs_analysis: bool


@dataclass(frozen=True)
class TrainerConfig:
    teacher_labels: tuple[Path, ...]
    exact_labels: tuple[Path, ...]
    eval_config: Path
    analyze_position: Path
    out_dir: Path
    analysis_cache: AnalysisCacheConfig
    analysis_jobs: int
    families: tuple[str, ...]
    split: str
    loss: str
    pair_mode: str
    pair_weighting: str
    max_pairs_per_position: int
    exact_best_weight: float
    teacher_weight: float
    min_score_margin: int
    l2: float
    epochs: int
    learning_rate: float
    max_abs_weight: float
    output_scale: float
    max_abs_output_weight: int
    candidate_pattern_table_weight: int
    include_base_margin: bool
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
    preferred_move: str
    other_move: str
    pair_kind: str
    pair_weight: float
    features: dict[FeatureKey, int]
    exact_best_moves: tuple[str, ...]
    preferred_score: int | None
    other_score: int | None
    rank_margin: int | None
    score_margin: int | None


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
    preferred_move: str
    other_move: str
    pair_kind: str
    pair_weight: float | None
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
        required=True,
        help="required base .eval config selected intentionally for this training run",
    )
    parser.add_argument(
        "--analyze-position",
        default=str(Path("build") / "othello_analyze_position"),
        help="othello_analyze_position executable used to get the engine choice",
    )
    parser.add_argument(
        "--analysis-cache-dir",
        help="optional directory for root-analysis cache JSONL artifacts",
    )
    parser.add_argument(
        "--analysis-cache-mode",
        choices=("off", "read-write", "read-only", "refresh"),
        default="off",
        help="root-analysis cache mode (default: off)",
    )
    parser.add_argument(
        "--analysis-jobs",
        type=parse_positive_int,
        default=1,
        help="maximum concurrent root-analysis subprocesses (default: 1)",
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
    parser.add_argument(
        "--pair-mode",
        choices=PAIR_MODES,
        default="best-vs-engine",
        help=(
            "preference-pair generation mode: best-vs-engine preserves the original "
            "teacher-vs-engine behavior; best-vs-all compares the teacher move with "
            "all candidate moves; rank-weighted uses lower-ranked root candidates; "
            "teacher-vs-ranked-above compares the teacher move only with root "
            "candidates scored above it by the base evaluator; exact-aware prefers "
            "exact-best moves when exact labels are available"
        ),
    )
    parser.add_argument(
        "--pair-weighting",
        choices=PAIR_WEIGHTINGS,
        default="uniform",
        help="pair weight policy used by the trainer objective",
    )
    parser.add_argument(
        "--max-pairs-per-position",
        type=parse_non_negative_int,
        default=0,
        help="cap generated pairs per position; 0 means no cap",
    )
    parser.add_argument(
        "--exact-best-weight",
        type=parse_positive_float,
        default=2.0,
        help="base weight for exact-best preference pairs when exact-aware weighting is used",
    )
    parser.add_argument(
        "--teacher-weight",
        type=parse_positive_float,
        default=1.0,
        help="base weight for teacher fallback pairs when exact-boost weighting is used",
    )
    parser.add_argument(
        "--min-score-margin",
        type=parse_non_negative_int,
        default=0,
        help="drop pairs with smaller root score margins when both candidate scores are available",
    )
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
    parser.add_argument(
        "--candidate-pattern-table-weight",
        type=parse_positive_int,
        default=1,
        help=(
            "phase pattern_table weight inserted into candidate eval when the base "
            ".eval has no phase-specific pattern_table weights"
        ),
    )
    parser.add_argument(
        "--no-base-margin",
        action="store_true",
        help=(
            "optimize learned delta margins only; by default the pairwise objective "
            "optimizes base evaluator score margin plus learned pattern delta"
        ),
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
    analysis_cache_dir = Path(args.analysis_cache_dir) if args.analysis_cache_dir else None
    if args.analysis_cache_mode != "off" and analysis_cache_dir is None:
        raise ScriptError("--analysis-cache-dir is required unless --analysis-cache-mode=off")
    if args.analysis_cache_mode == "off" and analysis_cache_dir is not None:
        raise ScriptError("--analysis-cache-mode must not be off when --analysis-cache-dir is provided")
    return TrainerConfig(
        teacher_labels=teacher_labels,
        exact_labels=exact_labels,
        eval_config=eval_config,
        analyze_position=Path(args.analyze_position),
        out_dir=resolve_out_dir(args.out_dir),
        analysis_cache=AnalysisCacheConfig(
            directory=analysis_cache_dir,
            mode=args.analysis_cache_mode,
        ),
        analysis_jobs=args.analysis_jobs,
        families=parse_families(args.families),
        split=args.split,
        loss=args.loss,
        pair_mode=args.pair_mode,
        pair_weighting=args.pair_weighting,
        max_pairs_per_position=args.max_pairs_per_position,
        exact_best_weight=args.exact_best_weight,
        teacher_weight=args.teacher_weight,
        min_score_margin=args.min_score_margin,
        l2=args.l2,
        epochs=args.epochs,
        learning_rate=args.learning_rate,
        max_abs_weight=args.max_abs_weight,
        output_scale=args.output_scale,
        max_abs_output_weight=args.max_abs_output_weight,
        candidate_pattern_table_weight=args.candidate_pattern_table_weight,
        include_base_margin=not args.no_base_margin,
        seed=args.seed,
        phase_cutoffs=phase_cutoffs_from_eval_config(eval_config),
        dataset_root=dataset_root,
        invocation=invocation or [],
    )


def write_analysis_cache_row(
    *,
    cache_dir: Path,
    config: TrainerConfig,
    request: AnalysisRequest,
    result: AnalyzeResult,
    eval_config_hash: str,
    analyzer_hash: str | None,
    git_sha: str,
) -> None:
    shared_write_analysis_cache_row(
        cache_dir=cache_dir,
        config=AnalysisRunnerConfig(
            analysis_cache=config.analysis_cache,
            analysis_jobs=config.analysis_jobs,
            analyze_position=config.analyze_position,
            eval_config=config.eval_config,
            analysis_depth=DEFAULT_ANALYSIS_DEPTH,
        ),
        request=request,
        result=result,
        eval_config_hash=eval_config_hash,
        analyzer_hash=analyzer_hash,
        git_sha=git_sha,
    )


def analyze_requests(
    *,
    config: TrainerConfig,
    requests: list[AnalysisRequest],
    analyzer: Analyzer,
    stats: collections.Counter[str],
    eval_config_hash: str,
) -> dict[int, AnalyzeResult]:
    return shared_analyze_requests(
        config=AnalysisRunnerConfig(
            analysis_cache=config.analysis_cache,
            analysis_jobs=config.analysis_jobs,
            analyze_position=config.analyze_position,
            eval_config=config.eval_config,
            analysis_depth=DEFAULT_ANALYSIS_DEPTH,
        ),
        requests=requests,
        analyzer=lambda board_text: analyzer(config, board_text),
        stats=stats,
        eval_config_hash=eval_config_hash,
    )


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


def pattern_counts(board_text: str, root_side: str, families: tuple[str, ...]) -> collections.Counter[FeatureKey]:
    rows, _ = parse_board(board_text)
    square_index_rows = board9_rows_to_square_index_rows(rows)
    counts: collections.Counter[FeatureKey] = collections.Counter()
    for family in families:
        for spec in PATTERN_SPECS[family]:
            counts[(family, pattern_index(square_index_rows, root_side, spec))] += 1
    return counts


def preference_features(
    *,
    root_board_text: str,
    teacher_child_board: str,
    engine_child_board: str,
    families: tuple[str, ...],
) -> dict[FeatureKey, int]:
    _, root_side = parse_board(root_board_text)
    child_side = opponent(root_side)
    # Root move scores are the negated child-position search scores. For a
    # preferred root move, increasing its root score therefore means decreasing
    # the child-side evaluation relative to the compared child.
    teacher_counts = pattern_counts(teacher_child_board, child_side, families)
    engine_counts = pattern_counts(engine_child_board, child_side, families)
    delta: dict[FeatureKey, int] = {}
    for key in set(teacher_counts) | set(engine_counts):
        value = engine_counts[key] - teacher_counts[key]
        if value:
            delta[key] = value
    return delta


def root_candidate_moves(root_scores: dict[str, int], legal_moves: set[str]) -> tuple[str, ...]:
    candidates = {move for move in root_scores if move in legal_moves}
    if not candidates:
        candidates = set(legal_moves)
    return tuple(sorted(candidates))


def root_score_ranks(root_scores: dict[str, int]) -> dict[str, int]:
    ordered_scores = sorted(set(root_scores.values()), reverse=True)
    score_rank = {score: index + 1 for index, score in enumerate(ordered_scores)}
    return {move: score_rank[score] for move, score in root_scores.items()}


def score_margin_for_pair(
    root_scores: dict[str, int],
    preferred_move: str,
    other_move: str,
) -> int | None:
    preferred_score = root_scores.get(preferred_move)
    other_score = root_scores.get(other_move)
    if preferred_score is None or other_score is None:
        return None
    return preferred_score - other_score


def rank_margin_for_pair(
    ranks: dict[str, int],
    preferred_move: str,
    other_move: str,
) -> int | None:
    preferred_rank = ranks.get(preferred_move)
    other_rank = ranks.get(other_move)
    if preferred_rank is None or other_rank is None:
        return None
    return other_rank - preferred_rank


def base_pair_weight(config: TrainerConfig, pair_kind: str) -> float:
    if config.pair_weighting == "exact-boost":
        return config.exact_best_weight if pair_kind == "exact" else config.teacher_weight
    return 1.0


def pair_objective_weight(
    config: TrainerConfig,
    *,
    pair_kind: str,
    rank_margin: int | None,
    score_margin: int | None,
) -> float:
    base = base_pair_weight(config, pair_kind)
    if config.pair_weighting == "rank-margin" and rank_margin is not None:
        return base * (1.0 + min(max(rank_margin, 0), 8) / 8.0)
    if config.pair_weighting == "score-margin" and score_margin is not None:
        return base * (1.0 + min(abs(score_margin), 256) / 128.0)
    return base


def pair_priority(pair: PreferencePair) -> tuple[int, float, str, str]:
    kind_priority = 0 if pair.pair_kind == "exact" else 1
    margin = pair.score_margin if pair.score_margin is not None else pair.rank_margin or 0
    return (kind_priority, -abs(float(margin)), pair.preferred_move, pair.other_move)


def limit_pairs_for_position(
    pairs: list[PreferencePair],
    max_pairs_per_position: int,
) -> tuple[list[PreferencePair], int]:
    if max_pairs_per_position <= 0 or len(pairs) <= max_pairs_per_position:
        return pairs, 0
    ordered = sorted(pairs, key=pair_priority)
    return ordered[:max_pairs_per_position], len(pairs) - max_pairs_per_position


def analyze_command(config: TrainerConfig) -> list[str]:
    return shared_analyze_command(
        AnalyzerConfig(
            analyze_position=config.analyze_position,
            eval_config=config.eval_config,
            depth=DEFAULT_ANALYSIS_DEPTH,
        )
    )


def run_analysis(config: TrainerConfig, board_text: str) -> AnalyzeResult:
    return shared_run_analysis(
        AnalyzerConfig(
            analyze_position=config.analyze_position,
            eval_config=config.eval_config,
            depth=DEFAULT_ANALYSIS_DEPTH,
        ),
        board_text,
    )


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
    preferred_move: str | None = None,
    other_move: str | None = None,
    pair_kind: str = "",
    pair_weight: float | None = None,
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
        preferred_move=preferred_move or teacher_move,
        other_move=other_move or engine_move,
        pair_kind=pair_kind,
        pair_weight=pair_weight,
        exact_best_moves=exact_best,
        teacher_exact_best=_exact_bool(teacher_move, exact_best),
        engine_exact_best=_exact_bool(engine_move, exact_best),
        model_margin=model_margin,
        loss=loss,
    )


def should_keep_score_margin(config: TrainerConfig, score_margin: int | None) -> bool:
    if config.min_score_margin <= 0 or score_margin is None:
        return True
    return abs(score_margin) >= config.min_score_margin


def make_preference_pair(
    *,
    config: TrainerConfig,
    source: LabelSource,
    split: str,
    board_text: str,
    teacher_move: str,
    engine_move: str,
    preferred_move: str,
    other_move: str,
    pair_kind: str,
    exact_best: tuple[str, ...],
    root_scores: dict[str, int],
    ranks: dict[str, int],
) -> PreferencePair | None:
    if preferred_move == other_move:
        return None
    score_margin = score_margin_for_pair(root_scores, preferred_move, other_move)
    if not should_keep_score_margin(config, score_margin):
        return None
    rank_margin = rank_margin_for_pair(ranks, preferred_move, other_move)
    try:
        preferred_child = apply_move_to_board(board_text, preferred_move)
        other_child = apply_move_to_board(board_text, other_move)
    except ScriptError:
        return None
    phase = phase_for_board(preferred_child, config.phase_cutoffs)
    features = preference_features(
        root_board_text=board_text,
        teacher_child_board=preferred_child,
        engine_child_board=other_child,
        families=config.families,
    )
    if not features:
        return None
    weight = pair_objective_weight(
        config,
        pair_kind=pair_kind,
        rank_margin=rank_margin,
        score_margin=score_margin,
    )
    return PreferencePair(
        position_id=position_id_for_source(source),
        source_path=source.path,
        source_line=source.line_number,
        split=split,
        phase=phase,
        board_text=board_text,
        teacher_move=teacher_move,
        engine_move=engine_move,
        preferred_move=preferred_move,
        other_move=other_move,
        pair_kind=pair_kind,
        pair_weight=weight,
        features=features,
        exact_best_moves=exact_best,
        preferred_score=root_scores.get(preferred_move),
        other_score=root_scores.get(other_move),
        rank_margin=rank_margin,
        score_margin=score_margin,
    )


def generate_position_pairs(
    *,
    config: TrainerConfig,
    source: LabelSource,
    split: str,
    board_text: str,
    teacher_move: str,
    engine_move: str,
    legal_moves: set[str],
    exact_best: tuple[str, ...],
    root_scores: dict[str, int],
) -> tuple[list[PreferencePair], collections.Counter[str]]:
    stats: collections.Counter[str] = collections.Counter()
    candidates = root_candidate_moves(root_scores, legal_moves)
    ranks = root_score_ranks(root_scores)
    generated: list[PreferencePair] = []

    def add_pair(preferred_move: str, other_move: str, pair_kind: str) -> None:
        pair = make_preference_pair(
            config=config,
            source=source,
            split=split,
            board_text=board_text,
            teacher_move=teacher_move,
            engine_move=engine_move,
            preferred_move=preferred_move,
            other_move=other_move,
            pair_kind=pair_kind,
            exact_best=exact_best,
            root_scores=root_scores,
            ranks=ranks,
        )
        if pair is None:
            stats["candidate_pair_skipped"] += 1
            return
        generated.append(pair)

    if config.pair_mode == "best-vs-engine":
        add_pair(teacher_move, engine_move, "teacher")
    elif config.pair_mode == "best-vs-all":
        for candidate in candidates:
            if candidate != teacher_move:
                add_pair(teacher_move, candidate, "teacher")
    elif config.pair_mode == "rank-weighted":
        teacher_rank = ranks.get(teacher_move)
        if teacher_rank is None:
            stats["rank_scores_missing_skipped"] += 1
        else:
            for candidate in candidates:
                if candidate == teacher_move:
                    continue
                candidate_rank = ranks.get(candidate)
                if candidate_rank is not None and candidate_rank > teacher_rank:
                    add_pair(teacher_move, candidate, "teacher")
    elif config.pair_mode == "teacher-vs-ranked-above":
        teacher_score = root_scores.get(teacher_move)
        if teacher_score is None:
            stats["rank_scores_missing_skipped"] += 1
        else:
            for candidate in candidates:
                if candidate == teacher_move:
                    continue
                candidate_score = root_scores.get(candidate)
                if candidate_score is not None and candidate_score > teacher_score:
                    add_pair(teacher_move, candidate, "teacher")
    elif config.pair_mode == "exact-aware":
        exact_preferred = tuple(move for move in exact_best if move in legal_moves)
        if exact_preferred:
            exact_set = set(exact_preferred)
            for preferred in exact_preferred:
                for candidate in candidates:
                    if candidate not in exact_set:
                        add_pair(preferred, candidate, "exact")
        else:
            stats["exact_unavailable_fallback_positions"] += 1
            for candidate in candidates:
                if candidate != teacher_move:
                    add_pair(teacher_move, candidate, "teacher")

    limited, truncated = limit_pairs_for_position(generated, config.max_pairs_per_position)
    if truncated:
        stats["max_pairs_truncated"] += 1
        stats["pairs_truncated"] += truncated
    return limited, stats


def collect_pairs(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> tuple[list[PreferencePair], list[ValidationRecord], dict[str, Any]]:
    sources = read_jsonl_sources(config.teacher_labels)
    exact_by_board = load_exact_by_board(config.exact_labels) if config.exact_labels else {}
    pairs: list[PreferencePair] = []
    validation_records: list[ValidationRecord] = []
    stats: collections.Counter[str] = collections.Counter()
    stats["teacher_rows"] = len(sources)
    stats["analysis_cache_hits"] = 0
    stats["analysis_cache_misses"] = 0
    stats["analysis_cache_writes"] = 0
    stats["analysis_jobs"] = config.analysis_jobs
    entries: list[ValidationRecord | PreparedPosition] = []
    analysis_requests: list[AnalysisRequest] = []
    eval_config_hash = sha256_file(config.eval_config)

    for source_index, source in enumerate(sources):
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
            entries.append(
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
        if exact_best and teacher_move not in exact_best and config.pair_mode != "exact-aware":
            stats["teacher_exact_disagreements_skipped"] += 1
            entries.append(
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
        if exact_best and teacher_move not in exact_best and config.pair_mode == "exact-aware":
            stats["teacher_exact_disagreements_used_by_exact_aware"] += 1

        engine_move = precomputed_engine_move(record, legal_moves)
        needs_analysis = engine_move is None or config.pair_mode != "best-vs-engine"
        board_hash = sha256_text(board_key(board_text))
        cache_key = analysis_cache_key(
            board_hash=board_hash,
            eval_config_hash=eval_config_hash,
            analysis_depth=DEFAULT_ANALYSIS_DEPTH,
        )
        if needs_analysis:
            analysis_requests.append(
                AnalysisRequest(
                    source_index=source_index,
                    source=source,
                    board_text=board_text,
                    cache_key=cache_key,
                    position_id=position_id_for_source(source),
                )
            )
        entries.append(
            PreparedPosition(
                source_index=source_index,
                source=source,
                split=split,
                phase=phase,
                board_text=board_text,
                teacher_move=teacher_move,
                engine_move=engine_move,
                legal_moves=legal_moves,
                exact_best=exact_best,
                needs_analysis=needs_analysis,
            )
        )

    analysis_by_source = analyze_requests(
        config=config,
        requests=analysis_requests,
        analyzer=analyzer,
        stats=stats,
        eval_config_hash=eval_config_hash,
    )

    for entry in entries:
        if isinstance(entry, ValidationRecord):
            validation_records.append(entry)
            continue
        source = entry.source
        split = entry.split
        phase = entry.phase
        board_text = entry.board_text
        teacher_move = entry.teacher_move
        legal_moves = entry.legal_moves
        exact_best = entry.exact_best
        analysis = analysis_by_source.get(entry.source_index) if entry.needs_analysis else None
        engine_move = analysis.best_move if analysis is not None else entry.engine_move
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
            if config.pair_mode == "best-vs-engine":
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
        root_scores = analysis.root_scores if analysis is not None else {}
        position_pairs, pair_stats = generate_position_pairs(
            config=config,
            source=source,
            split=split,
            board_text=board_text,
            teacher_move=teacher_move,
            engine_move=engine_move,
            legal_moves=legal_moves,
            exact_best=exact_best,
            root_scores=root_scores,
        )
        stats.update(pair_stats)
        if not position_pairs:
            stats["no_pair_generated_skipped"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="no_pair_generated",
                    teacher_move=teacher_move,
                    engine_move=engine_move,
                    exact_best=exact_best,
                )
            )
            continue

        pairs.extend(position_pairs)
        stats["paired_positions"] += 1
        stats["preference_pairs"] += len(position_pairs)
        stats["max_pairs_in_position"] = max(stats["max_pairs_in_position"], len(position_pairs))
        for pair in position_pairs:
            stats[f"{pair.phase}_pairs"] += 1
            stats[f"{pair.pair_kind}_pairs"] += 1
            if pair.pair_kind == "exact":
                stats["exact_aware_pairs"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=pair.phase,
                    status="paired",
                    teacher_move=teacher_move,
                    engine_move=engine_move,
                    preferred_move=pair.preferred_move,
                    other_move=pair.other_move,
                    pair_kind=pair.pair_kind,
                    pair_weight=pair.pair_weight,
                    exact_best=exact_best,
                )
            )

    for key in (
        "accepted_teacher_rows",
        "already_agreed",
        "candidate_pair_skipped",
        "exact_pairs",
        "exact_aware_pairs",
        "exact_unavailable_fallback_positions",
        "max_pairs_in_position",
        "max_pairs_truncated",
        "no_pair_generated_skipped",
        "pairs_truncated",
        "preference_pairs",
        "paired_positions",
        "rank_scores_missing_skipped",
        "split_skipped",
        "teacher_pairs",
        "teacher_exact_disagreements_used_by_exact_aware",
        "teacher_exact_disagreements_skipped",
        "unusable_teacher_rows",
    ):
        stats.setdefault(key, 0)
    for split in ("train", "validation", "holdout"):
        stats.setdefault(f"{split}_rows", 0)
    for phase in PHASES:
        stats.setdefault(f"{phase}_pairs", 0)
    row_stats: dict[str, Any] = {}
    for key in sorted(stats):
        value = stats[key]
        if isinstance(value, float):
            row_stats[key] = value
        else:
            row_stats[key] = int(value)
    row_stats["analysis_cache_mode"] = config.analysis_cache.mode
    row_stats["analysis_cache_dir"] = (
        str(config.analysis_cache.directory) if config.analysis_cache.directory is not None else ""
    )
    row_stats["avg_pairs_per_position"] = (
        row_stats["preference_pairs"] / row_stats["paired_positions"]
        if row_stats["paired_positions"]
        else 0.0
    )
    return pairs, validation_records, row_stats


def stable_sigmoid_negative_margin(margin: float) -> float:
    if margin >= 0:
        exp_neg = math.exp(-margin)
        return exp_neg / (1.0 + exp_neg)
    exp_pos = math.exp(margin)
    return 1.0 / (1.0 + exp_pos)


def pair_margin(weights: WeightsByPhase, pair: PreferencePair) -> float:
    phase_weights = weights.get(pair.phase, {})
    return sum(phase_weights.get(key, 0.0) * value for key, value in pair.features.items())


def base_score_margin(pair: PreferencePair) -> float:
    if pair.preferred_score is None or pair.other_score is None:
        return 0.0
    return float(pair.preferred_score - pair.other_score)


def model_margin(config: TrainerConfig, weights: WeightsByPhase, pair: PreferencePair) -> float:
    margin = pair_margin(weights, pair)
    if config.include_base_margin:
        margin += base_score_margin(pair)
    return margin


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


class LazyPhaseWeights:
    def __init__(self) -> None:
        self.scale = 1.0
        self.stored: dict[FeatureKey, float] = {}

    def effective_value(self, key: FeatureKey) -> float:
        return self.stored.get(key, 0.0) * self.scale

    def shrink(self, factor: float) -> None:
        if not self.stored or factor == 1.0:
            return
        if factor <= 0.0:
            self.stored.clear()
            self.scale = 1.0
            return
        self.scale *= factor
        if self.scale < 1.0e-8:
            self.materialize()

    def add_effective_update(self, key: FeatureKey, delta: float, maximum: float) -> None:
        updated = clamp_weight(self.effective_value(key) + delta, maximum)
        if abs(updated) < 1.0e-12:
            self.stored.pop(key, None)
            return
        self.stored[key] = updated / self.scale

    def materialize(self) -> dict[FeatureKey, float]:
        if self.scale != 1.0:
            self.stored = {
                key: value * self.scale
                for key, value in self.stored.items()
                if abs(value * self.scale) >= 1.0e-12
            }
            self.scale = 1.0
        else:
            self.stored = {
                key: value
                for key, value in self.stored.items()
                if abs(value) >= 1.0e-12
            }
        return self.stored


def lazy_pair_margin(lazy_weights: dict[str, LazyPhaseWeights], pair: PreferencePair) -> float:
    phase_weights = lazy_weights[pair.phase]
    return sum(phase_weights.effective_value(key) * value for key, value in pair.features.items())


def train_weights(config: TrainerConfig, pairs: list[PreferencePair]) -> tuple[WeightsByPhase, list[dict[str, Any]]]:
    weights: WeightsByPhase = {phase: {} for phase in PHASES}
    history: list[dict[str, Any]] = []
    if not pairs:
        for epoch in range(1, config.epochs + 1):
            history.append({"epoch": epoch, **evaluate_pairs(config, weights, pairs)})
        return weights, history

    rng = random.Random(config.seed)
    lazy_weights = {phase: LazyPhaseWeights() for phase in PHASES}
    shrink_factor = max(0.0, 1.0 - config.learning_rate * config.l2)
    for epoch in range(1, config.epochs + 1):
        shuffled = list(pairs)
        rng.shuffle(shuffled)
        for pair in shuffled:
            phase_weights = lazy_weights[pair.phase]
            if config.l2 != 0.0:
                phase_weights.shrink(shrink_factor)
            margin = lazy_pair_margin(lazy_weights, pair)
            if config.include_base_margin:
                margin += base_score_margin(pair)
            if config.loss == "hinge":
                factor = 1.0 if margin < 1.0 else 0.0
            else:
                factor = stable_sigmoid_negative_margin(margin)
            if factor == 0.0:
                continue
            for key, value in pair.features.items():
                delta = config.learning_rate * pair.pair_weight * factor * value
                phase_weights.add_effective_update(key, delta, config.max_abs_weight)
        weights = {phase: dict(lazy_weights[phase].materialize()) for phase in PHASES}
        metrics = evaluate_pairs(config, weights, pairs)
        history.append({"epoch": epoch, **metrics})
    return weights, history


def evaluate_pairs(
    config: TrainerConfig,
    weights: WeightsByPhase,
    pairs: list[PreferencePair],
) -> dict[str, Any]:
    if not pairs:
        return {
            "pairs": 0,
            "loss": 0.0,
            "weighted_loss": 0.0,
            "unweighted_loss": 0.0,
            "accuracy": 0.0,
            "weighted_accuracy": 0.0,
            "avg_margin": 0.0,
            "total_pair_weight": 0.0,
            "avg_pair_weight": 0.0,
        }
    losses = []
    weighted_losses = []
    correct = 0
    weighted_correct = 0.0
    margins = []
    total_weight = 0.0
    for pair in pairs:
        margin = model_margin(config, weights, pair)
        margins.append(margin)
        loss = pair_loss(config.loss, margin)
        losses.append(loss)
        weighted_losses.append(loss * pair.pair_weight)
        total_weight += pair.pair_weight
        if margin > 0.0:
            correct += 1
            weighted_correct += pair.pair_weight
    l2_term = 0.0
    for phase_weights in weights.values():
        l2_term += sum(value * value for value in phase_weights.values())
    weighted_loss = sum(weighted_losses) / total_weight if total_weight else 0.0
    return {
        "pairs": len(pairs),
        "loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "weighted_loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "unweighted_loss": sum(losses) / len(losses) + 0.5 * config.l2 * l2_term,
        "accuracy": correct / len(pairs),
        "weighted_accuracy": weighted_correct / total_weight if total_weight else 0.0,
        "avg_margin": sum(margins) / len(margins),
        "total_pair_weight": total_weight,
        "avg_pair_weight": total_weight / len(pairs),
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
        f"# pair_mode: {config.pair_mode}",
        f"# pair_weighting: {config.pair_weighting}",
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
    base_entries = eval_config_entries(base_text)
    missing_phase_weights = [
        phase for phase in PHASES if f"{phase}.pattern_table" not in base_entries
    ]
    lines = [
        "# generated_by: tools/scripts/regularized_pairwise_pattern_train.py",
        "# no_strength_claim: true",
        "# not_default_promotion: true",
        "# trainer_foundation: true",
        f"# model_margin: {'base_plus_delta' if config.include_base_margin else 'delta_only'}",
        f"# candidate_pattern_table_weight: {config.candidate_pattern_table_weight}",
    ]
    inserted_tables = False

    def append_table_bindings() -> None:
        lines.extend(
            (
                "pattern_table.opening=tables/opening.tsv",
                "pattern_table.midgame=tables/midgame.tsv",
                "pattern_table.late=tables/late.tsv",
            )
        )
        for phase in missing_phase_weights:
            lines.append(f"{phase}.pattern_table={config.candidate_pattern_table_weight}")

    for raw_line in base_text.splitlines():
        parsed = _line_key_value(raw_line)
        if parsed is not None:
            key, _ = parsed
            if key == "pattern_table" or key.startswith("pattern_table."):
                continue
            if key == "name":
                lines.append("name=regularized_pairwise_pattern_candidate")
                append_table_bindings()
                inserted_tables = True
                continue
        lines.append(raw_line)
    if not inserted_tables:
        lines.append("name=regularized_pairwise_pattern_candidate")
        append_table_bindings()
    return "\n".join(lines).rstrip() + "\n"


def validation_rows_with_margins(
    config: TrainerConfig,
    weights: WeightsByPhase,
    pairs: list[PreferencePair],
    records: list[ValidationRecord],
) -> list[ValidationRecord]:
    pair_by_source = {
        (pair.source_path, pair.source_line, pair.preferred_move, pair.other_move): pair
        for pair in pairs
    }
    updated: list[ValidationRecord] = []
    for record in records:
        pair = pair_by_source.get(
            (record.source_path, record.source_line, record.preferred_move, record.other_move)
        )
        if pair is None:
            updated.append(record)
            continue
        margin = model_margin(config, weights, pair)
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
                preferred_move=record.preferred_move,
                other_move=record.other_move,
                pair_kind=record.pair_kind,
                pair_weight=record.pair_weight,
                exact_best_moves=record.exact_best_moves,
                teacher_exact_best=record.teacher_exact_best,
                engine_exact_best=record.engine_exact_best,
                model_margin=margin,
                loss=pair_loss(config.loss, margin) * pair.pair_weight,
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
        "preferred_move",
        "other_move",
        "pair_kind",
        "pair_weight",
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
                    "preferred_move": record.preferred_move,
                    "other_move": record.other_move,
                    "pair_kind": record.pair_kind,
                    "pair_weight": "" if record.pair_weight is None else format_weight(record.pair_weight),
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


def summarize_weights(config: TrainerConfig, weights: WeightsByPhase) -> dict[str, Any]:
    phases: dict[str, Any] = {}
    clamp = min(config.max_abs_output_weight, INT16_LIMIT)
    for phase in PHASES:
        entries = phase_entries(weights, phase)
        by_family = collections.Counter(family for family, _, _ in entries)
        quantized = [quantize_output_weight(weight, config) for _, _, weight in entries]
        saturated = sum(1 for weight in quantized if abs(weight) >= clamp)
        quantized_zero = sum(1 for weight in quantized if weight == 0)
        phases[phase] = {
            "entries": len(entries),
            "entries_by_family": {family: int(by_family[family]) for family in config.families},
            "max_abs_weight": max((abs(weight) for _, _, weight in entries), default=0.0),
            "max_abs_output_weight": max((abs(weight) for weight in quantized), default=0),
            "quantized_nonzero_entries": sum(1 for weight in quantized if weight != 0),
            "quantized_zero_entries": quantized_zero,
            "saturated_entries": saturated,
            "saturation_rate": saturated / len(quantized) if quantized else 0.0,
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
    initial_metrics = summary["training"]["initial_metrics"]
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
        f"- analysis_cache_mode: `{summary['analysis_cache_mode']}`",
        f"- analysis_cache_dir: `{summary['analysis_cache_dir'] or 'none'}`",
        f"- analysis_jobs: `{summary['analysis_jobs']}`",
        "",
        "## Training",
        "",
        f"- split: `{config.split}`",
        f"- families: `{', '.join(config.families)}`",
        f"- loss: `{config.loss}`",
        f"- pair_mode: `{config.pair_mode}`",
        f"- pair_weighting: `{config.pair_weighting}`",
        f"- max_pairs_per_position: `{config.max_pairs_per_position}`",
        f"- exact_best_weight: `{config.exact_best_weight}`",
        f"- teacher_weight: `{config.teacher_weight}`",
        f"- min_score_margin: `{config.min_score_margin}`",
        f"- l2: `{config.l2}`",
        f"- epochs: `{config.epochs}`",
        f"- learning_rate: `{config.learning_rate}`",
        f"- max_abs_weight: `{config.max_abs_weight}`",
        f"- output_scale: `{config.output_scale}`",
        f"- max_abs_output_weight: `{config.max_abs_output_weight}`",
        f"- candidate_pattern_table_weight: `{config.candidate_pattern_table_weight}`",
        f"- model_margin: `{'base_plus_delta' if config.include_base_margin else 'delta_only'}`",
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
            "## Pair Metrics",
            "",
            f"- pairs: `{final_metrics['pairs']}`",
            f"- initial_weighted_loss: `{initial_metrics['weighted_loss']:.6f}`",
            f"- final_weighted_loss: `{final_metrics['weighted_loss']:.6f}`",
            f"- initial_unweighted_loss: `{initial_metrics['unweighted_loss']:.6f}`",
            f"- final_unweighted_loss: `{final_metrics['unweighted_loss']:.6f}`",
            f"- initial_accuracy: `{initial_metrics['accuracy']:.6f}`",
            f"- final_accuracy: `{final_metrics['accuracy']:.6f}`",
            f"- initial_weighted_accuracy: `{initial_metrics['weighted_accuracy']:.6f}`",
            f"- final_weighted_accuracy: `{final_metrics['weighted_accuracy']:.6f}`",
            f"- initial_avg_margin: `{initial_metrics['avg_margin']:.6f}`",
            f"- final_avg_margin: `{final_metrics['avg_margin']:.6f}`",
            f"- total_pair_weight: `{final_metrics['total_pair_weight']:.6f}`",
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
    initial_weights: WeightsByPhase = {phase: {} for phase in PHASES}
    initial_metrics = evaluate_pairs(config, initial_weights, pairs)
    weights, history = train_weights(config, pairs)
    validation_records = validation_rows_with_margins(config, weights, pairs, validation_records)
    final_metrics = evaluate_pairs(config, weights, pairs)

    table_paths = {phase: table_dir / f"{phase}.tsv" for phase in PHASES}
    weight_summary = summarize_weights(config, weights)
    for phase in PHASES:
        entries = phase_entries(weights, phase)
        phase_stats = {
            "paired_rows": row_stats.get(f"{phase}_pairs", 0),
            "entries": len(entries),
            "quantized_nonzero_entries": weight_summary[phase]["quantized_nonzero_entries"],
            "quantized_zero_entries": weight_summary[phase]["quantized_zero_entries"],
            "saturated_entries": weight_summary[phase]["saturated_entries"],
            "max_abs_output_weight": weight_summary[phase]["max_abs_output_weight"],
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
        "analysis_cache_mode": config.analysis_cache.mode,
        "analysis_cache_dir": (
            str(config.analysis_cache.directory) if config.analysis_cache.directory is not None else ""
        ),
        "analysis_cache_hits": row_stats.get("analysis_cache_hits", 0),
        "analysis_cache_misses": row_stats.get("analysis_cache_misses", 0),
        "analysis_cache_writes": row_stats.get("analysis_cache_writes", 0),
        "analysis_jobs": config.analysis_jobs,
        "analysis_elapsed_seconds": row_stats.get("analysis_elapsed_seconds", 0.0),
        "families": list(config.families),
        "split": config.split,
        "loss": config.loss,
        "pair_mode": config.pair_mode,
        "pair_weighting": config.pair_weighting,
        "max_pairs_per_position": config.max_pairs_per_position,
        "exact_best_weight": config.exact_best_weight,
        "teacher_weight": config.teacher_weight,
        "min_score_margin": config.min_score_margin,
        "l2": config.l2,
        "epochs": config.epochs,
        "learning_rate": config.learning_rate,
        "max_abs_weight": config.max_abs_weight,
        "output_scale": config.output_scale,
        "max_abs_output_weight": config.max_abs_output_weight,
        "candidate_pattern_table_weight": config.candidate_pattern_table_weight,
        "include_base_margin": config.include_base_margin,
        "seed": config.seed,
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
        "rows": row_stats,
        "training": {
            "initial_metrics": initial_metrics,
            "history": history,
            "final_metrics": final_metrics,
            "weights": weight_summary,
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
