#!/usr/bin/env python3
"""Train phase-specific PatternOnly tables from listwise teacher/exact labels.

This is a tooling-only trainer. It reads reusable teacher/exact JSONL artifacts,
builds exact-aware listwise targets, and writes local candidate artifacts under
runs/.
"""

from __future__ import annotations

import argparse
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
from pattern_training.board9 import (
    apply_move_to_board,
    board_key,
    board_to_text,
    empty_count,
    legal_moves_for_board,
    normalize_move,
    occupied_count,
    opponent,
    parse_board,
)
from pattern_training.features import pattern_counts, preference_delta, root_move_features
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
ANALYSIS_RUNNERS = ("subprocess", "batch")
LISTWISE_FEATURE_CACHE_MODES = ("off", "read-write", "read-only", "refresh")
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
    source_bucket: str
    board_text: str
    teacher_move: str
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
    analysis_runner: str
    analysis_jobs: int
    listwise_feature_cache_dir: Path | None
    listwise_feature_cache_mode: str
    families: tuple[str, ...]
    split: str
    bucket_weights_path: Path | None
    bucket_weights: dict[str, float]
    default_bucket_weight: float
    bucket_field: str
    diagnose_dataset: bool
    exact_score_temperature: float
    exact_score_target_floor: float
    exact_score_near_best_window: int
    post_training_symmetrize_modes: tuple[str, ...]
    max_top_group_size_for_training: int
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
class MoveScoreCoverage:
    status: str
    present_count: int
    legal_count: int


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
        description="Train PatternOnly listwise pattern tables from teacher/exact labels."
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
            "families, phase cutoffs, canonical listwise policy, and seed"
        ),
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
    parser.add_argument(
        "--bucket-weights",
        help=(
            "optional JSON object mapping label-row bucket names to example-weight multipliers"
        ),
    )
    parser.add_argument(
        "--default-bucket-weight",
        type=parse_non_negative_float,
        default=1.0,
        help="example-weight multiplier for rows whose bucket is missing or unmapped",
    )
    parser.add_argument(
        "--bucket-field",
        default="source_bucket",
        help="label-row field used to look up bucket weights (default: source_bucket)",
    )
    parser.add_argument(
        "--diagnose-dataset",
        action="store_true",
        help="write teacher/exact/listwise diagnostics without training or candidate output",
    )
    parser.add_argument(
        "--exact-score-temperature",
        type=parse_positive_float,
        default=4.0,
        help="temperature in discs for soft exact-score targets (default: 4.0)",
    )
    parser.add_argument(
        "--exact-score-target-floor",
        type=parse_non_negative_float,
        default=0.0001,
        help=(
            "minimum target probability per legal move for soft exact-score targets "
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
    if args.exact_score_target_floor >= 1.0:
        raise ScriptError("--exact-score-target-floor must be less than 1")
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
        bucket_weights_path=bucket_weights_path,
        bucket_weights=bucket_weights,
        default_bucket_weight=args.default_bucket_weight,
        bucket_field=args.bucket_field,
        diagnose_dataset=args.diagnose_dataset,
        exact_score_temperature=args.exact_score_temperature,
        exact_score_target_floor=args.exact_score_target_floor,
        exact_score_near_best_window=args.exact_score_near_best_window,
        post_training_symmetrize_modes=post_training_symmetrize_modes,
        max_top_group_size_for_training=args.max_top_group_size_for_training,
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
    return move_scores_from_record(record, score_keys=("exact_score_side_to_move", "score"))


def teacher_move_scores(record: dict[str, Any] | None) -> dict[str, int]:
    return move_scores_from_record(record, score_keys=("score", "teacher_score", "eval_score", "root_score"))


def move_scores_from_record(
    record: dict[str, Any] | None,
    *,
    score_keys: tuple[str, ...],
) -> dict[str, int]:
    if record is None:
        return {}
    root_scores = record.get("root_scores")
    if isinstance(root_scores, dict):
        parsed_from_root: dict[str, int] = {}
        for raw_move, raw_score in root_scores.items():
            move = normalize_move(raw_move)
            if move is None or isinstance(raw_score, bool) or not isinstance(raw_score, int):
                continue
            parsed_from_root[move] = raw_score
        if parsed_from_root:
            return parsed_from_root
    scores = record.get("move_scores")
    if not isinstance(scores, list):
        return {}
    parsed: dict[str, int] = {}
    for item in scores:
        if not isinstance(item, dict):
            continue
        move = normalize_move(item.get("move"))
        score = None
        for key in score_keys:
            raw_score = item.get(key)
            if not isinstance(raw_score, bool) and isinstance(raw_score, int):
                score = raw_score
                break
        if move is None or isinstance(score, bool) or not isinstance(score, int):
            continue
        parsed[move] = score
    return parsed


def move_score_coverage(scores: dict[str, int], legal_moves: set[str]) -> MoveScoreCoverage:
    legal_count = len(legal_moves)
    present_count = sum(1 for move in legal_moves if move in scores)
    if legal_count == 0:
        status = "complete"
    elif present_count == legal_count:
        status = "complete"
    elif present_count == 0:
        status = "missing"
    else:
        status = "partial"
    return MoveScoreCoverage(status=status, present_count=present_count, legal_count=legal_count)


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
        "listwise_policy": {
            "split": config.split,
            "exact_score_temperature": config.exact_score_temperature,
            "exact_score_target_floor": config.exact_score_target_floor,
            "exact_score_near_best_window": config.exact_score_near_best_window,
            "max_top_group_size_for_training": config.max_top_group_size_for_training,
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


def phase_for_board(board_text: str, cutoffs: PhaseCutoffs) -> str:
    return phase_for_occupied(occupied_count(board_text), cutoffs)


def exact_score_target_probabilities(
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
    if exact_scores:
        target_probabilities = exact_score_target_probabilities(
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
                example_weight=bucket_weight,
                bucket=bucket,
                bucket_weight=bucket_weight,
            )
    exact_targets = tuple(move for move in exact_best if move in legal_moves)
    if exact_targets:
        target_moves = exact_targets
    elif teacher_move in legal_moves:
        target_moves = (teacher_move,)
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
        example_weight=bucket_weight,
        bucket=bucket,
        bucket_weight=bucket_weight,
    )


def root_score_ranks(root_scores: dict[str, int]) -> dict[str, int]:
    ordered_scores = sorted(set(root_scores.values()), reverse=True)
    score_rank = {score: index + 1 for index, score in enumerate(ordered_scores)}
    return {move: score_rank[score] for move, score in root_scores.items()}


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
    )


def bucket_for_source(config: TrainerConfig, source: LabelSource) -> str:
    value = source.record.get(config.bucket_field)
    if isinstance(value, str) and value.strip():
        return value.strip()
    return "__missing__"


def source_bucket_for_source(source: LabelSource) -> str:
    value = source.record.get("source_bucket")
    if isinstance(value, str) and value.strip():
        return value.strip()
    return "__missing__"


def bucket_weight_for_source(config: TrainerConfig, source: LabelSource) -> float:
    bucket = bucket_for_source(config, source)
    return config.bucket_weights.get(bucket, config.default_bucket_weight)


def _new_diagnostic_group() -> dict[str, Any]:
    return {
        "counters": collections.Counter(),
        "legal_move_count_distribution": collections.Counter(),
        "example_count_distribution": collections.Counter(),
        "score_margin_distribution": collections.Counter(),
        "rank_margin_distribution": collections.Counter(),
        "teacher_rank_distribution": collections.Counter(),
        "exact_best_top_group_size_distribution": collections.Counter(),
        "example_weight_mass": 0.0,
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
        "example_count_distribution": {
            str(key): int(value) for key, value in sorted(group["example_count_distribution"].items())
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
        "example_weight_mass": float(group["example_weight_mass"]),
    }


def serialize_dataset_diagnostics(diagnostics: dict[str, Any]) -> dict[str, Any]:
    return {
        dimension: {
            key: _serialize_diagnostic_group(group)
            for key, group in sorted(groups.items())
        }
        for dimension, groups in diagnostics.items()
    }


def _new_qc_summary() -> dict[str, Any]:
    return {
        "root_phase_counts": _new_phase_count_summary(),
        "child_phase_counts": _new_phase_count_summary(),
        "root_to_child_phase_counts": collections.defaultdict(collections.Counter),
        "training_pattern_family_counts": {
            "overall": collections.Counter(),
            "by_phase": collections.defaultdict(collections.Counter),
        },
        "legal_move_count_distribution": collections.Counter(),
        "complete_exact_move_scores": _new_coverage_summary(),
        "complete_teacher_move_scores": _new_coverage_summary(),
        "source_bucket_counts": collections.Counter(),
        "training_bucket_counts": collections.Counter(),
        "duplicate_groups": collections.defaultdict(lambda: {"rows": 0, "splits": collections.Counter()}),
    }


def _new_phase_count_summary() -> dict[str, Any]:
    return {
        "overall": collections.Counter(),
        "by_split": collections.defaultdict(collections.Counter),
        "by_source_bucket": collections.defaultdict(collections.Counter),
    }


def _new_coverage_summary() -> dict[str, Any]:
    return {
        "overall": collections.Counter(),
        "by_split": collections.defaultdict(collections.Counter),
        "by_phase": collections.defaultdict(collections.Counter),
        "by_source_bucket": collections.defaultdict(collections.Counter),
    }


def _bump_phase_count(
    summary: dict[str, Any],
    *,
    phase: str,
    split: str,
    bucket: str,
) -> None:
    summary["overall"][phase] += 1
    summary["by_split"][split][phase] += 1
    summary["by_source_bucket"][bucket][phase] += 1


def _bump_coverage_summary(
    summary: dict[str, Any],
    *,
    coverage: MoveScoreCoverage,
    split: str,
    phase: str,
    bucket: str,
) -> None:
    for counter in (
        summary["overall"],
        summary["by_split"][split],
        summary["by_phase"][phase],
        summary["by_source_bucket"][bucket],
    ):
        counter["rows"] += 1
        counter[f"{coverage.status}_rows"] += 1
        counter["present_scores"] += coverage.present_count
        counter["legal_moves"] += coverage.legal_count


def _serialize_counter(counter: collections.Counter[Any]) -> dict[str, int]:
    return {str(key): int(value) for key, value in sorted(counter.items())}


def _serialize_coverage_group(counter: collections.Counter[str]) -> dict[str, Any]:
    rows = int(counter["rows"])
    complete_rows = int(counter["complete_rows"])
    legal_moves = int(counter["legal_moves"])
    present_scores = int(counter["present_scores"])
    return {
        "rows": rows,
        "complete_rows": complete_rows,
        "partial_rows": int(counter["partial_rows"]),
        "missing_rows": int(counter["missing_rows"]),
        "complete_rate": (complete_rows / rows) if rows else None,
        "present_scores": present_scores,
        "legal_moves": legal_moves,
        "score_coverage_rate": (present_scores / legal_moves) if legal_moves else None,
    }


def _serialize_coverage_summary(summary: dict[str, Any]) -> dict[str, Any]:
    return {
        "overall": _serialize_coverage_group(summary["overall"]),
        "by_split": {
            key: _serialize_coverage_group(counter)
            for key, counter in sorted(summary["by_split"].items())
        },
        "by_phase": {
            key: _serialize_coverage_group(counter)
            for key, counter in sorted(summary["by_phase"].items())
        },
        "by_source_bucket": {
            key: _serialize_coverage_group(counter)
            for key, counter in sorted(summary["by_source_bucket"].items())
        },
    }


def _serialize_phase_count_summary(summary: dict[str, Any]) -> dict[str, Any]:
    return {
        "overall": _serialize_counter(summary["overall"]),
        "by_split": {
            key: _serialize_counter(counter)
            for key, counter in sorted(summary["by_split"].items())
        },
        "by_source_bucket": {
            key: _serialize_counter(counter)
            for key, counter in sorted(summary["by_source_bucket"].items())
        },
    }


def _serialize_pattern_family_counts(summary: dict[str, Any]) -> dict[str, Any]:
    return {
        "overall": _serialize_counter(summary["overall"]),
        "by_phase": {
            phase: _serialize_counter(counter)
            for phase, counter in sorted(summary["by_phase"].items())
        },
    }


def _serialize_duplicate_group_check(groups: dict[str, Any], *, limit: int = 20) -> dict[str, Any]:
    duplicate_count = 0
    duplicate_rows = 0
    leaking: list[dict[str, Any]] = []
    for group_key, group in groups.items():
        rows = int(group["rows"])
        if rows < 2:
            continue
        duplicate_count += 1
        duplicate_rows += rows
        split_counts = _serialize_counter(group["splits"])
        if len(split_counts) > 1:
            leaking.append(
                {
                    "group_key_sha256": sha256_text(group_key),
                    "rows": rows,
                    "split_counts": split_counts,
                }
            )
    leaking.sort(key=lambda item: (-int(item["rows"]), item["group_key_sha256"]))
    return {
        "duplicate_groups": duplicate_count,
        "duplicate_rows": duplicate_rows,
        "leaking_groups": len(leaking),
        "leaking_rows": sum(int(item["rows"]) for item in leaking),
        "examples": leaking[:limit],
    }


def serialize_qc_summary(qc: dict[str, Any]) -> dict[str, Any]:
    return {
        "root_phase_counts": _serialize_phase_count_summary(qc["root_phase_counts"]),
        "child_phase_counts": _serialize_phase_count_summary(qc["child_phase_counts"]),
        "root_to_child_phase_counts": {
            root_phase: _serialize_counter(children)
            for root_phase, children in sorted(qc["root_to_child_phase_counts"].items())
        },
        "training_pattern_family_counts": _serialize_pattern_family_counts(
            qc["training_pattern_family_counts"]
        ),
        "legal_move_count_distribution": _serialize_counter(qc["legal_move_count_distribution"]),
        "complete_exact_move_scores": _serialize_coverage_summary(qc["complete_exact_move_scores"]),
        "complete_teacher_move_scores": _serialize_coverage_summary(qc["complete_teacher_move_scores"]),
        "source_bucket_counts": _serialize_counter(qc["source_bucket_counts"]),
        "training_bucket_counts": _serialize_counter(qc["training_bucket_counts"]),
        "duplicate_group_split_leakage_check": _serialize_duplicate_group_check(qc["duplicate_groups"]),
    }


def collect_training_data(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> tuple[list[ListwiseExample], list[ValidationRecord], dict[str, Any]]:
    total_start = time.perf_counter()
    label_start = time.perf_counter()
    sources = read_jsonl_sources(config.teacher_labels)
    label_load_seconds = time.perf_counter() - label_start
    exact_start = time.perf_counter()
    exact_by_board = load_exact_by_board(config.exact_labels) if config.exact_labels else {}
    exact_load_seconds = time.perf_counter() - exact_start
    listwise_examples: list[ListwiseExample] = []
    validation_records: list[ValidationRecord] = []
    stats: collections.Counter[str] = collections.Counter()
    bucket_example_counts: collections.Counter[str] = collections.Counter()
    bucket_example_weight_mass: collections.Counter[str] = collections.Counter()
    dataset_diagnostics = _new_dataset_diagnostics()
    qc_summary = _new_qc_summary()
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
        source_bucket = source_bucket_for_source(source)
        qc_summary["source_bucket_counts"][source_bucket] += 1
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
            bucket=source_bucket,
            key="teacher_rows",
        )
        if not accepted_teacher_record(record):
            stats["unusable_teacher_rows"] += 1
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=source_bucket,
                key="illegal_skipped_rows",
            )
            continue
        stats["accepted_teacher_rows"] += 1
        _bump_diagnostic_counter(
            dataset_diagnostics,
            split=split,
            phase=phase,
            bucket=source_bucket,
            key="accepted_rows",
        )
        stats[f"{split}_rows"] += 1

        board_text = board_text_from_record(record)
        assert board_text is not None
        phase = phase_for_board(board_text, config.phase_cutoffs)
        _bump_phase_count(qc_summary["root_phase_counts"], phase=phase, split=split, bucket=source_bucket)
        teacher_move = normalize_move(teacher_move_from_record(record))
        if teacher_move is None:
            stats["missing_teacher_move_skipped"] += 1
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=source_bucket,
                key="illegal_skipped_rows",
            )
            continue
        legal_moves = legal_moves_for_board(board_text)
        qc_summary["legal_move_count_distribution"][str(len(legal_moves))] += 1
        for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=source_bucket):
            group["legal_move_count_distribution"][len(legal_moves)] += 1
        if teacher_move not in legal_moves:
            stats["illegal_teacher_move_skipped"] += 1
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=source_bucket,
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
        _bump_coverage_summary(
            qc_summary["complete_exact_move_scores"],
            coverage=move_score_coverage(exact_move_scores(exact_record), legal_moves),
            split=split,
            phase=phase,
            bucket=source_bucket,
        )
        _bump_coverage_summary(
            qc_summary["complete_teacher_move_scores"],
            coverage=move_score_coverage(teacher_move_scores(record), legal_moves),
            split=split,
            phase=phase,
            bucket=source_bucket,
        )
        duplicate_group = qc_summary["duplicate_groups"][board_key(board_text)]
        duplicate_group["rows"] += 1
        duplicate_group["splits"][split] += 1
        if exact_best:
            for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=source_bucket):
                group["exact_best_top_group_size_distribution"][len(exact_best)] += 1
            if teacher_move in exact_best:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=source_bucket,
                    key="teacher_in_exact_best",
                )
            else:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=source_bucket,
                    key="teacher_not_in_exact_best",
                )
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=source_bucket,
                    key="teacher_exact_disagreement",
                )
        else:
            _bump_diagnostic_counter(
                dataset_diagnostics,
                split=split,
                phase=phase,
                bucket=source_bucket,
                key="exact_unavailable",
            )

        if config.split != "all" and split != config.split:
            stats["split_skipped"] += 1
            continue

        if exact_best and teacher_move not in exact_best:
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
        needs_analysis = True
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
                source_bucket=source_bucket,
                board_text=board_text,
                teacher_move=teacher_move,
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

    example_generation_start = time.perf_counter()
    for entry in entries:
        if isinstance(entry, ValidationRecord):
            validation_records.append(entry)
            continue
        source = entry.source
        split = entry.split
        phase = entry.phase
        source_bucket = entry.source_bucket
        board_text = entry.board_text
        teacher_move = entry.teacher_move
        legal_moves = entry.legal_moves
        exact_best = entry.exact_best
        analysis = analysis_by_source.get(entry.source_index) if entry.needs_analysis else None
        engine_move = analysis.best_move if analysis is not None else None
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
                    bucket=source_bucket,
                    key="engine_in_exact_best",
                )
            else:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=source_bucket,
                    key="engine_not_in_exact_best",
                )
        if engine_move == teacher_move:
            stats["already_agreed"] += 1
        root_scores = analysis.root_scores if analysis is not None else {}
        ranks = root_score_ranks(root_scores)
        teacher_rank = ranks.get(teacher_move)
        if exact_best and teacher_rank is not None:
            for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=source_bucket):
                group["teacher_rank_distribution"][teacher_rank] += 1
        if exact_best and root_scores:
            top_score = max(root_scores.values())
            engine_top_group = {move for move, score in root_scores.items() if score == top_score}
            if engine_top_group.intersection(exact_best):
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=source_bucket,
                    key="exact_best_in_engine_top_group",
                )
            else:
                _bump_diagnostic_counter(
                    dataset_diagnostics,
                    split=split,
                    phase=phase,
                    bucket=source_bucket,
                    key="exact_best_not_in_engine_top_group",
                )
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
            feature_cache=listwise_feature_cache,
        )
        if listwise_example is None:
            stats["no_example_generated_skipped"] += 1
            validation_records.append(
                make_validation_record(
                    source=source,
                    split=split,
                    phase=phase,
                    status="no_example_generated",
                    teacher_move=teacher_move,
                    engine_move=engine_move,
                    exact_best=exact_best,
                )
            )
            continue

        listwise_examples.append(listwise_example)
        stats["listwise_examples"] += 1
        stats["training_examples"] += 1
        if listwise_example.target_probabilities is not None:
            stats["soft_target_examples"] += 1
        elif listwise_example.exact_best_moves:
            stats["exact_best_examples"] += 1
        else:
            stats["teacher_fallback_examples"] += 1
        bucket_example_counts[listwise_example.bucket] += 1
        bucket_example_weight_mass[listwise_example.bucket] += listwise_example.example_weight
        qc_summary["training_bucket_counts"][listwise_example.bucket] += 1
        for candidate in listwise_example.candidates:
            _bump_phase_count(
                qc_summary["child_phase_counts"],
                phase=candidate.phase,
                split=split,
                bucket=source_bucket,
            )
            qc_summary["root_to_child_phase_counts"][phase][candidate.phase] += 1
            for family, _ in candidate.features:
                qc_summary["training_pattern_family_counts"]["overall"][family] += 1
                qc_summary["training_pattern_family_counts"]["by_phase"][candidate.phase][family] += 1
        for group in _diagnostic_groups(dataset_diagnostics, split=split, phase=phase, bucket=source_bucket):
            group["example_count_distribution"][1] += 1
            group["example_weight_mass"] += listwise_example.example_weight
        validation_records.append(
            make_validation_record(
                source=source,
                split=split,
                phase=phase,
                status="trained",
                teacher_move=teacher_move,
                engine_move=engine_move,
                preferred_move=listwise_example.target_moves[0],
                other_move="",
                pair_kind="listwise",
                pair_weight=listwise_example.example_weight,
                exact_best=exact_best,
            )
        )

    example_generation_seconds = time.perf_counter() - example_generation_start
    feature_construction_seconds = time.perf_counter() - feature_start
    if listwise_feature_cache is not None:
        listwise_feature_cache.save()

    for key in (
        "accepted_teacher_rows",
        "already_agreed",
        "exact_best_examples",
        "large_exact_top_group_skipped",
        "listwise_examples",
        "no_example_generated_skipped",
        "split_skipped",
        "soft_target_examples",
        "teacher_fallback_examples",
        "teacher_exact_disagreements_used_by_exact_aware",
        "teacher_exact_disagreements_skipped",
        "training_examples",
        "unusable_teacher_rows",
    ):
        stats.setdefault(key, 0)
    for split in ("train", "validation", "holdout"):
        stats.setdefault(f"{split}_rows", 0)
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
    row_stats["bucket_example_counts"] = {
        bucket: int(bucket_example_counts[bucket]) for bucket in sorted(bucket_example_counts)
    }
    row_stats["bucket_example_weight_mass"] = {
        bucket: float(bucket_example_weight_mass[bucket]) for bucket in sorted(bucket_example_weight_mass)
    }
    row_stats["dataset_diagnostics"] = serialize_dataset_diagnostics(dataset_diagnostics)
    row_stats["qc_summary"] = serialize_qc_summary(qc_summary)
    row_stats["timing"] = {
        "label_load_seconds": round(label_load_seconds, 6),
        "exact_load_seconds": round(exact_load_seconds, 6),
        "analysis_seconds": round(analysis_elapsed_seconds, 6),
        "feature_construction_seconds": round(feature_construction_seconds, 6),
        "example_generation_seconds": round(example_generation_seconds, 6),
        "collect_training_data_seconds": round(row_stats["total_collect_seconds"], 6),
    }
    return listwise_examples, validation_records, row_stats


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

        weights = {phase: dict(lazy_weights[phase].materialize()) for phase in PHASES}
        elapsed = time.perf_counter() - epoch_start
        history.append(
            {
                "epoch": epoch,
                **evaluate_listwise_examples(config, weights, dataset),
                "updates": update_count,
                "updates_per_second": update_count / elapsed if elapsed > 0.0 else 0.0,
                "epoch_seconds": elapsed,
            }
        )
    return weights, history


