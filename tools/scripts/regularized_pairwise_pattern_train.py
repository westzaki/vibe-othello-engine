#!/usr/bin/env python3
"""Train phase-specific pattern tables from teacher-vs-engine preferences.

This is a tooling-only trainer. It reads reusable teacher/exact JSONL artifacts,
derives pairwise preferences between the teacher move and the current engine
choice, and writes local candidate artifacts under runs/.
"""

from __future__ import annotations

import argparse
import array
import collections
import csv
import datetime as dt
import hashlib
import json
import math
import pickle
import random
import sys
import time
from dataclasses import dataclass, replace
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
from pattern_symmetry_diagnostics import (
    SYMMETRIZE_MODES,
    SymmetrizeSummary,
    TableViolationSummary,
    parse_symmetrize_modes,
    symmetrize_pattern_table,
)
from pattern_training.analysis_cache import (
    AnalysisCacheConfig,
    AnalysisRequest,
    AnalysisRunnerConfig,
    analysis_cache_key,
    analyze_position_hash,
    collect_git_sha,
    sha256_file,
    sha256_text,
    write_analysis_cache_row as shared_write_analysis_cache_row,
)
from pattern_training.analysis_cache import analyze_requests as shared_analyze_requests
from pattern_training.analyzer import AnalyzerConfig
from pattern_training.analyzer import analyze_command as shared_analyze_command
from pattern_training.analyzer import run_batch_analysis as shared_run_batch_analysis
from pattern_training.analyzer import run_parallel_batch_analysis as shared_run_parallel_batch_analysis
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
OBJECTIVES = ("pairwise-logistic", "listwise-softmax", "exact-aware-listwise")
GUARD_MODES = ("off", "base-agreement")
ANALYSIS_RUNNERS = ("subprocess", "batch")
LISTWISE_FEATURE_CACHE_MODES = ("off", "read-write", "read-only", "refresh")
SOFT_SIGN_ANCHOR_MODES = ("off", "selected", "expected")
FAMILY_ALIASES: dict[str, tuple[str, ...]] = {
    **COMMON_FAMILY_ALIASES,
    "all": FAMILY_ORDER,
    "corner_only": ("corner_2x3", "corner_3x3"),
    "edge_only": ("edge_8", "edge_x_10"),
}

FeatureKey = tuple[str, int]
WeightsByPhase = dict[str, dict[FeatureKey, float]]
PHASE_TO_ID = {phase: index for index, phase in enumerate(PHASES)}
ID_TO_PHASE = {index: phase for phase, index in PHASE_TO_ID.items()}
PAIR_CACHE_SCHEMA = "regularized_pairwise_pair_cache.v1"
CHECKPOINT_SCHEMA = "regularized_pairwise_checkpoint.v1"
TRAINER_MODEL = "pattern_only_delta_only"


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
    bucket: str
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
    pair_cache_dir: Path | None
    pair_cache_mode: str
    checkpoint_dir: Path | None
    resume_checkpoint: bool
    analysis_cache: AnalysisCacheConfig
    analysis_runner: str
    analysis_jobs: int
    listwise_feature_cache_dir: Path | None
    listwise_feature_cache_mode: str
    families: tuple[str, ...]
    split: str
    objective: str
    loss: str
    pair_mode: str
    pair_weighting: str
    bucket_weights_path: Path | None
    bucket_weights: dict[str, float]
    default_bucket_weight: float
    bucket_field: str
    guard_mode: str
    guard_weight: float
    guard_max_pairs_per_position: int
    max_pairs_per_position: int
    exact_best_weight: float
    teacher_weight: float
    min_score_margin: int
    diagnose_dataset: bool
    drop_teacher_exact_disagreement: bool
    exact_aware_only_when_available: bool
    exact_score_soft_target: bool
    exact_score_temperature: float
    exact_score_target_floor: float
    exact_score_near_best_window: int
    soft_sign_anchor_weight: float
    soft_sign_anchor_margin: float
    soft_sign_anchor_mode: str
    post_training_symmetrize_modes: tuple[str, ...]
    max_top_group_size_for_training: int
    tie_penalty: float
    target_top_group_size: int
    top_group_margin: float
    sign_penalty: float
    high_confidence_margin: float
    calibrate_output_scale: bool
    scale_grid: tuple[float, ...]
    l2: float
    epochs: int
    learning_rate: float
    max_abs_weight: float
    output_scale: float
    max_abs_output_weight: int
    candidate_pattern_table_weight: int
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
    bucket: str
    bucket_weight: float
    features: dict[FeatureKey, int]
    exact_best_moves: tuple[str, ...]
    preferred_score: int | None
    other_score: int | None
    rank_margin: int | None
    score_margin: int | None


@dataclass(frozen=True)
class CandidateMove:
    move: str
    phase: str
    features: dict[FeatureKey, int]
    exact_score: int | None


@dataclass(frozen=True)
class ListwiseExample:
    position_id: str
    source_path: Path
    source_line: int
    split: str
    board_text: str
    teacher_move: str
    engine_move: str
    target_moves: tuple[str, ...]
    exact_best_moves: tuple[str, ...]
    exact_root_score: int | None
    candidates: tuple[CandidateMove, ...]
    target_probabilities: tuple[float, ...] | None
    example_weight: float
    bucket: str
    bucket_weight: float


@dataclass(frozen=True)
class CompactListwiseDataset:
    """Flattened listwise examples used by the hot training/evaluation loops."""

    example_offsets: tuple[int, ...]
    candidate_feature_offsets: tuple[int, ...]
    candidate_moves: tuple[str, ...]
    candidate_phase_ids: tuple[int, ...]
    candidate_exact_scores: tuple[int | None, ...]
    candidate_target_mask: tuple[bool, ...]
    candidate_target_probabilities: tuple[float, ...]
    candidate_exact_best_mask: tuple[bool, ...]
    example_teacher_moves: tuple[str, ...]
    example_exact_root_scores: tuple[int | None, ...]
    example_weights: tuple[float, ...]
    example_splits: tuple[str, ...]
    feature_keys: tuple[FeatureKey, ...]
    feature_ids: tuple[int, ...]
    feature_values: tuple[int, ...]

    @property
    def example_count(self) -> int:
        return max(0, len(self.example_offsets) - 1)

    @property
    def candidate_count(self) -> int:
        return len(self.candidate_moves)

    @property
    def feature_entry_count(self) -> int:
        return len(self.feature_ids)


ListwiseData = list[ListwiseExample] | CompactListwiseDataset


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


@dataclass(frozen=True)
class DiagnosticResult:
    summary: dict[str, Any]
    report_path: Path


@dataclass
class FeatureCache:
    families: tuple[str, ...]
    counts_by_child: dict[tuple[str, str], collections.Counter[FeatureKey]]
    hits: int = 0
    misses: int = 0

    def counts(self, board_text: str, side: str) -> collections.Counter[FeatureKey]:
        key = (board_key(board_text), side)
        cached = self.counts_by_child.get(key)
        if cached is not None:
            self.hits += 1
            return cached
        self.misses += 1
        cached = pattern_counts(board_text, side, self.families)
        self.counts_by_child[key] = cached
        return cached

    @property
    def hit_rate(self) -> float:
        total = self.hits + self.misses
        return self.hits / total if total else 0.0


@dataclass
class CompactPreferencePairs:
    feature_keys: list[FeatureKey]
    feature_ids: array.array
    values: array.array
    offsets: array.array
    phase_ids: array.array
    pair_weights: array.array
    count: int
    memory_bytes: int

    @classmethod
    def from_pairs(cls, pairs: list[PreferencePair]) -> "CompactPreferencePairs":
        feature_to_id: dict[FeatureKey, int] = {}
        feature_keys: list[FeatureKey] = []
        feature_ids = array.array("i")
        values = array.array("h")
        offsets = array.array("Q", [0])
        phase_ids = array.array("B")
        pair_weights = array.array("d")
        for pair in pairs:
            phase_ids.append(PHASE_TO_ID[pair.phase])
            pair_weights.append(float(pair.pair_weight))
            for key, value in pair.features.items():
                feature_id = feature_to_id.get(key)
                if feature_id is None:
                    feature_id = len(feature_keys)
                    feature_to_id[key] = feature_id
                    feature_keys.append(key)
                feature_ids.append(feature_id)
                values.append(value)
            offsets.append(len(feature_ids))
        memory_bytes = (
            len(feature_ids) * feature_ids.itemsize
            + len(values) * values.itemsize
            + len(offsets) * offsets.itemsize
            + len(phase_ids) * phase_ids.itemsize
            + len(pair_weights) * pair_weights.itemsize
        )
        return cls(
            feature_keys=feature_keys,
            feature_ids=feature_ids,
            values=values,
            offsets=offsets,
            phase_ids=phase_ids,
            pair_weights=pair_weights,
            count=len(pairs),
            memory_bytes=memory_bytes,
        )


