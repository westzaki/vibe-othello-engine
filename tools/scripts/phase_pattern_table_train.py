#!/usr/bin/env python3
"""Train separate sparse pattern tables for opening/midgame/late phases."""

from __future__ import annotations

import argparse
import collections
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from common import ScriptError, parse_csv_values, quote_command
from dataset_paths import is_dataset_reference, resolve_dataset_root
import pattern_teacher_v0_train as base_trainer
from pattern_training.analysis_cache import (
    AnalysisCacheConfig,
    AnalysisRequest,
    AnalysisRunnerConfig,
    analysis_cache_key,
    analyze_requests,
    sha256_file,
)
from pattern_training.analyzer import AnalyzerConfig
from pattern_training.analyzer import analyze_command as shared_analyze_command
from pattern_training.analyzer import run_analysis as shared_run_analysis
from pattern_training.board9 import board_hash
from pattern_training.root_candidates import RootAnalysis


PHASES = ("opening", "midgame", "late")
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PHASE_CUTOFFS = (20, 44)
SENTINEL_FAMILY = "corner_2x3"
SENTINEL_ENTRY = (0, 0)


@dataclass(frozen=True)
class PhaseCutoffs:
    opening_max_occupied: int
    midgame_max_occupied: int


@dataclass(frozen=True)
class PhaseTrainConfig:
    teacher_label_paths: tuple[Path, ...]
    exact_label_paths: tuple[Path, ...]
    eval_config: Path
    analyze_position: Path
    analysis_cache: AnalysisCacheConfig
    analysis_jobs: int
    out_dir: Path
    table_name: str
    families: tuple[str, ...]
    update_mode: str
    split: str
    split_ratios: base_trainer.SplitRatios
    split_seed: int
    limit: int | None
    empty_min: int | None
    empty_max: int | None
    min_abs_diff: int
    scale: int
    max_abs_weight: int
    depth: int
    corner_pairs: int
    corner_3x3_pairs: int
    edge_pairs: int
    edge_x_10_pairs: int
    diagonal_pairs: int
    inner_row_pairs: int
    phase_cutoffs: PhaseCutoffs
    dataset_root: dict[str, str] | None
    invocation: list[str]


@dataclass(frozen=True)
class PhaseTrainResult:
    summary: dict[str, object]
    candidate_eval_path: Path
    table_paths: dict[str, Path]
    report_path: Path


Analyzer = Callable[[PhaseTrainConfig, str], base_trainer.AnalyzeResult]


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Generate phase-specific sparse pattern tables and a local .eval candidate."
        )
    )
    parser.add_argument("--teacher-labels", required=True, help="comma-separated teacher JSONL")
    parser.add_argument(
        "--dataset-root",
        help="shared dataset root for dataset: references; overrides VIBE_OTHELLO_DATASET_ROOT",
    )
    parser.add_argument("--exact-labels", help="optional comma-separated exact labels")
    parser.add_argument(
        "--eval-config",
        default="data/eval/pattern_reboot_v0.eval",
        help="pattern-only baseline .eval config used for analysis and candidate rendering",
    )
    parser.add_argument("--analyze-position", default="build/othello_analyze_position")
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
        type=int,
        default=1,
        help="maximum concurrent root-analysis subprocesses (default: 1)",
    )
    parser.add_argument("--out-dir", required=True, help="output directory, normally under runs/")
    parser.add_argument("--table-name", default="phase_broad_v0")
    parser.add_argument(
        "--families",
        default="broad_all",
        help=(
            "comma-separated pattern families or aliases. Families: "
            f"{','.join(base_trainer.FAMILY_ORDER)}. Aliases: "
            f"{','.join(sorted(base_trainer.FAMILY_ALIASES))}"
        ),
    )
    parser.add_argument(
        "--update-mode",
        choices=("residual", "rank"),
        default="rank",
        help="rank compares teacher with every higher-ranked choice; residual compares with top choice",
    )
    parser.add_argument(
        "--split",
        choices=("all", "train", "validation", "holdout"),
        default="train",
        help="deterministic teacher split to train on",
    )
    parser.add_argument("--split-seed", type=int, default=20260601)
    parser.add_argument(
        "--split-ratios",
        default="60,20,20",
        help="train,validation,holdout split weights",
    )
    parser.add_argument("--empty-min", type=int, help="minimum root empty count to include")
    parser.add_argument("--empty-max", type=int, help="maximum root empty count to include")
    parser.add_argument("--corner-pairs", type=int, default=64)
    parser.add_argument("--corner-3x3-pairs", type=int, default=64)
    parser.add_argument("--edge-pairs", type=int, default=64)
    parser.add_argument("--edge-x-10-pairs", type=int, default=64)
    parser.add_argument("--diagonal-pairs", type=int, default=64)
    parser.add_argument("--inner-row-pairs", type=int, default=64)
    parser.add_argument("--min-abs-diff", type=int, default=3)
    parser.add_argument("--scale", type=int, default=4)
    parser.add_argument("--max-abs-weight", type=int, default=4)
    parser.add_argument("--depth", type=int, default=1)
    parser.add_argument("--limit", type=int, help="optional accepted teacher row limit")
    return parser.parse_args(argv)


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


