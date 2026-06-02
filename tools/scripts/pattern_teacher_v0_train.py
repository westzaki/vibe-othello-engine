#!/usr/bin/env python3
"""Learn a small sparse pattern table from validated teacher residuals."""

from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

from common import ScriptError
from pattern_specs import (
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
    analyze_requests,
    sha256_file,
)
from pattern_training.analyzer import AnalyzerConfig
from pattern_training.analyzer import analyze_command as shared_analyze_command
from pattern_training.analyzer import run_analysis as shared_run_analysis
from pattern_training.dataset import (
    accepted_teacher_rows,
    empty_count,
    load_exact_best,
    normalize_move,
    parse_label_paths,
    read_jsonl,
)
from pattern_training.pattern_tables import (
    LEGACY_FAMILY_ALIASES as FAMILY_ALIASES,
    clamp,
    parse_families,
    render_table,
    sparse_entries,
    swapped_index,
)
from pattern_training.preference_features import (
    apply_preference_update,
    parse_board,
    pattern_indexes,
    pattern_indexes_by_family,
)
from pattern_training.root_candidates import RootAnalysis
from pattern_training.board9 import board_hash, board_key
from pattern_training.root_candidates import (
    RootCandidate,
    parse_analysis_stdout as parse_root_analysis_stdout,
)
from pattern_training.splits import (
    SplitRatios,
    parse_split_ratios,
    split_name_for_row,
    write_split_files,
)


Candidate = RootCandidate
AnalyzeResult = RootAnalysis


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a sparse pattern table from teacher-vs-evaluator residuals."
    )
    parser.add_argument("--teacher-labels", required=True, help="comma-separated teacher JSONL")
    parser.add_argument(
        "--dataset-root",
        help="shared dataset root for dataset: references; overrides VIBE_OTHELLO_DATASET_ROOT",
    )
    parser.add_argument("--eval-config", required=True, help="residual baseline .eval config")
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
    parser.add_argument("--out", required=True, help="output pattern table TSV")
    parser.add_argument("--table-name", default="pattern_teacher_v0")
    parser.add_argument("--exact-labels", help="optional comma-separated exact labels for filtering")
    parser.add_argument("--limit", type=int, help="optional accepted teacher row limit")
    parser.add_argument(
        "--families",
        default="legacy",
        help=(
            "comma-separated pattern families or aliases. Families: "
            f"{','.join(FAMILY_ORDER)}. Aliases: {','.join(sorted(FAMILY_ALIASES))}"
        ),
    )
    parser.add_argument("--corner-pairs", type=int, default=32)
    parser.add_argument("--corner-3x3-pairs", type=int, default=32)
    parser.add_argument("--edge-pairs", type=int, default=32)
    parser.add_argument("--edge-x-10-pairs", type=int, default=32)
    parser.add_argument("--diagonal-pairs", type=int, default=32)
    parser.add_argument("--inner-row-pairs", type=int, default=32)
    parser.add_argument("--min-abs-diff", type=int, default=3)
    parser.add_argument("--scale", type=int, default=3)
    parser.add_argument("--max-abs-weight", type=int, default=4)
    parser.add_argument("--depth", type=int, default=1)
    parser.add_argument(
        "--split",
        choices=("all", "train", "validation", "holdout"),
        default="all",
        help="deterministic teacher split to train on",
    )
    parser.add_argument(
        "--split-ratios",
        default="60,20,20",
        help="train,validation,holdout split weights",
    )
    parser.add_argument("--split-seed", type=int, default=20260601)
    parser.add_argument("--write-splits", help="optional directory for accepted teacher split JSONL")
    parser.add_argument(
        "--update-mode",
        choices=("residual", "rank"),
        default="residual",
        help="residual updates compare teacher with top choice; rank updates compare teacher with every higher-ranked choice",
    )
    parser.add_argument("--empty-min", type=int, help="minimum root empty count to include")
    parser.add_argument("--empty-max", type=int, help="maximum root empty count to include")
    return parser.parse_args(argv)