@dataclass
class PairCachePayload:
    pairs: list[PreferencePair]
    listwise_examples: list[ListwiseExample]
    validation_records: list[ValidationRecord]
    row_stats: dict[str, Any]
    compact_pairs: CompactPreferencePairs


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
    parser.add_argument(
        "--analysis-runner",
        choices=ANALYSIS_RUNNERS,
        default="subprocess",
        help="root-analysis runner implementation (default: subprocess)",
    )
    parser.add_argument(
        "--listwise-feature-cache-dir",
        help="optional directory for cached listwise child-board feature artifacts",
    )
    parser.add_argument(
        "--listwise-feature-cache-mode",
        choices=LISTWISE_FEATURE_CACHE_MODES,
        default="off",
        help=(
            "listwise feature cache mode. The cache key includes dataset/eval hashes, "
            "families, phase cutoffs, objective, pair/listwise policy, and seed"
        ),
    )
    parser.add_argument("--out-dir", required=True, help="output directory under runs/")
    parser.add_argument(
        "--pair-cache-dir",
        help=(
            "directory for generated compact pair cache pickle artifacts "
            "(default: OUT_DIR/pair_cache; trusted local runs/ artifacts only)"
        ),
    )
    parser.add_argument(
        "--pair-cache-mode",
        choices=("off", "read-write", "read-only", "refresh"),
        default="read-write",
        help="generated pair cache mode (default: read-write)",
    )
    parser.add_argument(
        "--checkpoint-dir",
        help=(
            "directory for epoch checkpoint pickle artifacts "
            "(default: OUT_DIR/checkpoints; trusted local runs/ artifacts only)"
        ),
    )
    parser.add_argument(
        "--resume-checkpoint",
        action="store_true",
        help="resume pairwise SGD from the latest matching epoch checkpoint",
    )
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
    parser.add_argument(
        "--objective",
        choices=OBJECTIVES,
        default="pairwise-logistic",
        help=(
            "training objective. pairwise-logistic preserves the existing pairwise "
            "trainer; listwise-softmax optimizes root candidate softmax; "
            "exact-aware-listwise uses exact-best target sets when exact labels are "
            "available, falls back to the teacher move otherwise, and adds exact sign "
            "penalties when exact labels include move scores. "
            "--exact-aware-only-when-available only affects pairwise exact-aware "
            "pair generation, not listwise teacher fallback"
        ),
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
        "--bucket-weights",
        help=(
            "optional JSON object mapping label-row bucket names to pair-weight multipliers"
        ),
    )
    parser.add_argument(
        "--default-bucket-weight",
        type=parse_non_negative_float,
        default=1.0,
        help="pair-weight multiplier for rows whose bucket is missing or unmapped",
    )
    parser.add_argument(
        "--bucket-field",
        default="source_bucket",
        help="label-row field used to look up bucket weights (default: source_bucket)",
    )
    parser.add_argument(
        "--guard-mode",
        choices=GUARD_MODES,
        default="off",
        help="optional anti-regression guard pair generation mode (default: off)",
    )
    parser.add_argument(
        "--guard-weight",
        type=parse_non_negative_float,
        default=0.25,
        help="base pair weight for generated guard pairs before bucket weighting",
    )
    parser.add_argument(
        "--guard-max-pairs-per-position",
        type=parse_non_negative_int,
        default=1,
        help="cap generated guard pairs per guarded position; 0 means no cap",
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
    parser.add_argument(
        "--diagnose-dataset",
        action="store_true",
        help="write teacher/exact/pair-generation diagnostics without training or candidate output",
    )
    parser.add_argument(
        "--drop-teacher-exact-disagreement",
        action="store_true",
        help="drop rows where an available exact label does not include the teacher move",
    )
    parser.add_argument(
        "--exact-aware-only-when-available",
        action="store_true",
        help=(
            "with --pair-mode exact-aware, skip rows without exact labels instead "
            "of falling back to teacher pairs; listwise objectives still fall back "
            "to teacher targets when exact-best targets are unavailable"
        ),
    )
    parser.add_argument(
        "--exact-score-soft-target",
        action="store_true",
        help=(
            "for exact-aware-listwise rows with complete move_scores, train against "
            "a soft exact-score distribution over all legal root moves; rows without "
            "complete move_scores fall back to the teacher move"
        ),
    )
    parser.add_argument(
        "--exact-score-temperature",
        type=parse_positive_float,
        default=4.0,
        help="temperature in discs for --exact-score-soft-target (default: 4.0)",
    )
    parser.add_argument(
        "--exact-score-target-floor",
        type=parse_non_negative_float,
        default=0.0001,
        help=(
            "minimum target probability per legal move for --exact-score-soft-target "
            "(default: 0.0001)"
        ),
    )
    parser.add_argument(
        "--exact-score-near-best-window",
        type=parse_non_negative_int,
        default=8,
        help=(
            "exact-score window in discs that receives temperature-shaped probability "
            "before floor smoothing (default: 8)"
        ),
    )
    parser.add_argument(
        "--soft-sign-anchor-weight",
        type=parse_non_negative_float,
        default=0.0,
        help=(
            "small auxiliary exact-root sign anchor weight for soft exact-score "
            "target training (default: 0.0)"
        ),
    )
    parser.add_argument(
        "--soft-sign-anchor-margin",
        type=parse_non_negative_float,
        default=1.0,
        help=(
            "target signed model score margin for --soft-sign-anchor-mode "
            "selected/expected (default: 1.0)"
        ),
    )
    parser.add_argument(
        "--soft-sign-anchor-mode",
        choices=SOFT_SIGN_ANCHOR_MODES,
        default="off",
        help=(
            "soft exact-score sign anchor mode: off disables it; selected anchors "
            "the current selected/top candidate score; expected anchors the "
            "softmax-probability-weighted expected score"
        ),
    )
    parser.add_argument(
        "--post-training-symmetrize",
        default="",
        help=(
            "comma-separated same-family TSV symmetrization modes applied after "
            "training writes generated phase tables. Supported modes: "
            f"{','.join(SYMMETRIZE_MODES)}. Default: off"
        ),
    )
    parser.add_argument(
        "--max-top-group-size-for-training",
        type=parse_non_negative_int,
        default=0,
        help="drop rows whose exact-best top group is larger than this value; 0 disables the cap",
    )
    parser.add_argument(
        "--tie-penalty",
        type=parse_non_negative_float,
        default=0.0,
        help="extra listwise penalty weight for non-target moves in an oversized top group",
    )
    parser.add_argument(
        "--target-top-group-size",
        type=parse_positive_int,
        default=1,
        help="desired maximum top group size for tie-aware listwise training",
    )
    parser.add_argument(
        "--top-group-margin",
        type=parse_non_negative_float,
        default=1.0,
        help="score band treated as the top group for tie-aware training and diagnostics",
    )
    parser.add_argument(
        "--sign-penalty",
        type=parse_non_negative_float,
        default=1.0,
        help="exact-score ordering penalty for exact-aware-listwise rows with move_scores",
    )
    parser.add_argument(
        "--high-confidence-margin",
        type=parse_non_negative_float,
        default=8.0,
        help="model score magnitude used for high-confidence wrong-direction diagnostics",
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
        "--calibrate-output-scale",
        action="store_true",
        help="choose output_scale from --scale-grid using exact-overlap validation diagnostics",
    )
    parser.add_argument(
        "--scale-grid",
        default="1",
        help="comma-separated positive output scales considered by --calibrate-output-scale",
    )
    parser.add_argument(
        "--candidate-pattern-table-weight",
        type=parse_positive_int,
        default=1,
        help=(
            "phase pattern_table weight inserted into the generated pattern-only candidate eval"
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


def load_bucket_weights(path: Path) -> dict[str, float]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise ScriptError(f"failed to read bucket weights {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ScriptError(f"failed to parse bucket weights {path}: {exc}") from exc
    if not isinstance(payload, dict):
        raise ScriptError("--bucket-weights must contain a JSON object")
    weights: dict[str, float] = {}
    for raw_key, raw_value in payload.items():
        if not isinstance(raw_key, str) or not raw_key.strip():
            raise ScriptError("--bucket-weights keys must be non-empty strings")
        if not isinstance(raw_value, int | float) or isinstance(raw_value, bool):
            raise ScriptError(f"bucket weight for {raw_key!r} must be numeric")
        value = float(raw_value)
        if not math.isfinite(value) or value < 0:
            raise ScriptError(f"bucket weight for {raw_key!r} must be a finite non-negative number")
        weights[raw_key.strip()] = value
    return weights


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


def parse_scale_grid(text: str) -> tuple[float, ...]:
    values: list[float] = []
    for part in parse_csv_values(text, error_label="scale grid"):
        try:
            value = float(part)
        except ValueError as exc:
            raise ScriptError(f"invalid scale grid value: {part}") from exc
        if not math.isfinite(value) or value <= 0.0:
            raise ScriptError("--scale-grid values must be finite positive numbers")
        if value not in values:
            values.append(value)
    if not values:
        raise ScriptError("--scale-grid selected no scales")
    return tuple(values)


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


def reject_source_data_dir(path: Path, *, option_name: str) -> None:
    resolved = path.resolve(strict=False)
    source_data = (REPO_ROOT / "data").resolve(strict=False)
    try:
        resolved.relative_to(source_data)
    except ValueError:
        return
    raise ScriptError(f"{option_name} must not be under source-controlled data/")


def config_from_args(
    args: argparse.Namespace,
    invocation: list[str] | None = None,
) -> TrainerConfig:
    if args.max_abs_output_weight > INT16_LIMIT:
        raise ScriptError(f"--max-abs-output-weight must be <= {INT16_LIMIT}")
    if args.exact_score_soft_target and args.objective != "exact-aware-listwise":
        raise ScriptError("--exact-score-soft-target requires --objective exact-aware-listwise")
    if args.exact_score_target_floor >= 1.0:
        raise ScriptError("--exact-score-target-floor must be less than 1")
    if args.soft_sign_anchor_mode != "off":
        if args.objective != "exact-aware-listwise" or not args.exact_score_soft_target:
            raise ScriptError(
                "--soft-sign-anchor-mode selected/expected requires "
                "--objective exact-aware-listwise --exact-score-soft-target"
            )
        if args.soft_sign_anchor_weight == 0.0:
            raise ScriptError("--soft-sign-anchor-weight must be positive when sign anchor is enabled")
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
    out_dir = resolve_out_dir(args.out_dir)
    pair_cache_dir = Path(args.pair_cache_dir) if args.pair_cache_dir else out_dir / "pair_cache"
    if args.pair_cache_mode == "off":
        pair_cache_dir = None
    else:
        reject_source_data_dir(pair_cache_dir, option_name="--pair-cache-dir")
    checkpoint_dir = Path(args.checkpoint_dir) if args.checkpoint_dir else out_dir / "checkpoints"
    reject_source_data_dir(checkpoint_dir, option_name="--checkpoint-dir")
    listwise_feature_cache_dir = (
        Path(args.listwise_feature_cache_dir) if args.listwise_feature_cache_dir else None
    )
    if args.listwise_feature_cache_mode != "off" and listwise_feature_cache_dir is None:
        raise ScriptError("--listwise-feature-cache-dir is required unless --listwise-feature-cache-mode=off")
    if args.listwise_feature_cache_mode == "off" and listwise_feature_cache_dir is not None:
        raise ScriptError(
            "--listwise-feature-cache-mode must not be off when --listwise-feature-cache-dir is provided"
        )
    if listwise_feature_cache_dir is not None:
        reject_source_data_dir(listwise_feature_cache_dir, option_name="--listwise-feature-cache-dir")
    bucket_weights_path = Path(args.bucket_weights) if args.bucket_weights else None
    bucket_weights = load_bucket_weights(bucket_weights_path) if bucket_weights_path is not None else {}
    post_training_symmetrize_modes = parse_symmetrize_modes(args.post_training_symmetrize)
    return TrainerConfig(
        teacher_labels=teacher_labels,
        exact_labels=exact_labels,
        eval_config=eval_config,
        analyze_position=Path(args.analyze_position),
        out_dir=out_dir,
        pair_cache_dir=pair_cache_dir,
        pair_cache_mode=args.pair_cache_mode,
        checkpoint_dir=checkpoint_dir,
        resume_checkpoint=args.resume_checkpoint,
        analysis_cache=AnalysisCacheConfig(
            directory=analysis_cache_dir,
            mode=args.analysis_cache_mode,
        ),
        analysis_runner=args.analysis_runner,
        analysis_jobs=args.analysis_jobs,
        listwise_feature_cache_dir=listwise_feature_cache_dir,
        listwise_feature_cache_mode=args.listwise_feature_cache_mode,
        families=parse_families(args.families),
        split=args.split,
        objective=args.objective,
        loss=args.loss,
        pair_mode=args.pair_mode,
        pair_weighting=args.pair_weighting,
        bucket_weights_path=bucket_weights_path,
        bucket_weights=bucket_weights,
        default_bucket_weight=args.default_bucket_weight,
        bucket_field=args.bucket_field,
        guard_mode=args.guard_mode,
        guard_weight=args.guard_weight,
        guard_max_pairs_per_position=args.guard_max_pairs_per_position,
        max_pairs_per_position=args.max_pairs_per_position,
        exact_best_weight=args.exact_best_weight,
        teacher_weight=args.teacher_weight,
        min_score_margin=args.min_score_margin,
        diagnose_dataset=args.diagnose_dataset,
        drop_teacher_exact_disagreement=args.drop_teacher_exact_disagreement,
        exact_aware_only_when_available=args.exact_aware_only_when_available,
        exact_score_soft_target=args.exact_score_soft_target,
        exact_score_temperature=args.exact_score_temperature,
        exact_score_target_floor=args.exact_score_target_floor,
        exact_score_near_best_window=args.exact_score_near_best_window,
        soft_sign_anchor_weight=args.soft_sign_anchor_weight,
        soft_sign_anchor_margin=args.soft_sign_anchor_margin,
        soft_sign_anchor_mode=args.soft_sign_anchor_mode,
        post_training_symmetrize_modes=post_training_symmetrize_modes,
        max_top_group_size_for_training=args.max_top_group_size_for_training,
        tie_penalty=args.tie_penalty,
        target_top_group_size=args.target_top_group_size,
        top_group_margin=args.top_group_margin,
        sign_penalty=args.sign_penalty,
        high_confidence_margin=args.high_confidence_margin,
        calibrate_output_scale=args.calibrate_output_scale,
        scale_grid=parse_scale_grid(args.scale_grid),
        l2=args.l2,
        epochs=args.epochs,
        learning_rate=args.learning_rate,
        max_abs_weight=args.max_abs_weight,
        output_scale=args.output_scale,
        max_abs_output_weight=args.max_abs_output_weight,
        candidate_pattern_table_weight=args.candidate_pattern_table_weight,
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
    batch_analyzer = None
    if config.analysis_runner == "batch":
        analyzer_config = AnalyzerConfig(
            analyze_position=config.analyze_position,
            eval_config=config.eval_config,
            depth=DEFAULT_ANALYSIS_DEPTH,
        )

        def run_batch(
            requests: list[AnalysisRequest],
        ) -> Any:
            request_by_key = {request.cache_key: request for request in requests}
            batch_requests = ((request.cache_key, request.board_text) for request in requests)
            if config.analysis_jobs <= 1:
                results = shared_run_batch_analysis(analyzer_config, batch_requests)
            else:
                results = shared_run_parallel_batch_analysis(
                    analyzer_config,
                    batch_requests,
                    jobs=config.analysis_jobs,
                )
            for cache_key, result in results:
                request = request_by_key.get(cache_key)
                if request is None:
                    raise ScriptError(f"batch analyzer returned unexpected key: {cache_key}")
                yield request, result

        batch_analyzer = run_batch

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
        batch_analyzer=batch_analyzer,
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


def exact_root_score(record: dict[str, Any] | None) -> int | None:
    if record is None:
        return None
    value = record.get("exact_score_side_to_move")
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    return None


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


def _path_hash_manifest(paths: tuple[Path, ...]) -> list[dict[str, str]]:
    return [{"path": str(path), "sha256": sha256_file(path)} for path in paths]


def listwise_feature_cache_key(
    *,
    config: TrainerConfig,
    teacher_manifest: list[dict[str, str]],
    exact_manifest: list[dict[str, str]],
    eval_config_hash: str,
) -> str:
    payload = {
        "schema": 1,
        "dataset": {
            "teacher_labels": teacher_manifest,
            "exact_labels": exact_manifest,
        },
        "eval_config_sha256": eval_config_hash,
        "families": list(config.families),
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
        "objective": config.objective,
        "pair_listwise_policy": {
            "split": config.split,
            "pair_mode": config.pair_mode,
            "pair_weighting": config.pair_weighting,
            "exact_best_weight": config.exact_best_weight,
            "teacher_weight": config.teacher_weight,
            "drop_teacher_exact_disagreement": config.drop_teacher_exact_disagreement,
            "exact_aware_only_when_available": config.exact_aware_only_when_available,
            "exact_score_soft_target": config.exact_score_soft_target,
            "exact_score_temperature": config.exact_score_temperature,
            "exact_score_target_floor": config.exact_score_target_floor,
            "exact_score_near_best_window": config.exact_score_near_best_window,
            "soft_sign_anchor_weight": config.soft_sign_anchor_weight,
            "soft_sign_anchor_margin": config.soft_sign_anchor_margin,
            "soft_sign_anchor_mode": config.soft_sign_anchor_mode,
            "max_top_group_size_for_training": config.max_top_group_size_for_training,
            "target_top_group_size": config.target_top_group_size,
            "top_group_margin": config.top_group_margin,
        },
        "seed": config.seed,
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode("utf-8")).hexdigest()


FeatureCacheValue = tuple[int, tuple[tuple[str, int, int], ...]]


class ListwiseFeatureCache:
    def __init__(self, *, path: Path, mode: str) -> None:
        self.path = path
        self.mode = mode
        self.values: dict[str, FeatureCacheValue] = {}
        self.hits = 0
        self.misses = 0
        self.writes = 0
        self.load_seconds = 0.0
        self.save_seconds = 0.0
        if mode not in {"read-write", "read-only"}:
            return
        start = time.perf_counter()
        if path.exists():
            try:
                payload = pickle.loads(path.read_bytes())
            except (OSError, pickle.PickleError, EOFError) as exc:
                if mode == "read-only":
                    raise ScriptError(f"failed to load listwise feature cache {path}: {exc}") from exc
                payload = {}
            if isinstance(payload, dict) and payload.get("schema") == 1:
                raw_values = payload.get("values", {})
                if isinstance(raw_values, dict):
                    self.values = raw_values
        elif mode == "read-only":
            raise ScriptError(f"listwise feature cache does not exist: {path}")
        self.load_seconds = time.perf_counter() - start

    def get(self, key: str) -> FeatureCacheValue | None:
        value = self.values.get(key)
        if value is None:
            self.misses += 1
        else:
            self.hits += 1
        return value

    def put(self, key: str, value: FeatureCacheValue) -> None:
        if self.mode not in {"read-write", "refresh"}:
            return
        if key not in self.values:
            self.writes += 1
        self.values[key] = value

    def save(self) -> None:
        if self.mode not in {"read-write", "refresh"}:
            return
        start = time.perf_counter()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = {"schema": 1, "values": self.values}
        tmp_path = self.path.with_suffix(self.path.suffix + ".tmp")
        tmp_path.write_bytes(pickle.dumps(payload, protocol=pickle.HIGHEST_PROTOCOL))
        tmp_path.replace(self.path)
        self.save_seconds = time.perf_counter() - start


def maybe_make_listwise_feature_cache(
    *,
    config: TrainerConfig,
    teacher_manifest: list[dict[str, str]],
    exact_manifest: list[dict[str, str]],
    eval_config_hash: str,
) -> ListwiseFeatureCache | None:
    if config.listwise_feature_cache_mode == "off" or config.listwise_feature_cache_dir is None:
        return None
    cache_key = listwise_feature_cache_key(
        config=config,
        teacher_manifest=teacher_manifest,
        exact_manifest=exact_manifest,
        eval_config_hash=eval_config_hash,
    )
    cache_path = config.listwise_feature_cache_dir / f"listwise-features-{cache_key}.pkl"
    return ListwiseFeatureCache(path=cache_path, mode=config.listwise_feature_cache_mode)


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
    feature_cache: FeatureCache | None = None,
) -> dict[FeatureKey, int]:
    _, root_side = parse_board(root_board_text)
    child_side = opponent(root_side)
    # Root move scores are the negated child-position search scores. For a
    # preferred root move, increasing its root score therefore means decreasing
    # the child-side evaluation relative to the compared child.
    if feature_cache is None:
        teacher_counts = pattern_counts(teacher_child_board, child_side, families)
        engine_counts = pattern_counts(engine_child_board, child_side, families)
    else:
        teacher_counts = feature_cache.counts(teacher_child_board, child_side)
        engine_counts = feature_cache.counts(engine_child_board, child_side)
    delta: dict[FeatureKey, int] = {}
    for key in set(teacher_counts) | set(engine_counts):
        value = engine_counts[key] - teacher_counts[key]
        if value:
            delta[key] = value
    return delta


def root_move_features(
    *,
    root_board_text: str,
    child_board_text: str,
    families: tuple[str, ...],
    feature_cache: FeatureCache | None = None,
) -> dict[FeatureKey, int]:
    _, root_side = parse_board(root_board_text)
    child_side = opponent(root_side)
    counts = (
        pattern_counts(child_board_text, child_side, families)
        if feature_cache is None
        else feature_cache.counts(child_board_text, child_side)
    )
    return {key: -value for key, value in counts.items() if value}


def exact_score_soft_target_probabilities(
    *,
    moves: tuple[str, ...],
    exact_scores: dict[str, int],
    temperature: float,
    floor: float,
    near_best_window: int,
) -> tuple[float, ...] | None:
    if not moves or any(move not in exact_scores for move in moves):
        return None
    if floor * len(moves) >= 1.0:
        raise ScriptError(
            "--exact-score-target-floor is too large for the legal move count in at least one row"
        )
    best_score = max(exact_scores[move] for move in moves)
    raw: list[float] = []
    for move in moves:
        gap = best_score - exact_scores[move]
        shaped = math.exp(-gap / temperature) if gap <= near_best_window else 0.0
        raw.append(max(floor, shaped))
    total = sum(raw)
    if total <= 0.0 or not math.isfinite(total):
        return None
    return tuple(value / total for value in raw)


def make_listwise_example(
    *,
    config: TrainerConfig,
    source: LabelSource,
    split: str,
    board_text: str,
    teacher_move: str,
    engine_move: str,
    legal_moves: set[str],
    exact_best: tuple[str, ...],
    exact_scores: dict[str, int],
    exact_score: int | None,
    root_scores: dict[str, int],
    feature_cache: ListwiseFeatureCache | None = None,
) -> ListwiseExample | None:
    candidates: list[CandidateMove] = []
    root_key = board_key(board_text)
    for move in sorted(legal_moves):
        cache_key = f"{root_key}\n{move}"
        cached = feature_cache.get(cache_key) if feature_cache is not None else None
        if cached is not None:
            phase_id, raw_features = cached
            phase = ID_TO_PHASE[phase_id]
            features = {
                (family, int(index)): int(value)
                for family, index, value in raw_features
            }
        else:
            try:
                child = apply_move_to_board(board_text, move)
            except ScriptError:
                continue
            features = root_move_features(
                root_board_text=board_text,
                child_board_text=child,
                families=config.families,
            )
            phase = phase_for_board(child, config.phase_cutoffs)
            if feature_cache is not None:
                feature_cache.put(
                    cache_key,
                    (
                        PHASE_TO_ID[phase],
                        tuple(
                            (family, index, value)
                            for (family, index), value in sorted(features.items())
                        ),
                    ),
                )
        candidates.append(
            CandidateMove(
                move=move,
                phase=phase,
                features=features,
                exact_score=exact_scores.get(move),
            )
        )
    if len(candidates) < 2:
        return None
    candidate_moves = tuple(candidate.move for candidate in candidates)
    target_probabilities: tuple[float, ...] | None = None
    if config.exact_score_soft_target and exact_scores:
        target_probabilities = exact_score_soft_target_probabilities(
            moves=candidate_moves,
            exact_scores=exact_scores,
            temperature=config.exact_score_temperature,
            floor=config.exact_score_target_floor,
            near_best_window=config.exact_score_near_best_window,
        )
        if target_probabilities is not None:
            target_moves = tuple(
                move
                for move, probability in zip(candidate_moves, target_probabilities, strict=True)
                if probability > config.exact_score_target_floor
            )
            target_weight = config.exact_best_weight
            if not target_moves:
                best_probability = max(target_probabilities)
                target_moves = tuple(
                    move
                    for move, probability in zip(candidate_moves, target_probabilities, strict=True)
                    if probability == best_probability
                )
            bucket = bucket_for_source(config, source)
            bucket_weight = bucket_weight_for_source(config, source)
            return ListwiseExample(
                position_id=position_id_for_source(source),
                source_path=source.path,
                source_line=source.line_number,
                split=split,
                board_text=board_text,
                teacher_move=teacher_move,
                engine_move=engine_move,
                target_moves=target_moves,
                exact_best_moves=exact_best,
                exact_root_score=exact_score,
                candidates=tuple(candidates),
                target_probabilities=target_probabilities,
                example_weight=target_weight * bucket_weight,
                bucket=bucket,
                bucket_weight=bucket_weight,
            )
    exact_targets = tuple(move for move in exact_best if move in legal_moves)
    if exact_targets and not config.exact_score_soft_target:
        target_moves = exact_targets
        target_weight = config.exact_best_weight
    elif teacher_move in legal_moves:
        target_moves = (teacher_move,)
        target_weight = config.teacher_weight
    else:
        return None
    bucket = bucket_for_source(config, source)
    bucket_weight = bucket_weight_for_source(config, source)
    return ListwiseExample(
        position_id=position_id_for_source(source),
        source_path=source.path,
        source_line=source.line_number,
        split=split,
        board_text=board_text,
        teacher_move=teacher_move,
        engine_move=engine_move,
        target_moves=target_moves,
        exact_best_moves=exact_best,
        exact_root_score=exact_score,
        candidates=tuple(candidates),
        target_probabilities=None,
        example_weight=target_weight * bucket_weight,
        bucket=bucket,
        bucket_weight=bucket_weight,
    )


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
    kind_priority = {"exact": 0, "teacher": 1}.get(pair.pair_kind, 2)
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
    for key in (
        "selected_move",
        "engine_move",
        "engine_selected_move",
        "search_best_move",
        "vibe_move",
        "engine_best_move",
    ):
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


def bucket_for_source(config: TrainerConfig, source: LabelSource) -> str:
    value = source.record.get(config.bucket_field)
    if isinstance(value, str) and value.strip():
        return value.strip()
    return "__missing__"


def bucket_weight_for_source(config: TrainerConfig, source: LabelSource) -> float:
    bucket = bucket_for_source(config, source)
    return config.bucket_weights.get(bucket, config.default_bucket_weight)


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
    objective_weight_override: float | None = None,
    feature_cache: FeatureCache | None = None,
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
        feature_cache=feature_cache,
    )
    if not features:
        return None
    weight = (
        objective_weight_override
        if objective_weight_override is not None
        else pair_objective_weight(
            config,
            pair_kind=pair_kind,
            rank_margin=rank_margin,
            score_margin=score_margin,
        )
    )
    bucket = bucket_for_source(config, source)
    bucket_weight = bucket_weight_for_source(config, source)
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
        pair_weight=weight * bucket_weight,
        bucket=bucket,
        bucket_weight=bucket_weight,
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
    feature_cache: FeatureCache | None = None,
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
            feature_cache=feature_cache,
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
            if config.exact_aware_only_when_available:
                return [], stats
            for candidate in candidates:
                if candidate != teacher_move:
                    add_pair(teacher_move, candidate, "teacher")

    limited, truncated = limit_pairs_for_position(generated, config.max_pairs_per_position)
    if truncated:
        stats["max_pairs_truncated"] += 1
        stats["pairs_truncated"] += truncated
    return limited, stats


def generate_guard_pairs(
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
    feature_cache: FeatureCache | None = None,
) -> tuple[list[PreferencePair], collections.Counter[str]]:
    stats: collections.Counter[str] = collections.Counter()
    if config.guard_mode == "off":
        return [], stats
    candidates = root_candidate_moves(root_scores, legal_moves)
    ranks = root_score_ranks(root_scores)
    generated: list[PreferencePair] = []

    if config.guard_mode == "base-agreement":
        preferred_move: str | None = None
        protected: set[str] = set()
        if exact_best and engine_move in exact_best:
            preferred_move = engine_move
            protected = set(exact_best)
        elif engine_move == teacher_move:
            preferred_move = engine_move
            protected = {teacher_move}
        else:
            stats["guard_base_disagreement_skipped"] += 1
            return [], stats

        preferred_score = root_scores.get(preferred_move)
        for candidate in candidates:
            if candidate in protected or candidate == preferred_move:
                continue
            candidate_score = root_scores.get(candidate)
            if preferred_score is not None and candidate_score is not None and candidate_score >= preferred_score:
                continue
            pair = make_preference_pair(
                config=config,
                source=source,
                split=split,
                board_text=board_text,
                teacher_move=teacher_move,
                engine_move=engine_move,
                preferred_move=preferred_move,
                other_move=candidate,
                pair_kind="guard",
                exact_best=exact_best,
                root_scores=root_scores,
                ranks=ranks,
                objective_weight_override=config.guard_weight,
                feature_cache=feature_cache,
            )
            if pair is None:
                stats["guard_pair_skipped"] += 1
                continue
            generated.append(pair)

    limited, truncated = limit_pairs_for_position(generated, config.guard_max_pairs_per_position)
    if limited:
        stats["guarded_positions"] += 1
    if truncated:
        stats["guard_max_pairs_truncated"] += 1
        stats["guard_pairs_truncated"] += truncated
    return limited, stats


def _new_diagnostic_group() -> dict[str, Any]:
    return {
        "counters": collections.Counter(),
        "legal_move_count_distribution": collections.Counter(),
        "pair_count_distribution": collections.Counter(),
        "score_margin_distribution": collections.Counter(),
        "rank_margin_distribution": collections.Counter(),
        "teacher_rank_distribution": collections.Counter(),
        "exact_best_top_group_size_distribution": collections.Counter(),
        "pair_weight_mass": 0.0,
    }


def _new_dataset_diagnostics() -> dict[str, Any]:
    return {
        "by_split": collections.defaultdict(_new_diagnostic_group),
        "by_phase": collections.defaultdict(_new_diagnostic_group),
        "by_source_bucket": collections.defaultdict(_new_diagnostic_group),
    }


def _diagnostic_groups(
    diagnostics: dict[str, Any],
    *,
    split: str,
    phase: str,
    bucket: str,
) -> tuple[dict[str, Any], ...]:
    return (
        diagnostics["by_split"][split],
        diagnostics["by_phase"][phase],
        diagnostics["by_source_bucket"][bucket],
    )


def _bump_diagnostic_counter(
    diagnostics: dict[str, Any],
    *,
    split: str,
    phase: str,
    bucket: str,
    key: str,
    amount: int = 1,
) -> None:
    for group in _diagnostic_groups(diagnostics, split=split, phase=phase, bucket=bucket):
        group["counters"][key] += amount


def _bucket_margin(value: int | None) -> str:
    if value is None:
        return "unavailable"
    absolute = abs(value)
    if absolute == 0:
        return "0"
    if absolute <= 1:
        return "1"
    if absolute <= 4:
        return "2-4"
    if absolute <= 8:
        return "5-8"
    if absolute <= 16:
        return "9-16"
    if absolute <= 32:
        return "17-32"
    if absolute <= 64:
        return "33-64"
    if absolute <= 128:
        return "65-128"
    return "129+"


def _bump_pair_diagnostics(
    diagnostics: dict[str, Any],
    *,
    split: str,
    phase: str,
    bucket: str,
    pair: PreferencePair,
) -> None:
    for group in _diagnostic_groups(diagnostics, split=split, phase=phase, bucket=bucket):
        group["score_margin_distribution"][_bucket_margin(pair.score_margin)] += 1
        group["rank_margin_distribution"][_bucket_margin(pair.rank_margin)] += 1
        group["pair_weight_mass"] += pair.pair_weight


def _serialize_diagnostic_group(group: dict[str, Any]) -> dict[str, Any]:
    return {
        "teacher_rows": int(group["counters"]["teacher_rows"]),
        "accepted_rows": int(group["counters"]["accepted_rows"]),
        "illegal_skipped_rows": int(group["counters"]["illegal_skipped_rows"]),
        "teacher_exact_disagreement": int(group["counters"]["teacher_exact_disagreement"]),
        "exact_unavailable": int(group["counters"]["exact_unavailable"]),
        "teacher_in_exact_best": int(group["counters"]["teacher_in_exact_best"]),
        "teacher_not_in_exact_best": int(group["counters"]["teacher_not_in_exact_best"]),
        "engine_in_exact_best": int(group["counters"]["engine_in_exact_best"]),
        "engine_not_in_exact_best": int(group["counters"]["engine_not_in_exact_best"]),
        "exact_best_in_engine_top_group": int(group["counters"]["exact_best_in_engine_top_group"]),
        "exact_best_not_in_engine_top_group": int(group["counters"]["exact_best_not_in_engine_top_group"]),
        "legal_move_count_distribution": {
            str(key): int(value) for key, value in sorted(group["legal_move_count_distribution"].items())
        },
        "pair_count_distribution": {
            str(key): int(value) for key, value in sorted(group["pair_count_distribution"].items())
        },
        "score_margin_distribution": {
            key: int(value) for key, value in sorted(group["score_margin_distribution"].items())
        },
        "rank_margin_distribution": {
            key: int(value) for key, value in sorted(group["rank_margin_distribution"].items())
        },
        "teacher_rank_distribution": {
            str(key): int(value) for key, value in sorted(group["teacher_rank_distribution"].items())
        },
        "exact_best_top_group_size_distribution": {
            str(key): int(value)
            for key, value in sorted(group["exact_best_top_group_size_distribution"].items())
        },
        "pair_weight_mass": float(group["pair_weight_mass"]),
    }


def serialize_dataset_diagnostics(diagnostics: dict[str, Any]) -> dict[str, Any]:
    return {
        dimension: {
            key: _serialize_diagnostic_group(group)
            for key, group in sorted(groups.items())
        }
        for dimension, groups in diagnostics.items()
    }


def collect_training_data(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
    pair_feature_cache: FeatureCache | None = None,
) -> tuple[list[PreferencePair], list[ListwiseExample], list[ValidationRecord], dict[str, Any]]:
    total_start = time.perf_counter()
    label_start = time.perf_counter()
    sources = read_jsonl_sources(config.teacher_labels)
    label_load_seconds = time.perf_counter() - label_start
    exact_start = time.perf_counter()
    exact_by_board = load_exact_by_board(config.exact_labels) if config.exact_labels else {}
    exact_load_seconds = time.perf_counter() - exact_start
    pair_feature_cache = pair_feature_cache or FeatureCache(config.families, {})
    pairs: list[PreferencePair] = []
    listwise_examples: list[ListwiseExample] = []
    validation_records: list[ValidationRecord] = []
    stats: collections.Counter[str] = collections.Counter()
    bucket_pair_counts: collections.Counter[str] = collections.Counter()
    bucket_pair_weight_mass: collections.Counter[str] = collections.Counter()
    dataset_diagnostics = _new_dataset_diagnostics()
    stats["teacher_rows"] = len(sources)
    stats["analysis_cache_hits"] = 0
    stats["analysis_cache_misses"] = 0
    stats["analysis_cache_writes"] = 0
    stats["analysis_jobs"] = config.analysis_jobs
    entries: list[ValidationRecord | PreparedPosition] = []
    analysis_requests: list[AnalysisRequest] = []
    eval_config_hash = sha256_file(config.eval_config)
    teacher_manifest = _path_hash_manifest(config.teacher_labels)
    exact_manifest = _path_hash_manifest(config.exact_labels)
    listwise_feature_cache = maybe_make_listwise_feature_cache(
        config=config,
        teacher_manifest=teacher_manifest,
        exact_manifest=exact_manifest,
        eval_config_hash=eval_config_hash,
    )

    for source_index, source in enumerate(sources):
        record = source.record
        split = split_for_source(source, config.seed)
        bucket = bucket_for_source(config, source)
        board_text_for_phase = board_text_from_record(record)
        phase = (
            phase_for_board(board_text_for_phase, config.phase_cutoffs)
            if board_text_for_phase is not None
            else "unknown"
        )
        _bump_diagnostic_counter(
            dataset_diagnostics,
            split=split,
            phase=phase,
            bucket=bucket,
            key="teacher_rows",
        )
        if not accepted_teacher_record(record):
            stats["unusable_teacher_rows"] += 1
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=bucket,
                key="illegal_skipped_rows",
            )
            continue
        stats["accepted_teacher_rows"] += 1
        _bump_diagnostic_counter(
            dataset_diagnostics,
            split=split,
            phase=phase,
            bucket=bucket,
            key="accepted_rows",
        )
        stats[f"{split}_rows"] += 1

        board_text = board_text_from_record(record)
        assert board_text is not None
        phase = phase_for_board(board_text, config.phase_cutoffs)
        teacher_move = normalize_move(teacher_move_from_record(record))
        if teacher_move is None:
            stats["missing_teacher_move_skipped"] += 1
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=bucket,
                key="illegal_skipped_rows",
            )
            continue
        legal_moves = legal_moves_for_board(board_text)
        for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=bucket):
            group["legal_move_count_distribution"][len(legal_moves)] += 1
        if teacher_move not in legal_moves:
            stats["illegal_teacher_move_skipped"] += 1
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=bucket,
                key="illegal_skipped_rows",
            )
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
        if exact_best:
            for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=bucket):
                group["exact_best_top_group_size_distribution"][len(exact_best)] += 1
            if teacher_move in exact_best:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=bucket,
                    key="teacher_in_exact_best",
                )
            else:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=bucket,
                    key="teacher_not_in_exact_best",
                )
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=bucket,
                    key="teacher_exact_disagreement",
                )
        else:
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=bucket,
                key="exact_unavailable",
            )

        if config.split != "all" and split != config.split:
            stats["split_skipped"] += 1
            continue

        drop_exact_disagreement = (
            exact_best
            and teacher_move not in exact_best
            and (
                config.drop_teacher_exact_disagreement
                or (config.pair_mode != "exact-aware" and config.objective == "pairwise-logistic")
            )
        )
        if drop_exact_disagreement:
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
        if exact_best and teacher_move not in exact_best and (
            config.pair_mode == "exact-aware" or config.objective != "pairwise-logistic"
        ):
            stats["teacher_exact_disagreements_used_by_exact_aware"] += 1
        if (
            exact_best
            and config.max_top_group_size_for_training > 0
            and len(exact_best) > config.max_top_group_size_for_training
        ):
            stats["large_exact_top_group_skipped"] += 1
            entries.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="large_exact_top_group",
                    teacher_move=teacher_move,
                    engine_move="",
                    exact_best=exact_best,
                )
            )
            continue
        if (
            not exact_best
            and config.pair_mode == "exact-aware"
            and config.exact_aware_only_when_available
            and config.guard_mode == "off"
        ):
            stats["exact_unavailable_fallback_positions"] += 1
            stats["no_pair_generated_skipped"] += 1
            entries.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="exact_unavailable",
                    teacher_move=teacher_move,
                    engine_move="",
                    exact_best=exact_best,
                )
            )
            continue

        engine_move = precomputed_engine_move(record, legal_moves)
        needs_analysis = (
            engine_move is None
            or config.pair_mode != "best-vs-engine"
            or config.guard_mode != "off"
            or config.objective != "pairwise-logistic"
        )
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
                bucket=bucket,
                board_text=board_text,
                teacher_move=teacher_move,
                engine_move=engine_move,
                legal_moves=legal_moves,
                exact_best=exact_best,
                needs_analysis=needs_analysis,
            )
        )

    analysis_start = time.perf_counter()
    analysis_by_source = analyze_requests(
        config=config,
        requests=analysis_requests,
        analyzer=analyzer,
        stats=stats,
        eval_config_hash=eval_config_hash,
    )
    analysis_elapsed_seconds = time.perf_counter() - analysis_start
    feature_start = time.perf_counter()

    pair_generation_start = time.perf_counter()
    for entry in entries:
        if isinstance(entry, ValidationRecord):
            validation_records.append(entry)
            continue
        source = entry.source
        split = entry.split
        phase = entry.phase
        bucket = entry.bucket
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
        if exact_best:
            if engine_move in exact_best:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=bucket,
                    key="engine_in_exact_best",
                )
            else:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=bucket,
                    key="engine_not_in_exact_best",
                )
        if engine_move == teacher_move:
            stats["already_agreed"] += 1
            if (
                config.pair_mode == "best-vs-engine"
                and config.guard_mode == "off"
                and config.objective == "pairwise-logistic"
            ):
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
        ranks = root_score_ranks(root_scores)
        teacher_rank = ranks.get(teacher_move)
        if exact_best and teacher_rank is not None:
            for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=bucket):
                group["teacher_rank_distribution"][teacher_rank] += 1
        if exact_best and root_scores:
            top_score = max(root_scores.values())
            engine_top_group = {move for move, score in root_scores.items() if score == top_score}
            if engine_top_group.intersection(exact_best):
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=bucket,
                    key="exact_best_in_engine_top_group",
                )
            else:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=bucket,
                    key="exact_best_not_in_engine_top_group",
                )
        if config.objective != "pairwise-logistic" or config.calibrate_output_scale:
            exact_record = exact_by_board.get(board_key(board_text))
            listwise_example = make_listwise_example(
                config=config,
                source=source,
                split=split,
                board_text=board_text,
                teacher_move=teacher_move,
                engine_move=engine_move,
                legal_moves=legal_moves,
                exact_best=exact_best,
                exact_scores=exact_move_scores(exact_record),
                exact_score=exact_root_score(exact_record),
                root_scores=root_scores,
                feature_cache=listwise_feature_cache,
            )
            if listwise_example is not None:
                listwise_examples.append(listwise_example)
                stats["listwise_examples"] += 1
                if listwise_example.exact_best_moves:
                    stats["listwise_exact_examples"] += 1
                if config.objective != "pairwise-logistic":
                    stats["paired_positions"] += 1
                    validation_records.append(
                        make_validation_record(
                            source=source,
                            split=split,
                            phase=phase,
                            status="paired",
                            teacher_move=teacher_move,
                            engine_move=engine_move,
                            preferred_move=listwise_example.target_moves[0],
                            other_move="",
                            pair_kind="listwise",
                            pair_weight=listwise_example.example_weight,
                            exact_best=exact_best,
                        )
                    )
                    for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=bucket):
                        group["pair_count_distribution"][1] += 1
                    continue
            elif config.objective != "pairwise-logistic":
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
            feature_cache=pair_feature_cache,
        )
        stats.update(pair_stats)
        guard_pairs, guard_stats = generate_guard_pairs(
            config=config,
            source=source,
            split=split,
            board_text=board_text,
            teacher_move=teacher_move,
            engine_move=engine_move,
            legal_moves=legal_moves,
            exact_best=exact_best,
            root_scores=root_scores,
            feature_cache=pair_feature_cache,
        )
        stats.update(guard_stats)
        all_position_pairs = [*position_pairs, *guard_pairs]
        for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=bucket):
            group["pair_count_distribution"][len(all_position_pairs)] += 1
        if not all_position_pairs:
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

        pairs.extend(all_position_pairs)
        stats["paired_positions"] += 1
        stats["preference_pairs"] += len(all_position_pairs)
        stats["main_preference_pairs"] += len(position_pairs)
        stats["max_pairs_in_position"] = max(stats["max_pairs_in_position"], len(all_position_pairs))
        for pair in all_position_pairs:
            stats[f"{pair.phase}_pairs"] += 1
            stats[f"{pair.pair_kind}_pairs"] += 1
            bucket_pair_counts[pair.bucket] += 1
            bucket_pair_weight_mass[pair.bucket] += pair.pair_weight
            if pair.pair_kind == "exact":
                stats["exact_aware_pairs"] += 1
            if pair.pair_kind == "guard":
                stats["guard_pair_weight_mass"] += pair.pair_weight
            else:
                stats["main_pair_weight_mass"] += pair.pair_weight
            _bump_pair_diagnostics(
                dataset_diagnostics,
                split=split,
                phase=pair.phase,
                bucket=pair.bucket,
                pair=pair,
            )
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

    pair_generation_seconds = time.perf_counter() - pair_generation_start
    feature_construction_seconds = time.perf_counter() - feature_start
    if listwise_feature_cache is not None:
        listwise_feature_cache.save()

    for key in (
        "accepted_teacher_rows",
        "already_agreed",
        "candidate_pair_skipped",
        "exact_pairs",
        "exact_aware_pairs",
        "exact_unavailable_fallback_positions",
        "guard_base_disagreement_skipped",
        "guard_max_pairs_truncated",
        "guard_pair_skipped",
        "guard_pair_weight_mass",
        "guard_pairs",
        "guard_pairs_truncated",
        "guarded_positions",
        "large_exact_top_group_skipped",
        "listwise_examples",
        "listwise_exact_examples",
        "main_pair_weight_mass",
        "main_preference_pairs",
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
    row_stats["listwise_feature_cache_mode"] = config.listwise_feature_cache_mode
    row_stats["listwise_feature_cache_dir"] = (
        str(config.listwise_feature_cache_dir) if config.listwise_feature_cache_dir is not None else ""
    )
    row_stats["label_load_seconds"] = label_load_seconds
    row_stats["exact_load_seconds"] = exact_load_seconds
    row_stats["analysis_seconds"] = analysis_elapsed_seconds
    row_stats.setdefault("analysis_elapsed_seconds", analysis_elapsed_seconds)
    row_stats["feature_construction_seconds"] = feature_construction_seconds
    row_stats["total_collect_seconds"] = time.perf_counter() - total_start
    row_stats["listwise_feature_examples_per_second"] = (
        row_stats["listwise_examples"] / feature_construction_seconds
        if feature_construction_seconds > 0.0
        else 0.0
    )
    if listwise_feature_cache is not None:
        row_stats["listwise_feature_cache_path"] = str(listwise_feature_cache.path)
        row_stats["listwise_feature_cache_hits"] = listwise_feature_cache.hits
        row_stats["listwise_feature_cache_misses"] = listwise_feature_cache.misses
        row_stats["listwise_feature_cache_writes"] = listwise_feature_cache.writes
        row_stats["listwise_feature_cache_entries"] = len(listwise_feature_cache.values)
        row_stats["listwise_feature_cache_load_seconds"] = listwise_feature_cache.load_seconds
        row_stats["listwise_feature_cache_save_seconds"] = listwise_feature_cache.save_seconds
    else:
        row_stats["listwise_feature_cache_path"] = ""
        row_stats["listwise_feature_cache_hits"] = 0
        row_stats["listwise_feature_cache_misses"] = 0
        row_stats["listwise_feature_cache_writes"] = 0
        row_stats["listwise_feature_cache_entries"] = 0
        row_stats["listwise_feature_cache_load_seconds"] = 0.0
        row_stats["listwise_feature_cache_save_seconds"] = 0.0
    row_stats["avg_pairs_per_position"] = (
        row_stats["preference_pairs"] / row_stats["paired_positions"]
        if row_stats["paired_positions"]
        else 0.0
    )
    row_stats["bucket_pair_counts"] = {
        bucket: int(bucket_pair_counts[bucket]) for bucket in sorted(bucket_pair_counts)
    }
    row_stats["bucket_pair_weight_mass"] = {
        bucket: float(bucket_pair_weight_mass[bucket]) for bucket in sorted(bucket_pair_weight_mass)
    }
    row_stats["dataset_diagnostics"] = serialize_dataset_diagnostics(dataset_diagnostics)
    row_stats["timing"] = {
        "label_load_seconds": round(label_load_seconds, 6),
        "exact_load_seconds": round(exact_load_seconds, 6),
        "analysis_seconds": round(analysis_elapsed_seconds, 6),
        "feature_construction_seconds": round(feature_construction_seconds, 6),
        "pair_generation_seconds": round(pair_generation_seconds, 6),
        "collect_training_data_seconds": round(row_stats["total_collect_seconds"], 6),
    }
    row_stats["feature_cache"] = {
        "hits": int(pair_feature_cache.hits),
        "misses": int(pair_feature_cache.misses),
        "hit_rate": pair_feature_cache.hit_rate,
        "entries": len(pair_feature_cache.counts_by_child),
    }
    return pairs, listwise_examples, validation_records, row_stats


def collect_pairs(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> tuple[list[PreferencePair], list[ValidationRecord], dict[str, Any]]:
    pairs, _, validation_records, row_stats = collect_training_data(config, analyzer=analyzer)
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


def model_margin(config: TrainerConfig, weights: WeightsByPhase, pair: PreferencePair) -> float:
    del config
    return pair_margin(weights, pair)


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


def lazy_candidate_score(
    config: TrainerConfig,
    lazy_weights: dict[str, LazyPhaseWeights],
    candidate: CandidateMove,
) -> float:
    del config
    return sum(
        lazy_weights[candidate.phase].effective_value(key) * value
        for key, value in candidate.features.items()
    )


def candidate_score(config: TrainerConfig, weights: WeightsByPhase, candidate: CandidateMove) -> float:
    del config
    return sum(weights.get(candidate.phase, {}).get(key, 0.0) * value for key, value in candidate.features.items())


def add_sparse_update(
    phase_weights: LazyPhaseWeights,
    features: dict[FeatureKey, int],
    coefficient: float,
    maximum: float,
) -> None:
    if coefficient == 0.0:
        return
    for key, value in features.items():
        phase_weights.add_effective_update(key, coefficient * value, maximum)


def add_candidate_delta_update(
    lazy_weights: dict[str, LazyPhaseWeights],
    positive: CandidateMove,
    negative: CandidateMove,
    coefficient: float,
    maximum: float,
) -> None:
    add_sparse_update(lazy_weights[positive.phase], positive.features, coefficient, maximum)
    add_sparse_update(lazy_weights[negative.phase], negative.features, -coefficient, maximum)


def softmax_probabilities(scores: list[float]) -> list[float]:
    if not scores:
        return []
    maximum = max(scores)
    exps = [math.exp(score - maximum) for score in scores]
    total = sum(exps)
    return [value / total for value in exps]


def compact_listwise_dataset(examples: list[ListwiseExample]) -> CompactListwiseDataset:
    example_offsets: list[int] = [0]
    candidate_feature_offsets: list[int] = [0]
    candidate_moves: list[str] = []
    candidate_phase_ids: list[int] = []
    candidate_exact_scores: list[int | None] = []
    candidate_target_mask: list[bool] = []
    candidate_target_probabilities: list[float] = []
    candidate_exact_best_mask: list[bool] = []
    example_teacher_moves: list[str] = []
    example_exact_root_scores: list[int | None] = []
    example_weights: list[float] = []
    example_splits: list[str] = []
    feature_keys: list[FeatureKey] = []
    feature_id_by_key: dict[FeatureKey, int] = {}
    feature_ids: list[int] = []
    feature_values: list[int] = []
    for example in examples:
        target_moves = set(example.target_moves)
        exact_best_moves = set(example.exact_best_moves)
        if example.target_probabilities is not None:
            if len(example.target_probabilities) != len(example.candidates):
                raise ScriptError("listwise target probability count must match candidate count")
            target_probabilities = example.target_probabilities
        else:
            target_count = max(1, sum(1 for candidate in example.candidates if candidate.move in target_moves))
            target_probabilities = tuple(
                (1.0 / target_count) if candidate.move in target_moves else 0.0
                for candidate in example.candidates
            )
        for candidate in example.candidates:
            local_index = len(candidate_moves) - example_offsets[-1]
            candidate_moves.append(candidate.move)
            candidate_phase_ids.append(PHASE_TO_ID[candidate.phase])
            candidate_exact_scores.append(candidate.exact_score)
            candidate_target_mask.append(candidate.move in target_moves)
            candidate_target_probabilities.append(target_probabilities[local_index])
            candidate_exact_best_mask.append(candidate.move in exact_best_moves)
            for key, value in candidate.features.items():
                feature_id = feature_id_by_key.get(key)
                if feature_id is None:
                    feature_id = len(feature_keys)
                    feature_id_by_key[key] = feature_id
                    feature_keys.append(key)
                feature_ids.append(feature_id)
                feature_values.append(value)
            candidate_feature_offsets.append(len(feature_ids))
        example_offsets.append(len(candidate_moves))
        example_teacher_moves.append(example.teacher_move)
        example_exact_root_scores.append(example.exact_root_score)
        example_weights.append(example.example_weight)
        example_splits.append(example.split)
    return CompactListwiseDataset(
        example_offsets=tuple(example_offsets),
        candidate_feature_offsets=tuple(candidate_feature_offsets),
        candidate_moves=tuple(candidate_moves),
        candidate_phase_ids=tuple(candidate_phase_ids),
        candidate_exact_scores=tuple(candidate_exact_scores),
        candidate_target_mask=tuple(candidate_target_mask),
        candidate_target_probabilities=tuple(candidate_target_probabilities),
        candidate_exact_best_mask=tuple(candidate_exact_best_mask),
        example_teacher_moves=tuple(example_teacher_moves),
        example_exact_root_scores=tuple(example_exact_root_scores),
        example_weights=tuple(example_weights),
        example_splits=tuple(example_splits),
        feature_keys=tuple(feature_keys),
        feature_ids=tuple(feature_ids),
        feature_values=tuple(feature_values),
    )


def ensure_compact_listwise_dataset(examples: ListwiseData) -> CompactListwiseDataset:
    if isinstance(examples, CompactListwiseDataset):
        return examples
    return compact_listwise_dataset(examples)


def compact_listwise_dataset_subset(
    dataset: CompactListwiseDataset,
    example_indexes: list[int],
) -> CompactListwiseDataset:
    example_offsets: list[int] = [0]
    candidate_feature_offsets: list[int] = [0]
    candidate_moves: list[str] = []
    candidate_phase_ids: list[int] = []
    candidate_exact_scores: list[int | None] = []
    candidate_target_mask: list[bool] = []
    candidate_target_probabilities: list[float] = []
    candidate_exact_best_mask: list[bool] = []
    example_teacher_moves: list[str] = []
    example_exact_root_scores: list[int | None] = []
    example_weights: list[float] = []
    example_splits: list[str] = []
    feature_ids: list[int] = []
    feature_values: list[int] = []
    for example_index in example_indexes:
        candidate_start = dataset.example_offsets[example_index]
        candidate_end = dataset.example_offsets[example_index + 1]
        for candidate_index in range(candidate_start, candidate_end):
            candidate_moves.append(dataset.candidate_moves[candidate_index])
            candidate_phase_ids.append(dataset.candidate_phase_ids[candidate_index])
            candidate_exact_scores.append(dataset.candidate_exact_scores[candidate_index])
            candidate_target_mask.append(dataset.candidate_target_mask[candidate_index])
            candidate_target_probabilities.append(
                dataset.candidate_target_probabilities[candidate_index]
            )
            candidate_exact_best_mask.append(dataset.candidate_exact_best_mask[candidate_index])
            feature_start = dataset.candidate_feature_offsets[candidate_index]
            feature_end = dataset.candidate_feature_offsets[candidate_index + 1]
            feature_ids.extend(dataset.feature_ids[feature_start:feature_end])
            feature_values.extend(dataset.feature_values[feature_start:feature_end])
            candidate_feature_offsets.append(len(feature_ids))
        example_offsets.append(len(candidate_moves))
        example_teacher_moves.append(dataset.example_teacher_moves[example_index])
        example_exact_root_scores.append(dataset.example_exact_root_scores[example_index])
        example_weights.append(dataset.example_weights[example_index])
        example_splits.append(dataset.example_splits[example_index])
    return CompactListwiseDataset(
        example_offsets=tuple(example_offsets),
        candidate_feature_offsets=tuple(candidate_feature_offsets),
        candidate_moves=tuple(candidate_moves),
        candidate_phase_ids=tuple(candidate_phase_ids),
        candidate_exact_scores=tuple(candidate_exact_scores),
        candidate_target_mask=tuple(candidate_target_mask),
        candidate_target_probabilities=tuple(candidate_target_probabilities),
        candidate_exact_best_mask=tuple(candidate_exact_best_mask),
        example_teacher_moves=tuple(example_teacher_moves),
        example_exact_root_scores=tuple(example_exact_root_scores),
        example_weights=tuple(example_weights),
        example_splits=tuple(example_splits),
        feature_keys=dataset.feature_keys,
        feature_ids=tuple(feature_ids),
        feature_values=tuple(feature_values),
    )


def compact_candidate_score(
    config: TrainerConfig,
    weights: WeightsByPhase,
    dataset: CompactListwiseDataset,
    candidate_index: int,
) -> float:
    del config
    phase = ID_TO_PHASE[dataset.candidate_phase_ids[candidate_index]]
    phase_weights = weights.get(phase, {})
    start = dataset.candidate_feature_offsets[candidate_index]
    end = dataset.candidate_feature_offsets[candidate_index + 1]
    score = 0.0
    for offset in range(start, end):
        score += phase_weights.get(dataset.feature_keys[dataset.feature_ids[offset]], 0.0) * dataset.feature_values[offset]
    return score


def compact_lazy_candidate_score(
    config: TrainerConfig,
    lazy_weights: dict[str, LazyPhaseWeights],
    dataset: CompactListwiseDataset,
    candidate_index: int,
) -> float:
    del config
    phase = ID_TO_PHASE[dataset.candidate_phase_ids[candidate_index]]
    phase_weights = lazy_weights[phase]
    start = dataset.candidate_feature_offsets[candidate_index]
    end = dataset.candidate_feature_offsets[candidate_index + 1]
    score = 0.0
    for offset in range(start, end):
        score += phase_weights.effective_value(dataset.feature_keys[dataset.feature_ids[offset]]) * dataset.feature_values[offset]
    return score


def add_compact_sparse_update(
    lazy_weights: dict[str, LazyPhaseWeights],
    dataset: CompactListwiseDataset,
    candidate_index: int,
    coefficient: float,
    maximum: float,
) -> None:
    if coefficient == 0.0:
        return
    phase = ID_TO_PHASE[dataset.candidate_phase_ids[candidate_index]]
    phase_weights = lazy_weights[phase]
    start = dataset.candidate_feature_offsets[candidate_index]
    end = dataset.candidate_feature_offsets[candidate_index + 1]
    for offset in range(start, end):
        phase_weights.add_effective_update(
            dataset.feature_keys[dataset.feature_ids[offset]],
            coefficient * dataset.feature_values[offset],
            maximum,
        )


def add_compact_candidate_delta_update(
    lazy_weights: dict[str, LazyPhaseWeights],
    dataset: CompactListwiseDataset,
    positive_index: int,
    negative_index: int,
    coefficient: float,
    maximum: float,
) -> None:
    add_compact_sparse_update(lazy_weights, dataset, positive_index, coefficient, maximum)
    add_compact_sparse_update(lazy_weights, dataset, negative_index, -coefficient, maximum)


def sign_anchor_enabled(config: TrainerConfig) -> bool:
    return config.soft_sign_anchor_mode != "off" and config.soft_sign_anchor_weight > 0.0


def sign_anchor_loss_value(config: TrainerConfig, exact_root_score: int | None, score: float) -> float:
    exact_sign = sign(exact_root_score)
    if exact_sign == 0:
        return 0.0
    return max(0.0, config.soft_sign_anchor_margin - exact_sign * score)


def selected_anchor_candidate_index(
    dataset: CompactListwiseDataset,
    candidate_start: int,
    candidate_end: int,
    scores: list[float],
) -> int:
    best_score = max(scores)
    best_indexes = [
        candidate_index
        for candidate_index, score_value in zip(range(candidate_start, candidate_end), scores, strict=True)
        if score_value == best_score
    ]
    return min(best_indexes, key=lambda candidate_index: dataset.candidate_moves[candidate_index])


def expected_anchor_score(scores: list[float], probabilities: list[float]) -> float:
    return sum(score * probability for score, probability in zip(scores, probabilities, strict=True))


def apply_soft_sign_anchor_update(
    *,
    config: TrainerConfig,
    lazy_weights: dict[str, LazyPhaseWeights],
    dataset: CompactListwiseDataset,
    example_index: int,
    candidate_start: int,
    candidate_end: int,
    scores: list[float],
    probabilities: list[float],
) -> tuple[int, float]:
    exact_sign = sign(dataset.example_exact_root_scores[example_index])
    if exact_sign == 0 or not sign_anchor_enabled(config):
        return 0, 0.0
    if config.soft_sign_anchor_mode == "selected":
        anchor_index = selected_anchor_candidate_index(dataset, candidate_start, candidate_end, scores)
        anchor_score = scores[anchor_index - candidate_start]
        loss = sign_anchor_loss_value(config, dataset.example_exact_root_scores[example_index], anchor_score)
        if loss <= 0.0:
            return 0, 0.0
        coefficient = (
            config.learning_rate
            * dataset.example_weights[example_index]
            * config.soft_sign_anchor_weight
            * exact_sign
        )
        add_compact_sparse_update(lazy_weights, dataset, anchor_index, coefficient, config.max_abs_weight)
        return 1, loss
    if config.soft_sign_anchor_mode == "expected":
        anchor_score = expected_anchor_score(scores, probabilities)
        loss = sign_anchor_loss_value(config, dataset.example_exact_root_scores[example_index], anchor_score)
        if loss <= 0.0:
            return 0, 0.0
        updates = 0
        base_coefficient = (
            config.learning_rate
            * dataset.example_weights[example_index]
            * config.soft_sign_anchor_weight
            * exact_sign
        )
        for local_index, candidate_index in enumerate(range(candidate_start, candidate_end)):
            coefficient = base_coefficient * probabilities[local_index]
            add_compact_sparse_update(
                lazy_weights,
                dataset,
                candidate_index,
                coefficient,
                config.max_abs_weight,
            )
            updates += 1
        return updates, loss
    return 0, 0.0


def train_listwise_weights(
    config: TrainerConfig,
    examples: ListwiseData,
) -> tuple[WeightsByPhase, list[dict[str, Any]]]:
    dataset = ensure_compact_listwise_dataset(examples)
    weights: WeightsByPhase = {phase: {} for phase in PHASES}
    history: list[dict[str, Any]] = []
    if dataset.example_count == 0:
        for epoch in range(1, config.epochs + 1):
            history.append({"epoch": epoch, **evaluate_listwise_examples(config, weights, dataset)})
        return weights, history

    rng = random.Random(config.seed)
    lazy_weights = {phase: LazyPhaseWeights() for phase in PHASES}
    shrink_factor = max(0.0, 1.0 - config.learning_rate * config.l2)
    for epoch in range(1, config.epochs + 1):
        epoch_start = time.perf_counter()
        update_count = 0
        sign_anchor_updates = 0
        sign_anchor_loss_sum = 0.0
        sign_anchor_rows = 0
        shuffled = list(range(dataset.example_count))
        rng.shuffle(shuffled)
        for example_index in shuffled:
            candidate_start = dataset.example_offsets[example_index]
            candidate_end = dataset.example_offsets[example_index + 1]
            if config.l2 != 0.0:
                for phase_weights in lazy_weights.values():
                    phase_weights.shrink(shrink_factor)
            scores = [
                compact_lazy_candidate_score(config, lazy_weights, dataset, candidate_index)
                for candidate_index in range(candidate_start, candidate_end)
            ]
            probabilities = softmax_probabilities(scores)
            for local_index, candidate_index in enumerate(range(candidate_start, candidate_end)):
                target_probability = dataset.candidate_target_probabilities[candidate_index]
                coefficient = (
                    config.learning_rate
                    * dataset.example_weights[example_index]
                    * (target_probability - probabilities[local_index])
                )
                add_compact_sparse_update(
                    lazy_weights,
                    dataset,
                    candidate_index,
                    coefficient,
                    config.max_abs_weight,
                )
                update_count += 1

            updates, anchor_loss = apply_soft_sign_anchor_update(
                config=config,
                lazy_weights=lazy_weights,
                dataset=dataset,
                example_index=example_index,
                candidate_start=candidate_start,
                candidate_end=candidate_end,
                scores=scores,
                probabilities=probabilities,
            )
            if updates:
                update_count += updates
                sign_anchor_updates += updates
                sign_anchor_loss_sum += anchor_loss
                sign_anchor_rows += 1

            if config.tie_penalty > 0.0:
                target_indexes = [
                    candidate_index
                    for candidate_index in range(candidate_start, candidate_end)
                    if dataset.candidate_target_mask[candidate_index]
                ]
                if target_indexes:
                    best_target = max(
                        target_indexes,
                        key=lambda candidate_index: compact_lazy_candidate_score(
                            config,
                            lazy_weights,
                            dataset,
                            candidate_index,
                        ),
                    )
                    current_scores = [
                        compact_lazy_candidate_score(config, lazy_weights, dataset, candidate_index)
                        for candidate_index in range(candidate_start, candidate_end)
                    ]
                    best_score = max(current_scores)
                    top_group = [
                        candidate_index
                        for candidate_index, score in zip(range(candidate_start, candidate_end), current_scores, strict=True)
                        if best_score - score <= config.top_group_margin
                    ]
                    if len(top_group) > config.target_top_group_size:
                        for candidate_index in top_group:
                            if dataset.candidate_target_mask[candidate_index]:
                                continue
                            margin = (
                                compact_lazy_candidate_score(config, lazy_weights, dataset, best_target)
                                - compact_lazy_candidate_score(config, lazy_weights, dataset, candidate_index)
                            )
                            factor = stable_sigmoid_negative_margin(margin)
                            coefficient = (
                                config.learning_rate
                                * dataset.example_weights[example_index]
                                * config.tie_penalty
                                * factor
                            )
                            add_compact_candidate_delta_update(
                                lazy_weights,
                                dataset,
                                best_target,
                                candidate_index,
                                coefficient,
                                config.max_abs_weight,
                            )
                            update_count += 2

            if config.objective == "exact-aware-listwise" and config.sign_penalty > 0.0:
                exact_scored = [
                    candidate_index
                    for candidate_index in range(candidate_start, candidate_end)
                    if dataset.candidate_exact_scores[candidate_index] is not None
                ]
                for better in exact_scored:
                    for worse in exact_scored:
                        better_score = dataset.candidate_exact_scores[better]
                        worse_score = dataset.candidate_exact_scores[worse]
                        assert better_score is not None
                        assert worse_score is not None
                        exact_delta = better_score - worse_score
                        if exact_delta <= 0:
                            continue
                        margin = (
                            compact_lazy_candidate_score(config, lazy_weights, dataset, better)
                            - compact_lazy_candidate_score(config, lazy_weights, dataset, worse)
                        )
                        factor = stable_sigmoid_negative_margin(margin)
                        coefficient = (
                            config.learning_rate
                            * dataset.example_weights[example_index]
                            * config.sign_penalty
                            * min(abs(exact_delta), 64)
                            / 64.0
                            * factor
                        )
                        add_compact_candidate_delta_update(
                            lazy_weights,
                            dataset,
                            better,
                            worse,
                            coefficient,
                            config.max_abs_weight,
                        )
                        update_count += 2

        weights = {phase: dict(lazy_weights[phase].materialize()) for phase in PHASES}
        elapsed = time.perf_counter() - epoch_start
        history.append(
            {
                "epoch": epoch,
                **evaluate_listwise_examples(config, weights, dataset),
                "updates": update_count,
                "sign_anchor_rows": sign_anchor_rows,
                "sign_anchor_updates": sign_anchor_updates,
                "sign_anchor_loss": (
                    sign_anchor_loss_sum / sign_anchor_rows if sign_anchor_rows else 0.0
                ),
                "updates_per_second": update_count / elapsed if elapsed > 0.0 else 0.0,
                "epoch_seconds": elapsed,
            }
        )
    return weights, history


def train_weights(
    config: TrainerConfig,
    pairs: list[PreferencePair],
    listwise_examples: ListwiseData | None = None,
) -> tuple[WeightsByPhase, list[dict[str, Any]]]:
    if config.objective != "pairwise-logistic":
        return train_listwise_weights(config, listwise_examples or [])
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


def compact_pair_margin(
    phase_weights: dict[int, float],
    compact: CompactPreferencePairs,
    pair_index: int,
) -> float:
    start = compact.offsets[pair_index]
    end = compact.offsets[pair_index + 1]
    feature_ids = compact.feature_ids
    values = compact.values
    total = 0.0
    for offset in range(start, end):
        total += phase_weights.get(feature_ids[offset], 0.0) * values[offset]
    return total


def compact_weights_to_feature_keys(
    weights_by_phase_id: dict[int, dict[int, float]],
    compact: CompactPreferencePairs,
) -> WeightsByPhase:
    weights: WeightsByPhase = {phase: {} for phase in PHASES}
    for phase_id, phase_weights in weights_by_phase_id.items():
        phase = ID_TO_PHASE[phase_id]
        weights[phase] = {
            compact.feature_keys[feature_id]: value
            for feature_id, value in phase_weights.items()
            if abs(value) >= 1.0e-12
        }
    return weights


def evaluate_compact_pairs(
    config: TrainerConfig,
    weights_by_phase_id: dict[int, dict[int, float]],
    compact: CompactPreferencePairs,
) -> dict[str, Any]:
    if compact.count == 0:
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
    loss_sum = 0.0
    weighted_loss_sum = 0.0
    margin_sum = 0.0
    correct = 0
    weighted_correct = 0.0
    total_weight = 0.0
    for pair_index in range(compact.count):
        phase_id = compact.phase_ids[pair_index]
        margin = compact_pair_margin(weights_by_phase_id[phase_id], compact, pair_index)
        weight = compact.pair_weights[pair_index]
        loss = pair_loss(config.loss, margin)
        loss_sum += loss
        weighted_loss_sum += loss * weight
        margin_sum += margin
        total_weight += weight
        if margin > 0.0:
            correct += 1
            weighted_correct += weight
    l2_term = 0.0
    for phase_weights in weights_by_phase_id.values():
        l2_term += sum(value * value for value in phase_weights.values())
    weighted_loss = weighted_loss_sum / total_weight if total_weight else 0.0
    return {
        "pairs": compact.count,
        "loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "weighted_loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "unweighted_loss": loss_sum / compact.count + 0.5 * config.l2 * l2_term,
        "accuracy": correct / compact.count,
        "weighted_accuracy": weighted_correct / total_weight if total_weight else 0.0,
        "avg_margin": margin_sum / compact.count,
        "total_pair_weight": total_weight,
        "avg_pair_weight": total_weight / compact.count,
    }


def compact_checkpoint_paths(config: TrainerConfig) -> tuple[Path | None, Path | None]:
    if config.checkpoint_dir is None:
        return None, None
    return config.checkpoint_dir / "latest.pkl", config.checkpoint_dir / "latest.json"


def load_compact_checkpoint(
    config: TrainerConfig,
    config_hash: str,
) -> tuple[int, random.Random, dict[int, LazyPhaseWeights], list[dict[str, Any]]] | None:
    if not config.resume_checkpoint:
        return None
    checkpoint_path, _ = compact_checkpoint_paths(config)
    if checkpoint_path is None or not checkpoint_path.exists():
        return None
    try:
        with checkpoint_path.open("rb") as input_file:
            row = pickle.load(input_file)
    except OSError as exc:
        raise ScriptError(f"failed to read checkpoint {checkpoint_path}: {exc}") from exc
    if not isinstance(row, dict) or row.get("schema") != CHECKPOINT_SCHEMA:
        raise ScriptError(f"invalid checkpoint {checkpoint_path}")
    if row.get("config_hash") != config_hash:
        raise ScriptError("checkpoint config hash does not match this run")
    rng = random.Random()
    rng.setstate(row["rng_state"])
    lazy_weights = {phase_id: LazyPhaseWeights() for phase_id in range(len(PHASES))}
    for raw_phase_id, stored in row["weights"].items():
        phase_id = int(raw_phase_id)
        lazy_weights[phase_id].stored = dict(stored)
        lazy_weights[phase_id].scale = 1.0
    return int(row["epoch"]), rng, lazy_weights, list(row.get("history", []))


def write_compact_checkpoint(
    config: TrainerConfig,
    *,
    epoch: int,
    config_hash: str,
    rng: random.Random,
    lazy_weights: dict[int, LazyPhaseWeights],
    history: list[dict[str, Any]],
) -> None:
    checkpoint_path, metadata_path = compact_checkpoint_paths(config)
    if checkpoint_path is None or metadata_path is None:
        return
    checkpoint_path.parent.mkdir(parents=True, exist_ok=True)
    materialized = {
        phase_id: dict(phase_weights.materialize())
        for phase_id, phase_weights in lazy_weights.items()
    }
    row = {
        "schema": CHECKPOINT_SCHEMA,
        "epoch": epoch,
        "config_hash": config_hash,
        "rng_state": rng.getstate(),
        "weights": materialized,
        "history": history,
    }
    try:
        with checkpoint_path.open("wb") as output_file:
            pickle.dump(row, output_file, protocol=pickle.HIGHEST_PROTOCOL)
        metadata_path.write_text(
            json.dumps(
                {
                    "schema": CHECKPOINT_SCHEMA,
                    "epoch": epoch,
                    "config_hash": config_hash,
                    "path": str(checkpoint_path),
                    "generated_at": dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
    except OSError as exc:
        raise ScriptError(f"failed to write checkpoint {checkpoint_path}: {exc}") from exc


def train_compact_weights(
    config: TrainerConfig,
    compact: CompactPreferencePairs,
) -> tuple[WeightsByPhase, list[dict[str, Any]], dict[str, Any]]:
    if config.objective != "pairwise-logistic":
        raise ScriptError("compact SGD only supports pairwise-logistic")
    start = time.perf_counter()
    updates = 0
    config_hash = training_config_hash(config)
    history: list[dict[str, Any]] = []
    rng = random.Random(config.seed)
    lazy_weights = {phase_id: LazyPhaseWeights() for phase_id in range(len(PHASES))}
    start_epoch = 1
    resumed_from_epoch = 0
    checkpoint = load_compact_checkpoint(config, config_hash)
    if checkpoint is not None:
        resumed_from_epoch, rng, lazy_weights, history = checkpoint
        start_epoch = resumed_from_epoch + 1
    if compact.count == 0:
        weights = compact_weights_to_feature_keys(
            {phase_id: phase_weights.materialize() for phase_id, phase_weights in lazy_weights.items()},
            compact,
        )
        for epoch in range(start_epoch, config.epochs + 1):
            history.append({"epoch": epoch, **evaluate_pairs(config, weights, [])})
        return weights, history, {
            "sgd_seconds": round(time.perf_counter() - start, 6),
            "updates_per_second": 0.0,
            "updates": 0,
            "resumed_from_epoch": resumed_from_epoch,
        }

    shrink_factor = max(0.0, 1.0 - config.learning_rate * config.l2)
    feature_ids = compact.feature_ids
    values = compact.values
    offsets = compact.offsets
    for epoch in range(start_epoch, config.epochs + 1):
        order = list(range(compact.count))
        rng.shuffle(order)
        for pair_index in order:
            phase_id = compact.phase_ids[pair_index]
            phase_weights = lazy_weights[phase_id]
            if config.l2 != 0.0:
                phase_weights.shrink(shrink_factor)
            margin = 0.0
            start_offset = offsets[pair_index]
            end_offset = offsets[pair_index + 1]
            for offset in range(start_offset, end_offset):
                margin += phase_weights.effective_value(feature_ids[offset]) * values[offset]
            if config.loss == "hinge":
                factor = 1.0 if margin < 1.0 else 0.0
            else:
                factor = stable_sigmoid_negative_margin(margin)
            if factor == 0.0:
                continue
            coefficient = config.learning_rate * compact.pair_weights[pair_index] * factor
            for offset in range(start_offset, end_offset):
                phase_weights.add_effective_update(
                    feature_ids[offset],
                    coefficient * values[offset],
                    config.max_abs_weight,
                )
                updates += 1
        dense_weights_by_phase = {
            phase_id: dict(phase_weights.materialize())
            for phase_id, phase_weights in lazy_weights.items()
        }
        metrics = evaluate_compact_pairs(config, dense_weights_by_phase, compact)
        history.append({"epoch": epoch, **metrics})
        write_compact_checkpoint(
            config,
            epoch=epoch,
            config_hash=config_hash,
            rng=rng,
            lazy_weights=lazy_weights,
            history=history,
        )
    weights_by_phase_id = {
        phase_id: dict(phase_weights.materialize())
        for phase_id, phase_weights in lazy_weights.items()
    }
    weights = compact_weights_to_feature_keys(weights_by_phase_id, compact)
    elapsed = time.perf_counter() - start
    return weights, history, {
        "sgd_seconds": round(elapsed, 6),
        "updates_per_second": updates / elapsed if elapsed > 0.0 else 0.0,
        "updates": updates,
        "resumed_from_epoch": resumed_from_epoch,
        "checkpoint_dir": str(config.checkpoint_dir) if config.checkpoint_dir is not None else "",
        "config_hash": config_hash,
    }


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


def rank_from_scores(scores: dict[str, float], move: str) -> int | None:
    if move not in scores:
        return None
    score = scores[move]
    return 1 + sum(1 for other_score in scores.values() if other_score > score)


def sign(value: float | int | None) -> int:
    if value is None:
        return 0
    if value > 0:
        return 1
    if value < 0:
        return -1
    return 0


def listwise_scores(
    config: TrainerConfig,
    weights: WeightsByPhase,
    example: ListwiseExample,
) -> dict[str, float]:
    return {
        candidate.move: candidate_score(config, weights, candidate)
        for candidate in example.candidates
    }


def compact_listwise_scores(
    config: TrainerConfig,
    weights: WeightsByPhase,
    dataset: CompactListwiseDataset,
    example_index: int,
) -> dict[str, float]:
    start = dataset.example_offsets[example_index]
    end = dataset.example_offsets[example_index + 1]
    return {
        dataset.candidate_moves[candidate_index]: compact_candidate_score(
            config,
            weights,
            dataset,
            candidate_index,
        )
        for candidate_index in range(start, end)
    }


def evaluate_listwise_examples(
    config: TrainerConfig,
    weights: WeightsByPhase,
    examples: ListwiseData,
) -> dict[str, Any]:
    dataset = ensure_compact_listwise_dataset(examples)
    if dataset.example_count == 0:
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
    losses: list[float] = []
    weighted_losses: list[float] = []
    margins: list[float] = []
    correct = 0
    weighted_correct = 0.0
    total_weight = 0.0
    for example_index in range(dataset.example_count):
        candidate_start = dataset.example_offsets[example_index]
        candidate_end = dataset.example_offsets[example_index + 1]
        scores = [
            compact_candidate_score(config, weights, dataset, candidate_index)
            for candidate_index in range(candidate_start, candidate_end)
        ]
        probabilities = softmax_probabilities(scores)
        target_indexes = [
            index - candidate_start
            for index in range(candidate_start, candidate_end)
            if dataset.candidate_target_mask[index]
        ]
        loss = 0.0
        for local_index, candidate_index in enumerate(range(candidate_start, candidate_end)):
            target_probability = dataset.candidate_target_probabilities[candidate_index]
            if target_probability > 0.0:
                loss -= target_probability * math.log(max(probabilities[local_index], 1.0e-300))
        losses.append(loss)
        example_weight = dataset.example_weights[example_index]
        weighted_losses.append(loss * example_weight)
        total_weight += example_weight
        best_score = max(scores)
        best_indexes = {
            candidate_start + local_index
            for local_index, score_value in enumerate(scores)
            if score_value == best_score
        }
        if any(dataset.candidate_target_mask[index] for index in best_indexes):
            correct += 1
            weighted_correct += example_weight
        target_scores = [
            score_value
            for candidate_index, score_value in zip(range(candidate_start, candidate_end), scores, strict=True)
            if dataset.candidate_target_mask[candidate_index]
        ]
        non_target_scores = [
            score_value
            for candidate_index, score_value in zip(range(candidate_start, candidate_end), scores, strict=True)
            if not dataset.candidate_target_mask[candidate_index]
        ]
        if target_scores and non_target_scores:
            margins.append(max(target_scores) - max(non_target_scores))
    l2_term = 0.0
    for phase_weights in weights.values():
        l2_term += sum(value * value for value in phase_weights.values())
    weighted_loss = sum(weighted_losses) / total_weight if total_weight else 0.0
    return {
        "pairs": dataset.example_count,
        "loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "weighted_loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "unweighted_loss": sum(losses) / len(losses) + 0.5 * config.l2 * l2_term,
        "accuracy": correct / dataset.example_count,
        "weighted_accuracy": weighted_correct / total_weight if total_weight else 0.0,
        "avg_margin": sum(margins) / len(margins) if margins else 0.0,
        "total_pair_weight": total_weight,
        "avg_pair_weight": total_weight / dataset.example_count,
    }


def move_choice_metrics(
    config: TrainerConfig,
    weights: WeightsByPhase,
    examples: ListwiseData,
) -> dict[str, Any]:
    dataset = ensure_compact_listwise_dataset(examples)
    if dataset.example_count == 0:
        return {
            "rows": 0,
            "selected_teacher_agreement": 0,
            "selected_teacher_agreement_rate": 0.0,
            "avg_teacher_rank": None,
            "top_tie_rate": 0.0,
            "exact_best_top_group": 0,
            "exact_best_top_group_rate": None,
            "avg_exact_best_rank": None,
            "exact_sign_agreement": 0,
            "exact_sign_agreement_rate": None,
            "wrong_direction": 0,
            "wrong_direction_by_phase": {phase: 0 for phase in PHASES},
            "high_confidence_wrong_direction": 0,
            "soft_target_cross_entropy": None,
            "selected_exact_score_regret": None,
            "top_move_exact_score_gap": None,
            "sign_anchor_rows": 0,
            "sign_anchor_updates": 0,
            "sign_anchor_loss": 0.0,
        }
    selected_teacher = 0
    teacher_rank_sum = 0
    teacher_rank_rows = 0
    top_ties = 0
    exact_rows = 0
    exact_best_top_group = 0
    exact_best_rank_sum = 0
    exact_best_rank_rows = 0
    sign_rows = 0
    sign_agree = 0
    wrong_direction = 0
    wrong_direction_by_phase = collections.Counter({phase: 0 for phase in PHASES})
    high_conf_wrong = 0
    soft_target_ce_sum = 0.0
    soft_target_rows = 0
    exact_score_regret_sum = 0.0
    exact_score_gap_sum = 0.0
    selected_exact_score_rows = 0
    sign_anchor_rows = 0
    sign_anchor_updates = 0
    sign_anchor_loss_sum = 0.0
    for example_index in range(dataset.example_count):
        scores = compact_listwise_scores(config, weights, dataset, example_index)
        if not scores:
            continue
        best_score = max(scores.values())
        selected_move = min(move for move, score_value in scores.items() if score_value == best_score)
        if selected_move == dataset.example_teacher_moves[example_index]:
            selected_teacher += 1
        teacher_rank = rank_from_scores(scores, dataset.example_teacher_moves[example_index])
        if teacher_rank is not None:
            teacher_rank_sum += teacher_rank
            teacher_rank_rows += 1
        top_group = {
            move
            for move, score_value in scores.items()
            if best_score - score_value <= config.top_group_margin
        }
        if len(top_group) > config.target_top_group_size:
            top_ties += 1
        candidate_start = dataset.example_offsets[example_index]
        candidate_end = dataset.example_offsets[example_index + 1]
        candidate_indexes = range(candidate_start, candidate_end)
        local_scores = [
            compact_candidate_score(config, weights, dataset, candidate_index)
            for candidate_index in candidate_indexes
        ]
        probabilities = softmax_probabilities(local_scores)
        if sign_anchor_enabled(config):
            if config.soft_sign_anchor_mode == "selected":
                anchor_index = selected_anchor_candidate_index(
                    dataset,
                    candidate_start,
                    candidate_end,
                    local_scores,
                )
                anchor_score = local_scores[anchor_index - candidate_start]
                anchor_loss = sign_anchor_loss_value(
                    config,
                    dataset.example_exact_root_scores[example_index],
                    anchor_score,
                )
                if anchor_loss > 0.0:
                    sign_anchor_rows += 1
                    sign_anchor_updates += 1
                    sign_anchor_loss_sum += anchor_loss
            elif config.soft_sign_anchor_mode == "expected":
                anchor_score = expected_anchor_score(local_scores, probabilities)
                anchor_loss = sign_anchor_loss_value(
                    config,
                    dataset.example_exact_root_scores[example_index],
                    anchor_score,
                )
                if anchor_loss > 0.0:
                    sign_anchor_rows += 1
                    sign_anchor_updates += candidate_end - candidate_start
                    sign_anchor_loss_sum += anchor_loss
        has_soft_target = any(
            dataset.candidate_target_probabilities[candidate_index] > 0.0
            and not dataset.candidate_target_mask[candidate_index]
            for candidate_index in range(candidate_start, candidate_end)
        )
        if has_soft_target:
            row_ce = 0.0
            for local_index, candidate_index in enumerate(range(candidate_start, candidate_end)):
                target_probability = dataset.candidate_target_probabilities[candidate_index]
                if target_probability > 0.0:
                    row_ce -= target_probability * math.log(max(probabilities[local_index], 1.0e-300))
            soft_target_ce_sum += row_ce
            soft_target_rows += 1
        exact_best = {
            dataset.candidate_moves[candidate_index]
            for candidate_index in range(candidate_start, candidate_end)
            if dataset.candidate_exact_best_mask[candidate_index]
        }
        if exact_best:
            exact_rows += 1
            if top_group & exact_best:
                exact_best_top_group += 1
            ranks = [rank_from_scores(scores, move) for move in exact_best if rank_from_scores(scores, move) is not None]
            if ranks:
                exact_best_rank_sum += min(ranks)
                exact_best_rank_rows += 1
        scored_candidates = [
            candidate_index
            for candidate_index in range(candidate_start, candidate_end)
            if dataset.candidate_exact_scores[candidate_index] is not None
        ]
        selected_index = next(
            (
                candidate_index
                for candidate_index in range(candidate_start, candidate_end)
                if dataset.candidate_moves[candidate_index] == selected_move
            ),
            None,
        )
        if scored_candidates and selected_index is not None:
            selected_exact_score = dataset.candidate_exact_scores[selected_index]
            assert selected_exact_score is not None
            best_exact_score = max(
                dataset.candidate_exact_scores[candidate_index]
                for candidate_index in scored_candidates
                if dataset.candidate_exact_scores[candidate_index] is not None
            )
            regret = best_exact_score - selected_exact_score
            exact_score_regret_sum += regret
            exact_score_gap_sum += regret
            selected_exact_score_rows += 1
        exact_sign = sign(dataset.example_exact_root_scores[example_index])
        model_sign = sign(best_score)
        if exact_sign != 0 and model_sign != 0:
            sign_rows += 1
            if exact_sign == model_sign:
                sign_agree += 1
            else:
                wrong_direction += 1
                phase = ID_TO_PHASE[dataset.candidate_phase_ids[candidate_start]]
                wrong_direction_by_phase[phase] += 1
                if abs(best_score) >= config.high_confidence_margin:
                    high_conf_wrong += 1
    return {
        "rows": dataset.example_count,
        "selected_teacher_agreement": selected_teacher,
        "selected_teacher_agreement_rate": selected_teacher / dataset.example_count,
        "avg_teacher_rank": teacher_rank_sum / teacher_rank_rows if teacher_rank_rows else None,
        "top_tie_rate": top_ties / dataset.example_count,
        "exact_best_top_group": exact_best_top_group,
        "exact_best_top_group_rate": exact_best_top_group / exact_rows if exact_rows else None,
        "avg_exact_best_rank": exact_best_rank_sum / exact_best_rank_rows if exact_best_rank_rows else None,
        "exact_sign_rows": sign_rows,
        "exact_sign_agreement": sign_agree,
        "exact_sign_agreement_rate": sign_agree / sign_rows if sign_rows else None,
        "wrong_direction": wrong_direction,
        "wrong_direction_by_phase": {phase: wrong_direction_by_phase[phase] for phase in PHASES},
        "high_confidence_wrong_direction": high_conf_wrong,
        "soft_target_cross_entropy": soft_target_ce_sum / soft_target_rows if soft_target_rows else None,
        "soft_target_rows": soft_target_rows,
        "selected_exact_score_regret": (
            exact_score_regret_sum / selected_exact_score_rows if selected_exact_score_rows else None
        ),
        "top_move_exact_score_gap": (
            exact_score_gap_sum / selected_exact_score_rows if selected_exact_score_rows else None
        ),
        "selected_exact_score_rows": selected_exact_score_rows,
        "sign_anchor_rows": sign_anchor_rows,
        "sign_anchor_updates": sign_anchor_updates,
        "sign_anchor_loss": sign_anchor_loss_sum / sign_anchor_rows if sign_anchor_rows else 0.0,
    }


def quantized_dequantized_weights(
    weights: WeightsByPhase,
    *,
    output_scale: float,
    max_abs_output_weight: int,
) -> WeightsByPhase:
    clamp = min(max_abs_output_weight, INT16_LIMIT)
    calibrated: WeightsByPhase = {phase: {} for phase in PHASES}
    for phase, phase_weights in weights.items():
        for key, value in phase_weights.items():
            integer = max(-clamp, min(clamp, int(round(value * output_scale))))
            if integer != 0:
                calibrated[phase][key] = integer / output_scale
    return calibrated


def calibrate_output_scale(
    config: TrainerConfig,
    weights: WeightsByPhase,
    examples: ListwiseData,
) -> tuple[TrainerConfig, dict[str, Any]]:
    if not config.calibrate_output_scale:
        return config, {}
    dataset = ensure_compact_listwise_dataset(examples)
    validation_indexes = [
        index
        for index in range(dataset.example_count)
        if dataset.example_splits[index] == "validation"
        and any(
            dataset.candidate_exact_best_mask[candidate_index]
            for candidate_index in range(dataset.example_offsets[index], dataset.example_offsets[index + 1])
        )
    ]
    if not validation_indexes:
        validation_indexes = [
            index
            for index in range(dataset.example_count)
            if any(
                dataset.candidate_exact_best_mask[candidate_index]
                for candidate_index in range(dataset.example_offsets[index], dataset.example_offsets[index + 1])
            )
        ]
    if not validation_indexes:
        return config, {"status": "skipped", "reason": "no exact-overlap examples"}
    calibration_examples = compact_listwise_dataset_subset(dataset, validation_indexes)
    best_scale = config.output_scale
    best_metrics: dict[str, Any] | None = None
    best_key: tuple[float, float, float, float] | None = None
    candidates: list[dict[str, Any]] = []
    for scale in config.scale_grid:
        quantized = quantized_dequantized_weights(
            weights,
            output_scale=scale,
            max_abs_output_weight=config.max_abs_output_weight,
        )
        candidate_config = replace(config, output_scale=scale)
        metrics = move_choice_metrics(candidate_config, quantized, calibration_examples)
        key = (
            float(metrics["selected_teacher_agreement_rate"]),
            float(metrics["exact_sign_agreement_rate"] or 0.0),
            -float(metrics["top_tie_rate"]),
            -float(metrics["avg_exact_best_rank"] or 1.0e9),
        )
        candidates.append({"scale": scale, "metrics": metrics, "score_key": key})
        if best_key is None or key > best_key:
            best_key = key
            best_scale = scale
            best_metrics = metrics
    return replace(config, output_scale=best_scale), {
        "status": "selected",
        "selected_scale": best_scale,
        "selected_metrics": best_metrics,
        "candidates": candidates,
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
        f"# objective: {config.objective}",
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


def table_violation_summary_json(summary: TableViolationSummary) -> dict[str, Any]:
    return {
        "label": summary.label,
        "checked_pairs": summary.checked_pairs,
        "violations": summary.violations,
        "max_abs_delta": summary.max_abs_delta,
        "examples": [
            {
                "family": example[0],
                "index": example[1],
                "other_family": example[2],
                "other_index": example[3],
                "weight": example[4],
                "other_weight": example[5],
            }
            for example in summary.examples
        ],
    }


def post_training_symmetrize_summary_json(summary: SymmetrizeSummary) -> dict[str, Any]:
    return {
        "modes": list(summary.modes),
        "output_path": str(summary.output_path),
        "families_processed": list(summary.families_processed),
        "entries_read": summary.entries_read,
        "entries_written": summary.entries_written,
        "changed_entries": summary.changed_entries,
        "zero_entries_introduced": summary.zero_entries_introduced,
        "zero_entries_removed": summary.zero_entries_removed,
        "max_abs_delta_before": summary.max_abs_delta_before,
        "max_abs_delta_after": max(summary.max_abs_delta_after_by_check.values(), default=0),
        "violations_before": summary.violations_before,
        "violations_after": summary.violations_after,
        "max_abs_delta_before_by_check": summary.max_abs_delta_before_by_check,
        "max_abs_delta_after_by_check": summary.max_abs_delta_after_by_check,
    }


def ensure_generated_phase_table_output(path: Path, *, config: TrainerConfig) -> None:
    reject_source_data_dir(path, option_name="post-training symmetrize output")
    resolved_path = path.resolve(strict=False)
    resolved_out_dir = config.out_dir.resolve(strict=False)
    try:
        resolved_path.relative_to(resolved_out_dir)
    except ValueError as exc:
        raise ScriptError("post-training symmetrize output must stay under --out-dir") from exc


def pattern_table_has_data_entry(text: str) -> bool:
    return any(line.strip() and not line.lstrip().startswith("#") for line in text.splitlines())


def ensure_nonempty_generated_pattern_table(path: Path) -> bool:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to read generated pattern table {path}: {exc}") from exc
    if pattern_table_has_data_entry(text):
        return False
    sentinel_line = f"{SENTINEL_FAMILY}\t{SENTINEL_ENTRY[0]}\t{SENTINEL_ENTRY[1]}"
    try:
        path.write_text(text.rstrip() + "\n\n" + sentinel_line + "\n", encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to write generated pattern table {path}: {exc}") from exc
    return True


def apply_post_training_symmetrize(
    config: TrainerConfig,
    table_paths: dict[str, Path],
) -> dict[str, Any]:
    modes = config.post_training_symmetrize_modes
    summary: dict[str, Any] = {
        "modes": list(modes),
        "enabled": bool(modes),
        "phases": {},
    }
    if not modes:
        return summary

    for phase in PHASES:
        path = table_paths[phase]
        ensure_generated_phase_table_output(path, config=config)
        _, phase_summary, before, after = symmetrize_pattern_table(
            input_path=path,
            output_path=path,
            modes=modes,
        )
        sentinel_entry_written = ensure_nonempty_generated_pattern_table(path)
        phase_json = post_training_symmetrize_summary_json(phase_summary)
        if sentinel_entry_written:
            phase_json["entries_written"] += 1
            phase_json["zero_entries_introduced"] += 1
        phase_json["sentinel_entry_written"] = sentinel_entry_written
        summary["phases"][phase] = {
            **phase_json,
            "before": [table_violation_summary_json(row) for row in before],
            "after": [table_violation_summary_json(row) for row in after],
        }
    return summary


def render_candidate_eval(config: TrainerConfig) -> str:
    return "\n".join(
        [
            "# schema_version: eval.v1",
            "# generated_by: tools/scripts/regularized_pairwise_pattern_train.py",
            "# no_strength_claim: true",
            "# not_default_promotion: true",
            "# trainer_foundation: true",
            f"# model: {TRAINER_MODEL}",
            f"# candidate_pattern_table_weight: {config.candidate_pattern_table_weight}",
            "schema_version=eval.v1",
            "mode=pattern_only",
            "name=regularized_pairwise_pattern_candidate",
            "pattern_table.opening=tables/opening.tsv",
            "pattern_table.midgame=tables/midgame.tsv",
            "pattern_table.late=tables/late.tsv",
            f"opening.pattern_table={config.candidate_pattern_table_weight}",
            f"midgame.pattern_table={config.candidate_pattern_table_weight}",
            f"late.pattern_table={config.candidate_pattern_table_weight}",
            f"opening_max_occupied={config.phase_cutoffs.opening_max_occupied}",
            f"midgame_max_occupied={config.phase_cutoffs.midgame_max_occupied}",
        ]
    ) + "\n"


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
        f"- analysis_runner: `{config.analysis_runner}`",
        f"- analysis_cache_mode: `{summary['analysis_cache_mode']}`",
        f"- analysis_cache_dir: `{summary['analysis_cache_dir'] or 'none'}`",
        f"- listwise_feature_cache_mode: `{summary.get('listwise_feature_cache_mode', 'off')}`",
        f"- listwise_feature_cache_dir: `{summary.get('listwise_feature_cache_dir') or 'none'}`",
        f"- analysis_jobs: `{summary['analysis_jobs']}`",
        f"- pair_cache_mode: `{summary.get('pair_cache', {}).get('mode', 'off')}`",
        f"- pair_cache_status: `{summary.get('pair_cache', {}).get('status', 'off')}`",
        f"- checkpoint_dir: `{summary.get('checkpoint', {}).get('dir') or 'none'}`",
        f"- resumed_from_epoch: `{summary.get('checkpoint', {}).get('resumed_from_epoch', 0)}`",
        "",
        "## Training",
        "",
        f"- split: `{config.split}`",
        f"- families: `{', '.join(config.families)}`",
        f"- objective: `{config.objective}`",
        f"- loss: `{config.loss}`",
        f"- pair_mode: `{config.pair_mode}`",
        f"- pair_weighting: `{config.pair_weighting}`",
        f"- bucket_weights: `{config.bucket_weights_path or 'none'}`",
        f"- bucket_field: `{config.bucket_field}`",
        f"- default_bucket_weight: `{config.default_bucket_weight}`",
        f"- guard_mode: `{config.guard_mode}`",
        f"- guard_weight: `{config.guard_weight}`",
        f"- guard_max_pairs_per_position: `{config.guard_max_pairs_per_position}`",
        f"- max_pairs_per_position: `{config.max_pairs_per_position}`",
        f"- exact_best_weight: `{config.exact_best_weight}`",
        f"- teacher_weight: `{config.teacher_weight}`",
        f"- min_score_margin: `{config.min_score_margin}`",
        f"- drop_teacher_exact_disagreement: `{config.drop_teacher_exact_disagreement}`",
        f"- exact_aware_only_when_available: `{config.exact_aware_only_when_available}`",
        f"- exact_score_soft_target: `{config.exact_score_soft_target}`",
        f"- exact_score_temperature: `{config.exact_score_temperature}`",
        f"- exact_score_target_floor: `{config.exact_score_target_floor}`",
        f"- exact_score_near_best_window: `{config.exact_score_near_best_window}`",
        f"- soft_sign_anchor_weight: `{config.soft_sign_anchor_weight}`",
        f"- soft_sign_anchor_margin: `{config.soft_sign_anchor_margin}`",
        f"- soft_sign_anchor_mode: `{config.soft_sign_anchor_mode}`",
        f"- post_training_symmetrize: `{','.join(config.post_training_symmetrize_modes) or 'off'}`",
        f"- max_top_group_size_for_training: `{config.max_top_group_size_for_training}`",
        f"- tie_penalty: `{config.tie_penalty}`",
        f"- target_top_group_size: `{config.target_top_group_size}`",
        f"- top_group_margin: `{config.top_group_margin}`",
        f"- sign_penalty: `{config.sign_penalty}`",
        f"- high_confidence_margin: `{config.high_confidence_margin}`",
        f"- l2: `{config.l2}`",
        f"- epochs: `{config.epochs}`",
        f"- learning_rate: `{config.learning_rate}`",
        f"- max_abs_weight: `{config.max_abs_weight}`",
        f"- output_scale: `{config.output_scale}`",
        f"- calibrate_output_scale: `{config.calibrate_output_scale}`",
        f"- scale_grid: `{', '.join(format_weight(scale) for scale in config.scale_grid)}`",
        f"- max_abs_output_weight: `{config.max_abs_output_weight}`",
        f"- candidate_pattern_table_weight: `{config.candidate_pattern_table_weight}`",
        f"- model: `{TRAINER_MODEL}`",
        f"- seed: `{config.seed}`",
        f"- phase_cutoffs: opening <= `{config.phase_cutoffs.opening_max_occupied}`, "
        f"midgame <= `{config.phase_cutoffs.midgame_max_occupied}`, else late",
        "",
        "## Counts",
        "",
    ]
    for key in sorted(rows):
        if key in ("bucket_pair_counts", "bucket_pair_weight_mass"):
            continue
        lines.append(f"- {key}: `{rows[key]}`")
    bucket_counts = rows.get("bucket_pair_counts", {})
    bucket_mass = rows.get("bucket_pair_weight_mass", {})
    if bucket_counts or bucket_mass:
        lines.extend(["", "## Bucket Pair Weights", ""])
        for bucket in sorted(set(bucket_counts) | set(bucket_mass)):
            lines.append(
                f"- {bucket}: count=`{bucket_counts.get(bucket, 0)}`, "
                f"weighted_mass=`{float(bucket_mass.get(bucket, 0.0)):.6f}`"
            )
    timing = summary.get("timing", {})
    if timing:
        lines.extend(
            [
                "",
                "## Timing",
                "",
                f"- label_load_seconds: `{float(timing.get('label_load_seconds', 0.0)):.6f}`",
                f"- exact_load_seconds: `{float(timing.get('exact_load_seconds', 0.0)):.6f}`",
                f"- analysis_seconds: `{float(timing.get('analysis_seconds', 0.0)):.6f}`",
                f"- feature_construction_seconds: `{float(timing.get('feature_construction_seconds', 0.0)):.6f}`",
                f"- pair_generation_seconds: `{float(timing.get('pair_generation_seconds', 0.0)):.6f}`",
                f"- listwise_training_seconds: `{float(timing.get('listwise_training_seconds', 0.0)):.6f}`",
                f"- pairwise_training_seconds: `{float(timing.get('pairwise_training_seconds', 0.0)):.6f}`",
                f"- sgd_seconds: `{float(timing.get('sgd_seconds', 0.0)):.6f}`",
                f"- examples_per_second: `{float(timing.get('examples_per_second', 0.0)):.6f}`",
                f"- updates_per_second: `{float(timing.get('updates_per_second', 0.0)):.6f}`",
                f"- feature_cache_hit_rate: `{float(timing.get('feature_cache_hit_rate', 0.0)):.6f}`",
                f"- peak_pair_count: `{int(timing.get('peak_pair_count', 0))}`",
                f"- memory_estimate_bytes: `{int(timing.get('memory_estimate_bytes', 0))}`",
                f"- total_seconds: `{float(timing.get('total_seconds', 0.0)):.6f}`",
            ]
        )
    compact = summary.get("training", {}).get("listwise_compact", {})
    if compact:
        lines.extend(
            [
                "",
                "## Compact Listwise",
                "",
                f"- examples: `{compact.get('examples', 0)}`",
                f"- candidates: `{compact.get('candidates', 0)}`",
                f"- feature_entries: `{compact.get('feature_entries', 0)}`",
                f"- unique_features: `{compact.get('unique_features', 0)}`",
            ]
        )
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
            "## Move Choice Diagnostics",
            "",
        ]
    )
    diagnostics = summary.get("diagnostics", {})
    for label in ("initial", "final", "quantized_final"):
        row = diagnostics.get(label)
        if not row:
            continue
        lines.extend(
            [
                f"### {label}",
                "",
                f"- selected_teacher_agreement: `{row['selected_teacher_agreement']} / {row['rows']}`",
                f"- selected_teacher_agreement_rate: `{row['selected_teacher_agreement_rate']:.6f}`",
                f"- avg_teacher_rank: `{row['avg_teacher_rank'] if row['avg_teacher_rank'] is not None else 'n/a'}`",
                f"- top_tie_rate: `{row['top_tie_rate']:.6f}`",
                f"- exact_best_top_group: `{row['exact_best_top_group']}`",
                f"- exact_best_top_group_rate: `{row['exact_best_top_group_rate'] if row['exact_best_top_group_rate'] is not None else 'n/a'}`",
                f"- avg_exact_best_rank: `{row['avg_exact_best_rank'] if row['avg_exact_best_rank'] is not None else 'n/a'}`",
                f"- exact_sign_agreement: `{row.get('exact_sign_agreement', 0)} / {row.get('exact_sign_rows', 0)}`",
                f"- exact_sign_agreement_rate: `{row['exact_sign_agreement_rate'] if row['exact_sign_agreement_rate'] is not None else 'n/a'}`",
                f"- wrong_direction: `{row['wrong_direction']}`",
                f"- wrong_direction_by_phase: `{json.dumps(row.get('wrong_direction_by_phase', {}), sort_keys=True)}`",
                f"- high_confidence_wrong_direction: `{row['high_confidence_wrong_direction']}`",
                f"- soft_target_cross_entropy: `{row.get('soft_target_cross_entropy') if row.get('soft_target_cross_entropy') is not None else 'n/a'}`",
                f"- selected_exact_score_regret: `{row.get('selected_exact_score_regret') if row.get('selected_exact_score_regret') is not None else 'n/a'}`",
                f"- top_move_exact_score_gap: `{row.get('top_move_exact_score_gap') if row.get('top_move_exact_score_gap') is not None else 'n/a'}`",
                f"- sign_anchor_rows: `{row.get('sign_anchor_rows', 0)}`",
                f"- sign_anchor_updates: `{row.get('sign_anchor_updates', 0)}`",
                f"- sign_anchor_loss: `{row.get('sign_anchor_loss', 0.0)}`",
                "",
            ]
        )
    calibration = summary.get("output_scale_calibration", {})
    if calibration:
        lines.extend(["## Output Scale Calibration", ""])
        lines.append(f"- status: `{calibration.get('status', 'unknown')}`")
        if calibration.get("selected_scale") is not None:
            lines.append(f"- selected_scale: `{calibration['selected_scale']}`")
        if calibration.get("reason"):
            lines.append(f"- reason: `{calibration['reason']}`")
        lines.append("")
    post_sym = summary.get("post_training_symmetrize", {})
    lines.extend(
        [
            "## Post-Training Symmetrize",
            "",
            f"- enabled: `{bool(post_sym.get('enabled', False))}`",
            f"- modes: `{','.join(post_sym.get('modes', [])) or 'off'}`",
        ]
    )
    phases = post_sym.get("phases", {})
    if phases:
        lines.extend(
            [
                "",
                "| phase | entries read | entries written | changed entries | zero introduced | zero removed | violations before | violations after | max abs delta before | max abs delta after |",
                "| --- | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | --- |",
            ]
        )
        for phase in PHASES:
            row = phases.get(phase)
            if not row:
                continue
            lines.append(
                "| "
                + " | ".join(
                    [
                        phase,
                        str(row.get("entries_read", 0)),
                        str(row.get("entries_written", 0)),
                        str(row.get("changed_entries", 0)),
                        str(row.get("zero_entries_introduced", 0)),
                        str(row.get("zero_entries_removed", 0)),
                        json.dumps(row.get("violations_before", {}), sort_keys=True),
                        json.dumps(row.get("violations_after", {}), sort_keys=True),
                        json.dumps(row.get("max_abs_delta_before_by_check", {}), sort_keys=True),
                        json.dumps(row.get("max_abs_delta_after_by_check", {}), sort_keys=True),
                    ]
                )
                + " |"
            )
    lines.append("")
    lines.extend(
        [
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
            "- The trainer learns pattern-only delta scores from the selected teacher-vs-engine pairs; "
            "base scalar weights are not part of the trainer objective or generated candidate.",
            "- Follow-up validation must use held-out labels, search/match checks, and external sanity before any strength claim.",
        ]
    )
    return "\n".join(lines) + "\n"


def _format_distribution(distribution: dict[str, int]) -> str:
    if not distribution:
        return "none"
    return ", ".join(f"{key}:{value}" for key, value in distribution.items())


def _diagnostic_table_lines(title: str, groups: dict[str, Any], *, limit: int = 40) -> list[str]:
    lines = [
        f"## {title}",
        "",
        "| group | teacher rows | accepted | illegal/skipped | exact unavailable | teacher/exact disagreement | pairs | pair weight mass | legal moves | pair counts | score margins | rank margins | teacher ranks | exact top group |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |",
    ]
    for index, (name, group) in enumerate(groups.items()):
        if index >= limit:
            lines.append(
                f"| ... | {len(groups) - limit} more groups omitted from Markdown; see summary.json |  |  |  |  |  |  |  |  |  |  |  | |"
            )
            break
        pair_counts = group.get("pair_count_distribution", {})
        pair_total = sum(int(value) * int(key) for key, value in pair_counts.items() if str(key).isdigit())
        lines.append(
            "| "
            + " | ".join(
                [
                    str(name),
                    str(group.get("teacher_rows", 0)),
                    str(group.get("accepted_rows", 0)),
                    str(group.get("illegal_skipped_rows", 0)),
                    str(group.get("exact_unavailable", 0)),
                    str(group.get("teacher_exact_disagreement", 0)),
                    str(pair_total),
                    f"{float(group.get('pair_weight_mass', 0.0)):.6f}",
                    _format_distribution(group.get("legal_move_count_distribution", {})),
                    _format_distribution(pair_counts),
                    _format_distribution(group.get("score_margin_distribution", {})),
                    _format_distribution(group.get("rank_margin_distribution", {})),
                    _format_distribution(group.get("teacher_rank_distribution", {})),
                    _format_distribution(group.get("exact_best_top_group_size_distribution", {})),
                ]
            )
            + " |"
        )
    lines.append("")
    return lines


def render_dataset_diagnostic_report(config: TrainerConfig, summary: dict[str, Any]) -> str:
    rows = summary["rows"]
    diagnostics = rows.get("dataset_diagnostics", {})
    lines = [
        "# NTest Teacher Dataset Diagnostic",
        "",
        "This report performs teacher/exact/pair-generation diagnostics only. It does not train weights, "
        "write a candidate `.eval`, or change `current_default.eval`.",
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
        f"- analysis_runner: `{config.analysis_runner}`",
        f"- analysis_jobs: `{summary['analysis_jobs']}`",
        f"- analysis_cache_mode: `{summary['analysis_cache_mode']}`",
        f"- split: `{config.split}`",
        f"- pair_mode: `{config.pair_mode}`",
        f"- pair_weighting: `{config.pair_weighting}`",
        f"- min_score_margin: `{config.min_score_margin}`",
        f"- drop_teacher_exact_disagreement: `{config.drop_teacher_exact_disagreement}`",
        f"- exact_aware_only_when_available: `{config.exact_aware_only_when_available}`",
        f"- max_top_group_size_for_training: `{config.max_top_group_size_for_training}`",
        f"- max_pairs_per_position: `{config.max_pairs_per_position}`",
        "",
        "## Overall Counts",
        "",
    ]
    for key in sorted(rows):
        if key in ("bucket_pair_counts", "bucket_pair_weight_mass", "dataset_diagnostics"):
            continue
        lines.append(f"- {key}: `{rows[key]}`")
    if rows.get("bucket_pair_counts") or rows.get("bucket_pair_weight_mass"):
        lines.extend(["", "## Bucket Pair Weight Mass", ""])
        counts = rows.get("bucket_pair_counts", {})
        mass = rows.get("bucket_pair_weight_mass", {})
        for bucket in sorted(set(counts) | set(mass)):
            lines.append(
                f"- {bucket}: count=`{counts.get(bucket, 0)}`, "
                f"weighted_mass=`{float(mass.get(bucket, 0.0)):.6f}`"
            )
    if diagnostics:
        lines.extend([""])
        lines.extend(_diagnostic_table_lines("By Split", diagnostics.get("by_split", {})))
        lines.extend(_diagnostic_table_lines("By Phase", diagnostics.get("by_phase", {})))
        lines.extend(_diagnostic_table_lines("By Source Bucket", diagnostics.get("by_source_bucket", {})))
    lines.extend(
        [
            "## Full 300K Gate",
            "",
            "Proceed to full 300K exact-aware training only after 1K, 10K, and 50K diagnostics show:",
            "",
            "- accepted rows are high and illegal/skipped rows are explainable from known teacher failures",
            "- teacher/exact disagreement is visible and either low enough to keep or explicitly dropped",
            "- exact unavailable rows are expected for non-endgame positions, and exact-aware fallback is intentionally chosen or disabled",
            "- no split, phase, or source_bucket owns a surprising share of pair weight mass",
            "- weak score-margin and rank-margin pair mass is controlled by `--min-score-margin` or pair caps",
            "- exact-best top groups are not so broad that they turn exact-aware pairs into tie noise",
            "",
            "Safe preset for the next training pass:",
            "",
            "```sh",
            "python3 tools/scripts/regularized_pairwise_pattern_train.py \\",
            "  --pair-mode exact-aware \\",
            "  --pair-weighting exact-boost \\",
            "  --drop-teacher-exact-disagreement \\",
            "  --exact-aware-only-when-available \\",
            "  --min-score-margin 4 \\",
            "  --max-top-group-size-for-training 4 \\",
            "  --max-pairs-per-position 8",
            "```",
            "",
        ]
    )
    return "\n".join(lines)


def diagnose_dataset(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> DiagnosticResult:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    pairs, validation_records, row_stats = collect_pairs(config, analyzer=analyzer)
    validation_path = config.out_dir / "diagnostic_validation.tsv"
    write_validation_tsv(validation_path, validation_records)
    summary = {
        "script": "tools/scripts/regularized_pairwise_pattern_train.py",
        "mode": "diagnose_dataset",
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
        "analysis_runner": config.analysis_runner,
        "analysis_cache_mode": config.analysis_cache.mode,
        "analysis_cache_dir": (
            str(config.analysis_cache.directory) if config.analysis_cache.directory is not None else ""
        ),
        "analysis_jobs": config.analysis_jobs,
        "families": list(config.families),
        "split": config.split,
        "pair_mode": config.pair_mode,
        "pair_weighting": config.pair_weighting,
        "bucket_weighting": {
            "path": str(config.bucket_weights_path) if config.bucket_weights_path is not None else "",
            "field": config.bucket_field,
            "default_weight": config.default_bucket_weight,
            "weights": config.bucket_weights,
        },
        "max_pairs_per_position": config.max_pairs_per_position,
        "exact_best_weight": config.exact_best_weight,
        "teacher_weight": config.teacher_weight,
        "min_score_margin": config.min_score_margin,
        "drop_teacher_exact_disagreement": config.drop_teacher_exact_disagreement,
        "exact_aware_only_when_available": config.exact_aware_only_when_available,
        "exact_score_soft_target": config.exact_score_soft_target,
        "exact_score_temperature": config.exact_score_temperature,
        "exact_score_target_floor": config.exact_score_target_floor,
        "exact_score_near_best_window": config.exact_score_near_best_window,
        "soft_sign_anchor_weight": config.soft_sign_anchor_weight,
        "soft_sign_anchor_margin": config.soft_sign_anchor_margin,
        "soft_sign_anchor_mode": config.soft_sign_anchor_mode,
        "max_top_group_size_for_training": config.max_top_group_size_for_training,
        "seed": config.seed,
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
        "rows": row_stats,
        "pair_metrics": evaluate_pairs(config, {phase: {} for phase in PHASES}, pairs),
        "outputs": {
            "summary_json": str(config.out_dir / "summary.json"),
            "report_md": str(config.out_dir / "dataset_diagnostic.md"),
            "validation_tsv": str(validation_path),
        },
        "no_strength_claim": True,
        "training_performed": False,
        "default_promotion": False,
    }
    summary_path = config.out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    report_path = config.out_dir / "dataset_diagnostic.md"
    report_path.write_text(render_dataset_diagnostic_report(config, summary), encoding="utf-8")
    return DiagnosticResult(summary=summary, report_path=report_path)


def hash_file_group(paths: tuple[Path, ...]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(str(path).encode("utf-8"))
        digest.update(b"\0")
        digest.update(sha256_file(path).encode("ascii"))
        digest.update(b"\0")
    return digest.hexdigest()


def training_config_hash(config: TrainerConfig) -> str:
    pair_fingerprint, _ = pair_cache_key(config)
    material = {
        "schema": "regularized_pairwise_training_config.v1",
        "pair_cache_key": pair_fingerprint,
        "objective": config.objective,
        "loss": config.loss,
        "families": config.families,
        "split": config.split,
        "l2": config.l2,
        "learning_rate": config.learning_rate,
        "max_abs_weight": config.max_abs_weight,
        "exact_score_soft_target": config.exact_score_soft_target,
        "exact_score_temperature": config.exact_score_temperature,
        "exact_score_target_floor": config.exact_score_target_floor,
        "exact_score_near_best_window": config.exact_score_near_best_window,
        "soft_sign_anchor_weight": config.soft_sign_anchor_weight,
        "soft_sign_anchor_margin": config.soft_sign_anchor_margin,
        "soft_sign_anchor_mode": config.soft_sign_anchor_mode,
        "seed": config.seed,
    }
    return sha256_text(json.dumps(material, sort_keys=True, separators=(",", ":")))


def pair_cache_key(config: TrainerConfig) -> tuple[str, dict[str, Any]]:
    dataset_hash = hash_file_group(config.teacher_labels)
    exact_hash = hash_file_group(config.exact_labels)
    eval_hash = sha256_file(config.eval_config)
    analyzer_hash = analyze_position_hash(config.analyze_position) or "unavailable"
    material = {
        "schema": PAIR_CACHE_SCHEMA,
        "dataset_hash": dataset_hash,
        "eval_config_hash": eval_hash,
        "analyzer_hash": analyzer_hash,
        "analysis_depth": DEFAULT_ANALYSIS_DEPTH,
        "pair_mode": config.pair_mode,
        "pair_weighting": config.pair_weighting,
        "families": config.families,
        "split": config.split,
        "seed": config.seed,
        "exact_labels_hash": exact_hash,
        "max_pairs_per_position": config.max_pairs_per_position,
        "guard_mode": config.guard_mode,
        "guard_weight": config.guard_weight,
        "guard_max_pairs_per_position": config.guard_max_pairs_per_position,
        "bucket_weights_hash": (
            sha256_file(config.bucket_weights_path) if config.bucket_weights_path is not None else ""
        ),
        "default_bucket_weight": config.default_bucket_weight,
        "bucket_field": config.bucket_field,
        "exact_best_weight": config.exact_best_weight,
        "teacher_weight": config.teacher_weight,
        "min_score_margin": config.min_score_margin,
        "drop_teacher_exact_disagreement": config.drop_teacher_exact_disagreement,
        "exact_aware_only_when_available": config.exact_aware_only_when_available,
        "exact_score_soft_target": config.exact_score_soft_target,
        "exact_score_temperature": config.exact_score_temperature,
        "exact_score_target_floor": config.exact_score_target_floor,
        "exact_score_near_best_window": config.exact_score_near_best_window,
        "soft_sign_anchor_weight": config.soft_sign_anchor_weight,
        "soft_sign_anchor_margin": config.soft_sign_anchor_margin,
        "soft_sign_anchor_mode": config.soft_sign_anchor_mode,
        "max_top_group_size_for_training": config.max_top_group_size_for_training,
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
    }
    key = sha256_text(json.dumps(material, sort_keys=True, separators=(",", ":")))
    return key, material


def pair_cache_paths(config: TrainerConfig, key: str) -> tuple[Path, Path]:
    if config.pair_cache_dir is None:
        raise ScriptError("pair cache directory is not configured")
    return (
        config.pair_cache_dir / f"{key}.pkl",
        config.pair_cache_dir / f"{key}.json",
    )


def load_pair_cache(config: TrainerConfig, key: str) -> PairCachePayload | None:
    if config.pair_cache_dir is None or config.pair_cache_mode in {"off", "refresh"}:
        return None
    start = time.perf_counter()
    payload_path, _ = pair_cache_paths(config, key)
    if not payload_path.exists():
        if config.pair_cache_mode == "read-only":
            raise ScriptError(f"pair cache missing for key {key}")
        return None
    try:
        with payload_path.open("rb") as input_file:
            payload = pickle.load(input_file)
    except OSError as exc:
        raise ScriptError(f"failed to read pair cache {payload_path}: {exc}") from exc
    if not isinstance(payload, PairCachePayload):
        if config.pair_cache_mode == "read-only":
            raise ScriptError(f"invalid pair cache payload for key {key}")
        return None
    payload.row_stats["pair_cache_status"] = "hit"
    payload.row_stats["pair_cache_key"] = key
    payload.row_stats["timing"] = {
        "read_labels_seconds": 0.0,
        "preprocessing_seconds": 0.0,
        "analysis_seconds": 0.0,
        "pair_generation_seconds": 0.0,
        "pair_cache_load_seconds": round(time.perf_counter() - start, 6),
    }
    return payload


def write_pair_cache(
    config: TrainerConfig,
    *,
    key: str,
    material: dict[str, Any],
    payload: PairCachePayload,
) -> None:
    if config.pair_cache_dir is None or config.pair_cache_mode in {"off", "read-only"}:
        return
    payload_path, metadata_path = pair_cache_paths(config, key)
    payload_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with payload_path.open("wb") as output_file:
            pickle.dump(payload, output_file, protocol=pickle.HIGHEST_PROTOCOL)
        metadata = {
            "schema": PAIR_CACHE_SCHEMA,
            "key": key,
            "material": material,
            "pair_count": payload.compact_pairs.count,
            "feature_count": len(payload.compact_pairs.feature_keys),
            "nonzero_feature_values": len(payload.compact_pairs.feature_ids),
            "memory_bytes": payload.compact_pairs.memory_bytes,
            "generated_at": dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        }
        metadata_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to write pair cache {payload_path}: {exc}") from exc


def collect_pairwise_training_data_cached(
    config: TrainerConfig,
    analyzer: Analyzer,
) -> PairCachePayload:
    key, material = pair_cache_key(config)
    cached = load_pair_cache(config, key)
    if cached is not None:
        return cached
    feature_cache = FeatureCache(config.families, {})
    pairs, listwise_examples, validation_records, row_stats = collect_training_data(
        config,
        analyzer=analyzer,
        pair_feature_cache=feature_cache,
    )
    compact_pairs = CompactPreferencePairs.from_pairs(pairs)
    row_stats["pair_cache_status"] = "miss" if config.pair_cache_mode != "off" else "off"
    row_stats["pair_cache_key"] = key
    row_stats["compact_pairs"] = {
        "pairs": compact_pairs.count,
        "feature_count": len(compact_pairs.feature_keys),
        "nonzero_feature_values": len(compact_pairs.feature_ids),
        "memory_bytes": compact_pairs.memory_bytes,
    }
    payload = PairCachePayload(
        pairs=pairs,
        listwise_examples=listwise_examples,
        validation_records=validation_records,
        row_stats=row_stats,
        compact_pairs=compact_pairs,
    )
    write_pair_cache(config, key=key, material=material, payload=payload)
    return payload


def train_pairwise_tables(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> TrainingResult:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    table_dir = config.out_dir / "tables"
    table_dir.mkdir(parents=True, exist_ok=True)

    timing_start = time.perf_counter()
    compact_pairs: CompactPreferencePairs | None = None
    if config.objective == "pairwise-logistic":
        cached_payload = collect_pairwise_training_data_cached(config, analyzer)
        pairs = cached_payload.pairs
        listwise_examples = cached_payload.listwise_examples
        validation_records = cached_payload.validation_records
        row_stats = cached_payload.row_stats
        compact_pairs = cached_payload.compact_pairs
    else:
        pairs, listwise_examples, validation_records, row_stats = collect_training_data(config, analyzer=analyzer)
    compact_listwise = compact_listwise_dataset(listwise_examples)
    listwise_examples = []
    initial_weights: WeightsByPhase = {phase: {} for phase in PHASES}
    if config.objective == "pairwise-logistic":
        initial_metrics = evaluate_pairs(config, initial_weights, pairs)
    else:
        initial_metrics = evaluate_listwise_examples(config, initial_weights, compact_listwise)
    training_timing: dict[str, Any] = {}
    training_start = time.perf_counter()
    if config.objective == "pairwise-logistic":
        assert compact_pairs is not None
        weights, history, training_timing = train_compact_weights(config, compact_pairs)
    else:
        weights, history = train_weights(config, pairs, compact_listwise)
    listwise_training_seconds = time.perf_counter() - training_start if config.objective != "pairwise-logistic" else 0.0
    pairwise_training_seconds = time.perf_counter() - training_start if config.objective == "pairwise-logistic" else 0.0
    initial_diagnostics = move_choice_metrics(config, initial_weights, compact_listwise)
    final_diagnostics = move_choice_metrics(config, weights, compact_listwise)
    config, calibration_summary = calibrate_output_scale(config, weights, compact_listwise)
    quantized_weights = quantized_dequantized_weights(
        weights,
        output_scale=config.output_scale,
        max_abs_output_weight=config.max_abs_output_weight,
    )
    quantized_diagnostics = move_choice_metrics(config, quantized_weights, compact_listwise)
    validation_records = validation_rows_with_margins(config, weights, pairs, validation_records)
    if config.objective == "pairwise-logistic":
        final_metrics = evaluate_pairs(config, weights, pairs)
    else:
        final_metrics = evaluate_listwise_examples(config, weights, compact_listwise)
    timing = dict(row_stats.get("timing", {}))
    timing.update(training_timing)
    timing["listwise_training_seconds"] = listwise_training_seconds
    timing["pairwise_training_seconds"] = pairwise_training_seconds
    timing["examples_per_second"] = row_stats.get("listwise_feature_examples_per_second", 0.0)
    timing["updates_per_second"] = (
        history[-1].get("updates_per_second", timing.get("updates_per_second", 0.0))
        if history
        else timing.get("updates_per_second", 0.0)
    )
    timing["total_seconds"] = round(time.perf_counter() - timing_start, 6)
    timing["peak_pair_count"] = len(pairs)
    timing["memory_estimate_bytes"] = (
        compact_pairs.memory_bytes if compact_pairs is not None else 0
    )
    timing["feature_cache_hit_rate"] = row_stats.get("feature_cache", {}).get("hit_rate", 0.0)

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

    post_training_symmetrize_summary = apply_post_training_symmetrize(config, table_paths)

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
        "analysis_runner": config.analysis_runner,
        "analysis_cache_mode": config.analysis_cache.mode,
        "analysis_cache_dir": (
            str(config.analysis_cache.directory) if config.analysis_cache.directory is not None else ""
        ),
        "analysis_cache_hits": row_stats.get("analysis_cache_hits", 0),
        "analysis_cache_misses": row_stats.get("analysis_cache_misses", 0),
        "analysis_cache_writes": row_stats.get("analysis_cache_writes", 0),
        "analysis_jobs": config.analysis_jobs,
        "analysis_elapsed_seconds": row_stats.get("analysis_elapsed_seconds", 0.0),
        "pair_cache": {
            "mode": config.pair_cache_mode,
            "dir": str(config.pair_cache_dir) if config.pair_cache_dir is not None else "",
            "status": row_stats.get("pair_cache_status", "off"),
            "key": row_stats.get("pair_cache_key", ""),
        },
        "checkpoint": {
            "dir": str(config.checkpoint_dir) if config.checkpoint_dir is not None else "",
            "resume_requested": config.resume_checkpoint,
            "resumed_from_epoch": training_timing.get("resumed_from_epoch", 0),
            "config_hash": training_timing.get("config_hash", ""),
        },
        "listwise_feature_cache_mode": config.listwise_feature_cache_mode,
        "listwise_feature_cache_dir": (
            str(config.listwise_feature_cache_dir) if config.listwise_feature_cache_dir is not None else ""
        ),
        "families": list(config.families),
        "split": config.split,
        "objective": config.objective,
        "loss": config.loss,
        "pair_mode": config.pair_mode,
        "pair_weighting": config.pair_weighting,
        "bucket_weighting": {
            "path": str(config.bucket_weights_path) if config.bucket_weights_path is not None else "",
            "field": config.bucket_field,
            "default_weight": config.default_bucket_weight,
            "weights": config.bucket_weights,
        },
        "guard": {
            "mode": config.guard_mode,
            "weight": config.guard_weight,
            "max_pairs_per_position": config.guard_max_pairs_per_position,
        },
        "max_pairs_per_position": config.max_pairs_per_position,
        "exact_best_weight": config.exact_best_weight,
        "teacher_weight": config.teacher_weight,
        "min_score_margin": config.min_score_margin,
        "drop_teacher_exact_disagreement": config.drop_teacher_exact_disagreement,
        "exact_aware_only_when_available": config.exact_aware_only_when_available,
        "post_training_symmetrize": post_training_symmetrize_summary,
        "max_top_group_size_for_training": config.max_top_group_size_for_training,
        "tie_penalty": config.tie_penalty,
        "target_top_group_size": config.target_top_group_size,
        "top_group_margin": config.top_group_margin,
        "sign_penalty": config.sign_penalty,
        "high_confidence_margin": config.high_confidence_margin,
        "l2": config.l2,
        "epochs": config.epochs,
        "learning_rate": config.learning_rate,
        "max_abs_weight": config.max_abs_weight,
        "output_scale": config.output_scale,
        "calibrate_output_scale": config.calibrate_output_scale,
        "scale_grid": list(config.scale_grid),
        "max_abs_output_weight": config.max_abs_output_weight,
        "candidate_pattern_table_weight": config.candidate_pattern_table_weight,
        "model": TRAINER_MODEL,
        "seed": config.seed,
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
        "rows": row_stats,
        "timing": timing,
        "training": {
            "initial_metrics": initial_metrics,
            "history": history,
            "final_metrics": final_metrics,
            "weights": weight_summary,
            "listwise_compact": {
                "examples": compact_listwise.example_count,
                "candidates": compact_listwise.candidate_count,
                "feature_entries": compact_listwise.feature_entry_count,
                "unique_features": len(compact_listwise.feature_keys),
            },
        },
        "diagnostics": {
            "initial": initial_diagnostics,
            "final": final_diagnostics,
            "quantized_final": quantized_diagnostics,
        },
        "output_scale_calibration": calibration_summary,
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
        if config.diagnose_dataset:
            result = diagnose_dataset(config)
            print(f"wrote {config.out_dir} dataset_diagnostic={result.report_path}")
            return 0
        result = train_pairwise_tables(config)
        print(f"wrote {config.out_dir} candidate_eval={result.candidate_eval_path}")
        return 0
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