def phase_for_board(board_text: str, cutoffs: PhaseCutoffs) -> str:
    rows, _ = base_trainer.parse_board(board_text)
    occupied = sum(1 for row in rows for cell in row if cell in {"B", "W"})
    return phase_for_occupied(occupied, cutoffs)


def split_for_teacher_row(
    row: dict[str, object], ratios: base_trainer.SplitRatios, seed: int
) -> str:
    explicit = row.get("position_split")
    if explicit in {"train", "validation", "holdout"}:
        return str(explicit)
    return base_trainer.split_name_for_row(row, ratios, seed)


def _raw_label_values(args: argparse.Namespace) -> list[str]:
    values = parse_csv_values(args.teacher_labels, error_label="teacher label path list")
    if args.exact_labels:
        values.extend(parse_csv_values(args.exact_labels, error_label="exact label path list"))
    return values


def _dataset_root_metadata(args: argparse.Namespace) -> dict[str, str] | None:
    raw_values = _raw_label_values(args)
    if args.dataset_root is None and not any(is_dataset_reference(value) for value in raw_values):
        return None
    root = resolve_dataset_root(args.dataset_root, require_exists=False)
    return {"path": str(root.path), "source": root.source}


def resolve_out_dir(path_text: str) -> Path:
    path = Path(path_text)
    resolved = path.resolve(strict=False)
    source_data = (REPO_ROOT / "data").resolve(strict=False)
    try:
        resolved.relative_to(source_data)
    except ValueError:
        return path
    raise ScriptError("--out-dir must not be under source-controlled data/")


def _scalar_nonzero_keys(eval_text: str) -> list[str]:
    nonzero: list[str] = []
    for key, value in eval_config_entries(eval_text).items():
        phase, separator, feature = key.partition(".")
        if phase not in PHASES or not separator or feature == "pattern_table":
            continue
        try:
            parsed = int(value)
        except ValueError:
            continue
        if parsed != 0:
            nonzero.append(key)
    return sorted(nonzero)


def _validate_pattern_only_eval_config(eval_config: Path) -> None:
    nonzero = _scalar_nonzero_keys(read_eval_config_text(eval_config))
    if nonzero:
        preview = ", ".join(nonzero[:8])
        suffix = "" if len(nonzero) <= 8 else ", ..."
        raise ScriptError(
            "--eval-config must keep handcrafted scalar feature weights at zero "
            f"for phase-aware pattern training; nonzero keys: {preview}{suffix}"
        )


def _analysis_cache_config_from_args(args: argparse.Namespace) -> AnalysisCacheConfig:
    cache_dir = Path(args.analysis_cache_dir) if args.analysis_cache_dir else None
    if args.analysis_cache_mode != "off" and cache_dir is None:
        raise ScriptError("--analysis-cache-dir is required unless --analysis-cache-mode=off")
    if args.analysis_cache_mode == "off" and cache_dir is not None:
        raise ScriptError("--analysis-cache-mode must not be off when --analysis-cache-dir is provided")
    if args.analysis_jobs < 1:
        raise ScriptError("--analysis-jobs must be positive")
    return AnalysisCacheConfig(directory=cache_dir, mode=args.analysis_cache_mode)