def train_weights(
    config: TrainerConfig,
    examples: ListwiseData,
) -> tuple[WeightsByPhase, list[dict[str, Any]]]:
    return train_listwise_weights(config, examples)


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
            "examples": 0,
            "loss": 0.0,
            "weighted_loss": 0.0,
            "unweighted_loss": 0.0,
            "accuracy": 0.0,
            "weighted_accuracy": 0.0,
            "avg_margin": 0.0,
            "total_example_weight": 0.0,
            "avg_example_weight": 0.0,
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
        "examples": dataset.example_count,
        "loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "weighted_loss": weighted_loss + 0.5 * config.l2 * l2_term,
        "unweighted_loss": sum(losses) / len(losses) + 0.5 * config.l2 * l2_term,
        "accuracy": correct / dataset.example_count,
        "weighted_accuracy": weighted_correct / total_weight if total_weight else 0.0,
        "avg_margin": sum(margins) / len(margins) if margins else 0.0,
        "total_example_weight": total_weight,
        "avg_example_weight": total_weight / dataset.example_count,
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
            if score_value == best_score
        }
        if len(top_group) > 1:
            top_ties += 1
        candidate_start = dataset.example_offsets[example_index]
        candidate_end = dataset.example_offsets[example_index + 1]
        candidate_indexes = range(candidate_start, candidate_end)
        local_scores = [
            compact_candidate_score(config, weights, dataset, candidate_index)
            for candidate_index in candidate_indexes
        ]
        probabilities = softmax_probabilities(local_scores)
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
        "# generated_by: tools/scripts/pattern_only_train.py",
        f"# phase: {phase}",
        "# no_strength_claim: true",
        "# not_default_promotion: true",
        f"# model: {TRAINER_MODEL}",
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
            "# generated_by: tools/scripts/pattern_only_train.py",
            "# no_strength_claim: true",
            "# not_default_promotion: true",
            "# trainer_foundation: true",
            f"# model: {TRAINER_MODEL}",
            f"# candidate_pattern_table_weight: {config.candidate_pattern_table_weight}",
            "schema_version=eval.v1",
            "mode=pattern_only",
            "name=pattern_only_listwise_candidate",
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
        "# PatternOnly Listwise Trainer Report",
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
        "",
        "## Training",
        "",
        f"- split: `{config.split}`",
        f"- families: `{', '.join(config.families)}`",
        f"- model: `{TRAINER_MODEL}`",
        f"- bucket_weights: `{config.bucket_weights_path or 'none'}`",
        f"- bucket_field: `{config.bucket_field}`",
        f"- default_bucket_weight: `{config.default_bucket_weight}`",
        f"- exact_score_temperature: `{config.exact_score_temperature}`",
        f"- exact_score_target_floor: `{config.exact_score_target_floor}`",
        f"- exact_score_near_best_window: `{config.exact_score_near_best_window}`",
        f"- post_training_symmetrize: `{','.join(config.post_training_symmetrize_modes) or 'off'}`",
        f"- max_top_group_size_for_training: `{config.max_top_group_size_for_training}`",
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
        if key in ("bucket_example_counts", "bucket_example_weight_mass"):
            continue
        lines.append(f"- {key}: `{rows[key]}`")
    bucket_counts = rows.get("bucket_example_counts", {})
    bucket_mass = rows.get("bucket_example_weight_mass", {})
    if bucket_counts or bucket_mass:
        lines.extend(["", "## Bucket Example Weights", ""])
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
                f"- example_generation_seconds: `{float(timing.get('example_generation_seconds', 0.0)):.6f}`",
                f"- training_seconds: `{float(timing.get('training_seconds', 0.0)):.6f}`",
                f"- examples_per_second: `{float(timing.get('examples_per_second', 0.0)):.6f}`",
                f"- updates_per_second: `{float(timing.get('updates_per_second', 0.0)):.6f}`",
                f"- example_count: `{int(timing.get('example_count', 0))}`",
                f"- candidate_count: `{int(timing.get('candidate_count', 0))}`",
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
            "## Listwise Metrics",
            "",
            f"- examples: `{final_metrics['examples']}`",
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
            f"- total_example_weight: `{final_metrics['total_example_weight']:.6f}`",
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
            "- The trainer learns pattern-only delta scores from exact-aware listwise examples; "
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
        "| group | teacher rows | accepted | illegal/skipped | exact unavailable | teacher/exact disagreement | examples | example weight mass | legal moves | example counts | score margins | rank margins | teacher ranks | exact top group |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |",
    ]
    for index, (name, group) in enumerate(groups.items()):
        if index >= limit:
            lines.append(
                f"| ... | {len(groups) - limit} more groups omitted from Markdown; see summary.json |  |  |  |  |  |  |  |  |  |  |  | |"
            )
            break
        example_counts = group.get("example_count_distribution", {})
        example_total = sum(int(value) * int(key) for key, value in example_counts.items() if str(key).isdigit())
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
                    str(example_total),
                    f"{float(group.get('example_weight_mass', 0.0)):.6f}",
                    _format_distribution(group.get("legal_move_count_distribution", {})),
                    _format_distribution(example_counts),
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