def pair_limit_for_family(args: argparse.Namespace, family: str) -> int:
    if family == "corner_2x3":
        return args.corner_pairs
    if family == "corner_3x3":
        return args.corner_3x3_pairs
    if family == "edge_8":
        return args.edge_pairs
    if family == "edge_x_10":
        return args.edge_x_10_pairs
    if family == "diagonal_8":
        return args.diagonal_pairs
    if family == "inner_row_8":
        return args.inner_row_pairs
    raise ScriptError(f"unknown pattern family: {family}")


def analyze_command(args: argparse.Namespace) -> list[str]:
    return shared_analyze_command(
        AnalyzerConfig(
            analyze_position=Path(args.analyze_position),
            eval_config=Path(args.eval_config),
            depth=args.depth,
        )
    )


def analyze_result_from_root(root: RootAnalysis) -> AnalyzeResult:
    return AnalyzeResult(
        candidates=root.candidates,
        best_move=root.best_move,
        root_scores=root.root_scores,
        stdout=root.stdout,
    )


def parse_analysis_stdout(text: str) -> AnalyzeResult:
    return analyze_result_from_root(parse_root_analysis_stdout(text))


def run_analysis(args: argparse.Namespace, board_text: str) -> AnalyzeResult:
    return analyze_result_from_root(
        shared_run_analysis(
            AnalyzerConfig(
                analyze_position=Path(args.analyze_position),
                eval_config=Path(args.eval_config),
                depth=args.depth,
            ),
            board_text,
        )
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


def _analysis_runner_config_from_args(args: argparse.Namespace) -> AnalysisRunnerConfig:
    return AnalysisRunnerConfig(
        analysis_cache=_analysis_cache_config_from_args(args),
        analysis_jobs=args.analysis_jobs,
        analyze_position=Path(args.analyze_position),
        eval_config=Path(args.eval_config),
        analysis_depth=args.depth,
        require_stdout_cache=True,
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    teacher_paths = parse_label_paths(args.teacher_labels, dataset_root=args.dataset_root)
    exact_best = (
        load_exact_best(parse_label_paths(args.exact_labels, dataset_root=args.dataset_root))
        if args.exact_labels
        else {}
    )
    split_ratios = parse_split_ratios(args.split_ratios)
    families = parse_families(args.families)
    if args.empty_min is not None and args.empty_max is not None and args.empty_min > args.empty_max:
        raise ScriptError("--empty-min cannot be greater than --empty-max")
    analysis_runner_config = _analysis_runner_config_from_args(args)
    rows = accepted_teacher_rows(teacher_paths, args.limit)
    if not rows:
        raise ScriptError("no accepted teacher rows")
    if args.write_splits:
        write_split_files(rows, Path(args.write_splits), split_ratios, args.split_seed)
    if args.split != "all":
        rows = [
            row
            for row in rows
            if split_name_for_row(row, split_ratios, args.split_seed) == args.split
        ]
    if not rows:
        raise ScriptError("no teacher rows after split filtering")

    family_counts: dict[str, collections.Counter[int]] = {
        family: collections.Counter() for family in families
    }
    stats = collections.Counter[str]()
    stats["accepted_teacher_rows"] = len(rows)
    stats["split_seed"] = args.split_seed
    stats["family_count"] = len(families)
    stats["analysis_cache_hits"] = 0
    stats["analysis_cache_misses"] = 0
    stats["analysis_cache_writes"] = 0
    stats["analysis_jobs"] = args.analysis_jobs
    eval_config_hash = sha256_file(Path(args.eval_config))
    prepared: list[tuple[int, dict[str, object], str, str]] = []
    analysis_requests: list[AnalysisRequest] = []
    for row in rows:
        stats["teacher_rows"] += 1
        board_text = str(row["board_text"])
        empties = empty_count(board_text)
        if args.empty_min is not None and empties < args.empty_min:
            stats["empty_min_skipped"] += 1
            continue
        if args.empty_max is not None and empties > args.empty_max:
            stats["empty_max_skipped"] += 1
            continue
        teacher_move = normalize_move(row.get("move"))
        if teacher_move is None:
            continue
        best_exact = exact_best.get(board_key(board_text))
        if best_exact is not None and teacher_move not in best_exact:
            stats["teacher_exact_disagreements_skipped"] += 1
            continue
        source_index = len(prepared)
        prepared.append((source_index, row, board_text, teacher_move))
        analysis_requests.append(
            AnalysisRequest(
                source_index=source_index,
                board_text=board_text,
                cache_key=analysis_cache_key(
                    board_hash=board_hash(board_text),
                    eval_config_hash=eval_config_hash,
                    analysis_depth=args.depth,
                ),
                position_id=str(row.get("position_id") or source_index),
            )
        )

    analysis_by_source = analyze_requests(
        config=analysis_runner_config,
        requests=analysis_requests,
        analyzer=lambda board_text: shared_run_analysis(
            AnalyzerConfig(
                analyze_position=Path(args.analyze_position),
                eval_config=Path(args.eval_config),
                depth=args.depth,
            ),
            board_text,
        ),
        stats=stats,
        eval_config_hash=eval_config_hash,
    )

    for source_index, row, board_text, teacher_move in prepared:
        del row
        analysis = analyze_result_from_root(analysis_by_source[source_index])
        candidates = [candidate for candidate in analysis.candidates if candidate.child_board]
        if not candidates:
            stats["positions_without_candidates"] += 1
            continue
        selected = candidates[0]
        if selected.move == teacher_move:
            stats["already_agreed"] += 1
            continue
        teacher = next((candidate for candidate in candidates if candidate.move == teacher_move), None)
        if teacher is None or teacher.child_board is None or selected.child_board is None:
            stats["teacher_missing_from_candidates"] += 1
            continue

        if args.update_mode == "rank":
            teacher_score = teacher.score
            rank_updates = 0
            if teacher_score is not None:
                for candidate in candidates:
                    if candidate.move == teacher.move or candidate.child_board is None:
                        continue
                    if candidate.score is not None and candidate.score > teacher_score:
                        apply_preference_update(
                            board_text=board_text,
                            teacher_child_board=teacher.child_board,
                            compared_child_board=candidate.child_board,
                            family_counts=family_counts,
                            families=families,
                        )
                        rank_updates += 1
            if rank_updates == 0:
                apply_preference_update(
                    board_text=board_text,
                    teacher_child_board=teacher.child_board,
                    compared_child_board=selected.child_board,
                    family_counts=family_counts,
                    families=families,
                )
                stats["rank_fallback_updates"] += 1
            else:
                stats["rank_pair_updates"] += rank_updates
            stats["residual_updates"] += 1
        else:
            apply_preference_update(
                board_text=board_text,
                teacher_child_board=teacher.child_board,
                compared_child_board=selected.child_board,
                family_counts=family_counts,
                families=families,
            )
            stats["residual_updates"] += 1

    family_entries: dict[str, list[tuple[int, int]]] = {}
    for family in families:
        entries = sparse_entries(
            family_counts[family],
            cells=len(PATTERN_SPECS[family][0]),
            limit_pairs=pair_limit_for_family(args, family),
            min_abs_diff=args.min_abs_diff,
            scale=args.scale,
            max_abs_weight=args.max_abs_weight,
        )
        family_entries[family] = entries
        stats[f"{family}_entries"] = len(entries)
        if family == "corner_2x3":
            stats["corner_entries"] = len(entries)
        elif family == "edge_8":
            stats["edge_entries"] = len(entries)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        render_table(
            name=args.table_name,
            family_entries=family_entries,
            stats=dict(stats),
            command=["tools/scripts/pattern_teacher_v0_train.py", *(argv or sys.argv[1:])],
        ),
        encoding="utf-8",
    )
    entry_summary = " ".join(
        f"{family}_entries={len(family_entries[family])}" for family in families
    )
    print(f"wrote {out} {entry_summary}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(exc.exit_code)