def config_from_args(
    args: argparse.Namespace, invocation: list[str] | None = None
) -> PhaseTrainConfig:
    split_ratios = base_trainer.parse_split_ratios(args.split_ratios)
    families = base_trainer.parse_families(args.families)
    if args.empty_min is not None and args.empty_max is not None and args.empty_min > args.empty_max:
        raise ScriptError("--empty-min cannot be greater than --empty-max")
    eval_config = Path(args.eval_config)
    _validate_pattern_only_eval_config(eval_config)

    return PhaseTrainConfig(
        teacher_label_paths=tuple(
            base_trainer.parse_label_paths(args.teacher_labels, dataset_root=args.dataset_root)
        ),
        exact_label_paths=tuple(
            base_trainer.parse_label_paths(args.exact_labels, dataset_root=args.dataset_root)
            if args.exact_labels
            else ()
        ),
        eval_config=eval_config,
        analyze_position=Path(args.analyze_position),
        analysis_cache=_analysis_cache_config_from_args(args),
        analysis_jobs=args.analysis_jobs,
        out_dir=resolve_out_dir(args.out_dir),
        table_name=args.table_name,
        families=families,
        update_mode=args.update_mode,
        split=args.split,
        split_ratios=split_ratios,
        split_seed=args.split_seed,
        limit=args.limit,
        empty_min=args.empty_min,
        empty_max=args.empty_max,
        min_abs_diff=args.min_abs_diff,
        scale=args.scale,
        max_abs_weight=args.max_abs_weight,
        depth=args.depth,
        corner_pairs=args.corner_pairs,
        corner_3x3_pairs=args.corner_3x3_pairs,
        edge_pairs=args.edge_pairs,
        edge_x_10_pairs=args.edge_x_10_pairs,
        diagonal_pairs=args.diagonal_pairs,
        inner_row_pairs=args.inner_row_pairs,
        phase_cutoffs=phase_cutoffs_from_eval_config(eval_config),
        dataset_root=_dataset_root_metadata(args),
        invocation=invocation or [],
    )


def analyze_command(config: PhaseTrainConfig) -> list[str]:
    return shared_analyze_command(
        AnalyzerConfig(
            analyze_position=config.analyze_position,
            eval_config=config.eval_config,
            depth=config.depth,
        )
    )


def run_analysis(config: PhaseTrainConfig, board_text: str) -> base_trainer.AnalyzeResult:
    return base_trainer.analyze_result_from_root(
        shared_run_analysis(
            AnalyzerConfig(
                analyze_position=config.analyze_position,
                eval_config=config.eval_config,
                depth=config.depth,
            ),
            board_text,
        )
    )


def _root_from_analysis(analysis: base_trainer.AnalyzeResult) -> RootAnalysis:
    root_scores = dict(analysis.root_scores)
    if not root_scores:
        root_scores = {
            candidate.move: candidate.score
            for candidate in analysis.candidates
            if candidate.score is not None
        }
    best_move = analysis.best_move
    if best_move is None and root_scores:
        best_move = sorted(root_scores.items(), key=lambda item: (-item[1], item[0]))[0][0]
    return RootAnalysis(
        best_move=best_move,
        root_scores=root_scores,
        candidates=analysis.candidates,
        stdout=analysis.stdout,
    )


def pair_limit_for_family(config: PhaseTrainConfig, family: str) -> int:
    if family == "corner_2x3":
        return config.corner_pairs
    if family == "corner_3x3":
        return config.corner_3x3_pairs
    if family == "edge_8":
        return config.edge_pairs
    if family == "edge_x_10":
        return config.edge_x_10_pairs
    if family == "diagonal_8":
        return config.diagonal_pairs
    if family == "inner_row_8":
        return config.inner_row_pairs
    raise ScriptError(f"unknown pattern family: {family}")


def _apply_update(
    *,
    config: PhaseTrainConfig,
    board_text: str,
    teacher: base_trainer.Candidate,
    compared: base_trainer.Candidate,
    family_counts: dict[str, collections.Counter[int]],
) -> None:
    if teacher.child_board is None or compared.child_board is None:
        return
    base_trainer.apply_preference_update(
        board_text=board_text,
        teacher_child_board=teacher.child_board,
        compared_child_board=compared.child_board,
        family_counts=family_counts,
        families=config.families,
    )