def _coverage_report_lines(title: str, summary: dict[str, Any]) -> list[str]:
    overall = summary.get("overall", {})
    return [
        f"## {title}",
        "",
        f"- rows: `{overall.get('rows', 0)}`",
        f"- complete_rows: `{overall.get('complete_rows', 0)}`",
        f"- partial_rows: `{overall.get('partial_rows', 0)}`",
        f"- missing_rows: `{overall.get('missing_rows', 0)}`",
        f"- complete_rate: `{overall.get('complete_rate') if overall.get('complete_rate') is not None else 'n/a'}`",
        f"- score_coverage_rate: `{overall.get('score_coverage_rate') if overall.get('score_coverage_rate') is not None else 'n/a'}`",
        "",
    ]


def render_dataset_diagnostic_report(config: TrainerConfig, summary: dict[str, Any]) -> str:
    rows = summary["rows"]
    diagnostics = rows.get("dataset_diagnostics", {})
    qc = rows.get("qc_summary", {})
    lines = [
        "# NTest Teacher Dataset Diagnostic",
        "",
        "This report performs teacher/exact/listwise-construction diagnostics only. It does not train weights, "
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
        f"- max_top_group_size_for_training: `{config.max_top_group_size_for_training}`",
        "",
        "## Overall Counts",
        "",
    ]
    for key in sorted(rows):
        if key in ("bucket_example_counts", "bucket_example_weight_mass", "dataset_diagnostics", "qc_summary"):
            continue
        lines.append(f"- {key}: `{rows[key]}`")
    if qc:
        lines.extend(["", "## QC Summary", ""])
        lines.extend(
            [
                "- source_bucket uses the literal `source_bucket` label field for provenance.",
                f"- training_bucket uses `--bucket-field {config.bucket_field}` and matches example weighting.",
                "- source_bucket_counts cover all teacher rows.",
                "- root phase, legal move, exact/teacher coverage, and duplicate leakage cover accepted legal rows before the selected split filter.",
                "- child phase, pattern family, and training_bucket counts cover generated listwise examples after the selected split filter.",
                "",
            ]
        )
        for key in (
            "root_phase_counts",
            "child_phase_counts",
            "root_to_child_phase_counts",
            "training_pattern_family_counts",
            "legal_move_count_distribution",
            "source_bucket_counts",
            "training_bucket_counts",
        ):
            lines.append(f"- {key}: `{json.dumps(qc.get(key, {}), sort_keys=True)}`")
        leakage = qc.get("duplicate_group_split_leakage_check", {})
        lines.append(
            "- duplicate_group_split_leakage_check: "
            f"`groups={leakage.get('duplicate_groups', 0)}, "
            f"leaking_groups={leakage.get('leaking_groups', 0)}, "
            f"leaking_rows={leakage.get('leaking_rows', 0)}`"
        )
        lines.extend([""])
        lines.extend(_coverage_report_lines("Complete Exact Move Scores", qc.get("complete_exact_move_scores", {})))
        lines.extend(_coverage_report_lines("Complete Teacher Move Scores", qc.get("complete_teacher_move_scores", {})))
    if rows.get("bucket_example_counts") or rows.get("bucket_example_weight_mass"):
        lines.extend(["", "## Bucket Example Weight Mass", ""])
        counts = rows.get("bucket_example_counts", {})
        mass = rows.get("bucket_example_weight_mass", {})
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
            "- teacher/exact disagreement is visible and explained by exact-best target selection",
            "- exact unavailable rows are expected for non-endgame positions and fall back to teacher targets",
            "- no split, phase, or source_bucket owns a surprising share of example weight mass",
            "- exact-best top groups are not so broad that they turn exact-aware targets into tie noise",
            "",
            "Safe preset for the next training pass:",
            "",
            "```sh",
            "python3 tools/scripts/pattern_only_train.py \\",
            "  --max-top-group-size-for-training 4 \\",
            "  --exact-score-temperature 4 \\",
            "  --exact-score-target-floor 0.0001 \\",
            "  --exact-score-near-best-window 8",
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
    examples, validation_records, row_stats = collect_training_data(config, analyzer=analyzer)
    validation_path = config.out_dir / "diagnostic_validation.tsv"
    write_validation_tsv(validation_path, validation_records)
    summary = {
        "script": "tools/scripts/pattern_only_train.py",
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
        "bucket_weighting": {
            "path": str(config.bucket_weights_path) if config.bucket_weights_path is not None else "",
            "field": config.bucket_field,
            "default_weight": config.default_bucket_weight,
            "weights": config.bucket_weights,
        },
        "exact_score_temperature": config.exact_score_temperature,
        "exact_score_target_floor": config.exact_score_target_floor,
        "exact_score_near_best_window": config.exact_score_near_best_window,
        "max_top_group_size_for_training": config.max_top_group_size_for_training,
        "seed": config.seed,
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
        "rows": row_stats,
        "listwise_metrics": evaluate_listwise_examples(
            config,
            {phase: {} for phase in PHASES},
            examples,
        ),
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


def train_pattern_tables(
    config: TrainerConfig,
    analyzer: Analyzer = run_analysis,
) -> TrainingResult:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    table_dir = config.out_dir / "tables"
    table_dir.mkdir(parents=True, exist_ok=True)

    timing_start = time.perf_counter()
    listwise_examples, validation_records, row_stats = collect_training_data(config, analyzer=analyzer)
    compact_listwise = compact_listwise_dataset(listwise_examples)
    listwise_examples = []
    initial_weights: WeightsByPhase = {phase: {} for phase in PHASES}
    initial_metrics = evaluate_listwise_examples(config, initial_weights, compact_listwise)
    training_start = time.perf_counter()
    weights, history = train_weights(config, compact_listwise)
    training_seconds = time.perf_counter() - training_start
    initial_diagnostics = move_choice_metrics(config, initial_weights, compact_listwise)
    final_diagnostics = move_choice_metrics(config, weights, compact_listwise)
    config, calibration_summary = calibrate_output_scale(config, weights, compact_listwise)
    quantized_weights = quantized_dequantized_weights(
        weights,
        output_scale=config.output_scale,
        max_abs_output_weight=config.max_abs_output_weight,
    )
    quantized_diagnostics = move_choice_metrics(config, quantized_weights, compact_listwise)
    final_metrics = evaluate_listwise_examples(config, weights, compact_listwise)
    timing = dict(row_stats.get("timing", {}))
    timing["training_seconds"] = training_seconds
    timing["examples_per_second"] = row_stats.get("listwise_feature_examples_per_second", 0.0)
    timing["updates_per_second"] = (
        history[-1].get("updates_per_second", timing.get("updates_per_second", 0.0))
        if history
        else timing.get("updates_per_second", 0.0)
    )
    timing["total_seconds"] = round(time.perf_counter() - timing_start, 6)
    timing["example_count"] = compact_listwise.example_count
    timing["candidate_count"] = compact_listwise.candidate_count
    timing["memory_estimate_bytes"] = 0

    table_paths = {phase: table_dir / f"{phase}.tsv" for phase in PHASES}
    weight_summary = summarize_weights(config, weights)
    for phase in PHASES:
        entries = phase_entries(weights, phase)
        phase_stats = {
            "training_examples": row_stats.get("training_examples", 0),
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
        "script": "tools/scripts/pattern_only_train.py",
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
        "listwise_feature_cache_mode": config.listwise_feature_cache_mode,
        "listwise_feature_cache_dir": (
            str(config.listwise_feature_cache_dir) if config.listwise_feature_cache_dir is not None else ""
        ),
        "families": list(config.families),
        "split": config.split,
        "model": TRAINER_MODEL,
        "bucket_weighting": {
            "path": str(config.bucket_weights_path) if config.bucket_weights_path is not None else "",
            "field": config.bucket_field,
            "default_weight": config.default_bucket_weight,
            "weights": config.bucket_weights,
        },
        "post_training_symmetrize": post_training_symmetrize_summary,
        "max_top_group_size_for_training": config.max_top_group_size_for_training,
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
            "tools/scripts/pattern_only_train.py",
            *(argv if argv is not None else sys.argv[1:]),
        ]
        config = config_from_args(args, invocation=invocation)
        if config.diagnose_dataset:
            result = diagnose_dataset(config)
            print(f"wrote {config.out_dir} dataset_diagnostic={result.report_path}")
            return 0
        result = train_pattern_tables(config)
        print(f"wrote {config.out_dir} candidate_eval={result.candidate_eval_path}")
        return 0
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