def _update_phase_counts(
    *,
    config: PhaseTrainConfig,
    board_text: str,
    teacher_move: str,
    candidates: list[base_trainer.Candidate],
    family_counts: dict[str, collections.Counter[int]],
    phase_stats: collections.Counter[str],
) -> bool:
    selected = candidates[0]
    if selected.move == teacher_move:
        phase_stats["already_agreed"] += 1
        return False

    teacher = next((candidate for candidate in candidates if candidate.move == teacher_move), None)
    if teacher is None or teacher.child_board is None or selected.child_board is None:
        phase_stats["teacher_missing_from_candidates"] += 1
        return False

    pair_updates = 0
    if config.update_mode == "rank":
        teacher_score = teacher.score
        if teacher_score is not None:
            for candidate in candidates:
                if candidate.move == teacher.move or candidate.child_board is None:
                    continue
                if candidate.score is not None and candidate.score > teacher_score:
                    _apply_update(
                        config=config,
                        board_text=board_text,
                        teacher=teacher,
                        compared=candidate,
                        family_counts=family_counts,
                    )
                    pair_updates += 1
        if pair_updates == 0:
            _apply_update(
                config=config,
                board_text=board_text,
                teacher=teacher,
                compared=selected,
                family_counts=family_counts,
            )
            pair_updates = 1
            phase_stats["rank_fallback_updates"] += 1
        else:
            phase_stats["rank_pair_updates"] += pair_updates
    else:
        _apply_update(
            config=config,
            board_text=board_text,
            teacher=teacher,
            compared=selected,
            family_counts=family_counts,
        )
        pair_updates = 1
        phase_stats["residual_pair_updates"] += 1

    phase_stats["updates"] += 1
    phase_stats["preference_pair_updates"] += pair_updates
    return True


def render_phase_table(
    *,
    name: str,
    phase: str,
    family_entries: dict[str, list[tuple[int, int]]],
    stats: dict[str, int],
    command: list[str],
    empty_phase_sentinel: bool = False,
) -> str:
    lines = [
        "# schema_version: pattern_table.v1",
        f"# name: {name}_{phase}",
        "# generated_by: tools/scripts/phase_pattern_table_train.py",
        f"# phase: {phase}",
        f"# command: {quote_command(command)}",
    ]
    if empty_phase_sentinel:
        lines.extend(
            (
                "# empty_phase_sentinel: true",
                "# zero_effect: true",
            )
        )
    for key in sorted(stats):
        lines.append(f"# {key}: {stats[key]}")
    lines.append("")
    for family in base_trainer.FAMILY_ORDER:
        lines.extend(
            f"{family}\t{index}\t{value}"
            for index, value in family_entries.get(family, [])
        )
    return "\n".join(lines) + "\n"


def render_candidate_eval(eval_config: Path, table_name: str) -> str:
    base_text = read_eval_config_text(eval_config)
    lines: list[str] = [
        "# generated_by: tools/scripts/phase_pattern_table_train.py",
        "# no_strength_claim: true",
        "# not_default: true",
        "# generated_candidate: local_runs_artifact",
    ]
    inserted_tables = False
    for raw_line in base_text.splitlines():
        parsed = _line_key_value(raw_line)
        if parsed is not None:
            key, _ = parsed
            if key == "pattern_table" or key.startswith("pattern_table."):
                continue
            if key == "name":
                lines.append(f"name={table_name}")
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
                f"name={table_name}",
                "pattern_table.opening=tables/opening.tsv",
                "pattern_table.midgame=tables/midgame.tsv",
                "pattern_table.late=tables/late.tsv",
            )
        )
    return "\n".join(lines).rstrip() + "\n"


def _counter_dict(counter: collections.Counter[str]) -> dict[str, int]:
    return {key: int(counter[key]) for key in sorted(counter)}


def write_phase_summary(
    path: Path,
    phase_stats: dict[str, collections.Counter[str]],
    entries_by_phase: dict[str, dict[str, list[tuple[int, int]]]],
    families: tuple[str, ...],
) -> None:
    header = ["phase", "teacher_rows", "updates", "skipped", "empty_phase_sentinel"]
    header.extend(f"{family}_entries" for family in families)
    lines = ["\t".join(header)]
    for phase in PHASES:
        row = [
            phase,
            str(phase_stats[phase]["teacher_rows"]),
            str(phase_stats[phase]["updates"]),
            str(phase_stats[phase]["skipped"]),
            "true" if phase_stats[phase]["empty_phase_sentinel"] else "false",
        ]
        row.extend(str(len(entries_by_phase[phase].get(family, []))) for family in families)
        lines.append("\t".join(row))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_report(
    path: Path,
    *,
    config: PhaseTrainConfig,
    summary: dict[str, object],
    table_paths: dict[str, Path],
    candidate_eval_path: Path,
) -> None:
    rows = summary["rows"]
    assert isinstance(rows, dict)
    lines = [
        "# Phase Pattern Table Training Report",
        "",
        "This is a learning workflow artifact. It is not a strength claim, not an Elo estimate, "
        "and not a default-promotion recommendation.",
        "",
        "## Command",
        "",
        f"`{quote_command(config.invocation) if config.invocation else 'unknown'}`",
        "",
        "## Inputs",
        "",
        f"- teacher_labels: `{', '.join(str(path) for path in config.teacher_label_paths)}`",
        f"- exact_labels: `{', '.join(str(path) for path in config.exact_label_paths) or 'none'}`",
        f"- dataset_root: `{json.dumps(config.dataset_root, sort_keys=True)}`",
        f"- eval_config: `{config.eval_config}`",
        f"- analyze_position: `{config.analyze_position}`",
        "",
        "## Training",
        "",
        f"- phase_cutoffs: opening <= {config.phase_cutoffs.opening_max_occupied}, "
        f"midgame <= {config.phase_cutoffs.midgame_max_occupied}, else late",
        f"- families: `{', '.join(config.families)}`",
        f"- update_mode: `{config.update_mode}`",
        f"- split: `{config.split}`",
        f"- split_seed: `{config.split_seed}`",
        f"- split_ratios: `{config.split_ratios.train},{config.split_ratios.validation},{config.split_ratios.holdout}`",
        f"- empty_min: `{config.empty_min}`",
        f"- empty_max: `{config.empty_max}`",
        "",
        "Rows with no updates still leave their phase table loadable. A phase with no "
        "learned entries writes an explicit zero-effect sentinel entry "
        f"(`{SENTINEL_FAMILY}\\t{SENTINEL_ENTRY[0]}\\t{SENTINEL_ENTRY[1]}`).",
        "",
        "## Counts",
        "",
    ]
    for key in sorted(rows):
        lines.append(f"- {key}: `{rows[key]}`")
    lines.extend(("", "## Phase Summary", ""))
    phases = summary["phases"]
    assert isinstance(phases, dict)
    for phase in PHASES:
        phase_summary = phases[phase]
        assert isinstance(phase_summary, dict)
        lines.append(f"- {phase}: `{json.dumps(phase_summary, sort_keys=True)}`")
    lines.extend(("", "## Outputs", ""))
    for phase in PHASES:
        lines.append(f"- {phase}_table: `{table_paths[phase]}`")
    lines.append(f"- candidate_eval: `{candidate_eval_path}`")
    lines.extend(
        (
            "",
            "## Caveats",
            "",
            "- Generated TSVs and candidate `.eval` files belong under `runs/` and should not be committed.",
            "- This run does not promote the engine default.",
            "- Smoke checks from generated tables are behavior checks only, not strength proof.",
            "",
            "## Recommended Next Validation",
            "",
            "```sh",
            "python3 tools/scripts/eval_candidate_matrix.py \\",
            "  --build-dir build \\",
            f"  --candidates {candidate_eval_path} \\",
            f"  --out runs/eval-candidates/{config.table_name}-smoke",
            "```",
            "",
            "Use held-out teacher/exact labels and match/search validation before making any strength claim.",
            "",
        )
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def train_phase_tables(
    config: PhaseTrainConfig, analyzer: Analyzer = run_analysis
) -> PhaseTrainResult:
    rows = base_trainer.accepted_teacher_rows(list(config.teacher_label_paths), config.limit)
    if not rows:
        raise ScriptError("no accepted teacher rows")
    exact_best = (
        base_trainer.load_exact_best(list(config.exact_label_paths))
        if config.exact_label_paths
        else {}
    )

    family_counts_by_phase: dict[str, dict[str, collections.Counter[int]]] = {
        phase: {family: collections.Counter() for family in config.families}
        for phase in PHASES
    }
    phase_stats: dict[str, collections.Counter[str]] = {
        phase: collections.Counter() for phase in PHASES
    }
    row_stats = collections.Counter[str]()
    row_stats["accepted_teacher_rows"] = len(rows)
    row_stats["analysis_cache_hits"] = 0
    row_stats["analysis_cache_misses"] = 0
    row_stats["analysis_cache_writes"] = 0
    row_stats["analysis_jobs"] = config.analysis_jobs
    eval_config_hash = sha256_file(config.eval_config)
    prepared: list[tuple[int, str, str, str]] = []
    analysis_requests: list[AnalysisRequest] = []

    for row in rows:
        board_value = row.get("board_text")
        if not isinstance(board_value, str):
            row_stats["missing_board_text_skipped"] += 1
            continue
        board_text = board_value
        phase = phase_for_board(board_text, config.phase_cutoffs)
        split_name = split_for_teacher_row(row, config.split_ratios, config.split_seed)
        if config.split != "all" and split_name != config.split:
            row_stats["split_skipped"] += 1
            phase_stats[phase]["skipped"] += 1
            phase_stats[phase]["split_skipped"] += 1
            continue

        empties = base_trainer.empty_count(board_text)
        if config.empty_min is not None and empties < config.empty_min:
            row_stats["empty_min_skipped"] += 1
            phase_stats[phase]["skipped"] += 1
            phase_stats[phase]["empty_min_skipped"] += 1
            continue
        if config.empty_max is not None and empties > config.empty_max:
            row_stats["empty_max_skipped"] += 1
            phase_stats[phase]["skipped"] += 1
            phase_stats[phase]["empty_max_skipped"] += 1
            continue

        teacher_move = base_trainer.normalize_move(row.get("move"))
        if teacher_move is None:
            row_stats["missing_teacher_move_skipped"] += 1
            phase_stats[phase]["skipped"] += 1
            continue
        best_exact = exact_best.get(base_trainer.board_key(board_text))
        if best_exact is not None and teacher_move not in best_exact:
            row_stats["teacher_exact_disagreements_skipped"] += 1
            phase_stats[phase]["skipped"] += 1
            phase_stats[phase]["teacher_exact_disagreements_skipped"] += 1
            continue

        row_stats["training_rows"] += 1
        phase_stats[phase]["teacher_rows"] += 1
        source_index = len(prepared)
        prepared.append((source_index, phase, board_text, teacher_move))
        analysis_requests.append(
            AnalysisRequest(
                source_index=source_index,
                board_text=board_text,
                cache_key=analysis_cache_key(
                    board_hash=board_hash(board_text),
                    eval_config_hash=eval_config_hash,
                    analysis_depth=config.depth,
                ),
                position_id=str(row.get("position_id") or source_index),
            )
        )

    analysis_by_source = analyze_requests(
        config=AnalysisRunnerConfig(
            analysis_cache=config.analysis_cache,
            analysis_jobs=config.analysis_jobs,
            analyze_position=config.analyze_position,
            eval_config=config.eval_config,
            analysis_depth=config.depth,
            require_stdout_cache=True,
        ),
        requests=analysis_requests,
        analyzer=lambda board_text: _root_from_analysis(analyzer(config, board_text)),
        stats=row_stats,
        eval_config_hash=eval_config_hash,
    )

    for source_index, phase, board_text, teacher_move in prepared:
        analysis = base_trainer.analyze_result_from_root(analysis_by_source[source_index])
        candidates = [candidate for candidate in analysis.candidates if candidate.child_board]
        if not candidates:
            row_stats["positions_without_candidates"] += 1
            phase_stats[phase]["skipped"] += 1
            phase_stats[phase]["positions_without_candidates"] += 1
            continue
        updated = _update_phase_counts(
            config=config,
            board_text=board_text,
            teacher_move=teacher_move,
            candidates=candidates,
            family_counts=family_counts_by_phase[phase],
            phase_stats=phase_stats[phase],
        )
        if updated:
            row_stats["updates"] += 1
        else:
            row_stats["no_update_rows"] += 1
            phase_stats[phase]["skipped"] += 1

    if row_stats["training_rows"] == 0:
        raise ScriptError("no teacher rows after split/exact/empty filtering")

    entries_by_phase: dict[str, dict[str, list[tuple[int, int]]]] = {
        phase: {} for phase in PHASES
    }
    sentinel_by_phase: dict[str, bool] = {phase: False for phase in PHASES}
    for phase in PHASES:
        for family in config.families:
            entries = base_trainer.sparse_entries(
                family_counts_by_phase[phase][family],
                cells=len(base_trainer.PATTERN_SPECS[family][0]),
                limit_pairs=pair_limit_for_family(config, family),
                min_abs_diff=config.min_abs_diff,
                scale=config.scale,
                max_abs_weight=config.max_abs_weight,
            )
            entries_by_phase[phase][family] = entries
            phase_stats[phase][f"{family}_entries"] = len(entries)
        learned_entries = sum(len(entries) for entries in entries_by_phase[phase].values())
        phase_stats[phase]["learned_entries"] = learned_entries
        if learned_entries == 0:
            entries_by_phase[phase][SENTINEL_FAMILY] = [SENTINEL_ENTRY]
            sentinel_by_phase[phase] = True
            phase_stats[phase]["empty_phase_sentinel"] = 1
            phase_stats[phase]["zero_effect_sentinel_entries"] = 1

    table_dir = config.out_dir / "tables"
    table_dir.mkdir(parents=True, exist_ok=True)
    command = config.invocation or ["tools/scripts/phase_pattern_table_train.py"]
    table_paths: dict[str, Path] = {
        phase: table_dir / f"{phase}.tsv" for phase in PHASES
    }
    for phase in PHASES:
        table_paths[phase].write_text(
            render_phase_table(
                name=config.table_name,
                phase=phase,
                family_entries=entries_by_phase[phase],
                stats=_counter_dict(phase_stats[phase]),
                command=command,
                empty_phase_sentinel=sentinel_by_phase[phase],
            ),
            encoding="utf-8",
        )

    candidate_eval_path = config.out_dir / "candidate.eval"
    candidate_eval_path.write_text(
        render_candidate_eval(config.eval_config, config.table_name),
        encoding="utf-8",
    )
    phase_summary_path = config.out_dir / "phase_summary.tsv"
    write_phase_summary(
        phase_summary_path,
        phase_stats=phase_stats,
        entries_by_phase=entries_by_phase,
        families=config.families,
    )

    summary: dict[str, object] = {
        "script": "tools/scripts/phase_pattern_table_train.py",
        "command": quote_command(command),
        "teacher_label_paths": [str(path) for path in config.teacher_label_paths],
        "exact_label_paths": [str(path) for path in config.exact_label_paths],
        "dataset_root": config.dataset_root,
        "eval_config": str(config.eval_config),
        "analyze_position": str(config.analyze_position),
        "phase_cutoffs": {
            "opening_max_occupied": config.phase_cutoffs.opening_max_occupied,
            "midgame_max_occupied": config.phase_cutoffs.midgame_max_occupied,
        },
        "families": list(config.families),
        "update_mode": config.update_mode,
        "split": config.split,
        "split_seed": config.split_seed,
        "split_ratios": {
            "train": config.split_ratios.train,
            "validation": config.split_ratios.validation,
            "holdout": config.split_ratios.holdout,
        },
        "empty_min": config.empty_min,
        "empty_max": config.empty_max,
        "rows": _counter_dict(row_stats),
        "phases": {
            phase: {
                **_counter_dict(phase_stats[phase]),
                "entries_by_family": {
                    family: len(entries_by_phase[phase].get(family, []))
                    for family in config.families
                },
                "empty_phase_sentinel": sentinel_by_phase[phase],
                "sentinel": {
                    "family": SENTINEL_FAMILY,
                    "index": SENTINEL_ENTRY[0],
                    "value": SENTINEL_ENTRY[1],
                }
                if sentinel_by_phase[phase]
                else None,
            }
            for phase in PHASES
        },
        "outputs": {
            "candidate_eval": str(candidate_eval_path),
            "phase_summary": str(phase_summary_path),
            "tables": {phase: str(path) for phase, path in table_paths.items()},
        },
        "no_strength_claim": True,
        "default_promotion": False,
    }
    summary_path = config.out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    report_path = config.out_dir / "report.md"
    write_report(
        report_path,
        config=config,
        summary=summary,
        table_paths=table_paths,
        candidate_eval_path=candidate_eval_path,
    )

    return PhaseTrainResult(
        summary=summary,
        candidate_eval_path=candidate_eval_path,
        table_paths=table_paths,
        report_path=report_path,
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    invocation = ["tools/scripts/phase_pattern_table_train.py", *(argv or sys.argv[1:])]
    config = config_from_args(args, invocation=invocation)
    result = train_phase_tables(config)
    print(f"wrote {config.out_dir} candidate_eval={result.candidate_eval_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(exc.exit_code)
