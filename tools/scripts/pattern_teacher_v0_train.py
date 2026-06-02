#!/usr/bin/env python3
"""Learn a small sparse pattern table from validated teacher residuals."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from common import ScriptError, parse_csv_values, quote_command
from dataset_paths import resolve_path_references
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
    analyze_requests,
    sha256_file,
)
from pattern_training.analyzer import AnalyzerConfig
from pattern_training.analyzer import analyze_command as shared_analyze_command
from pattern_training.analyzer import run_analysis as shared_run_analysis
from pattern_training.board9 import board_hash
from pattern_training.root_candidates import (
    RootAnalysis,
    RootCandidate,
    parse_analysis_stdout as parse_root_analysis_stdout,
)

FAMILY_ALIASES: dict[str, tuple[str, ...]] = {
    **COMMON_FAMILY_ALIASES,
    "corner_only": ("corner_3x3",),
    "edge_context_only": ("edge_x_10",),
}


Candidate = RootCandidate


@dataclass(frozen=True)
class AnalyzeResult:
    candidates: tuple[Candidate, ...]
    best_move: str | None = None
    root_scores: dict[str, int] = field(default_factory=dict)
    stdout: str = ""


@dataclass(frozen=True)
class SplitRatios:
    train: int
    validation: int
    holdout: int

    @property
    def total(self) -> int:
        return self.train + self.validation + self.holdout


def normalize_move(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    return text or None


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


def parse_label_paths(value: str, *, dataset_root: str | None = None) -> list[Path]:
    return resolve_path_references(
        parse_csv_values(value, error_label="label path list"),
        explicit_root=dataset_root,
    )


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as input_file:
        for line_number, line in enumerate(input_file, start=1):
            line = line.strip()
            if not line:
                continue
            record = json.loads(line)
            if not isinstance(record, dict):
                raise ScriptError(f"{path}:{line_number}: expected JSON object")
            rows.append(record)
    return rows


def board_key(board_text: str) -> str:
    return "\n".join(line.rstrip() for line in board_text.strip().splitlines())


def parse_split_ratios(text: str) -> SplitRatios:
    parts = text.split(",")
    if len(parts) != 3:
        raise ScriptError("--split-ratios must be TRAIN,VALIDATION,HOLDOUT")
    try:
        train, validation, holdout = (int(part) for part in parts)
    except ValueError as exc:
        raise ScriptError("--split-ratios must contain integers") from exc
    if train < 0 or validation < 0 or holdout < 0:
        raise ScriptError("--split-ratios cannot contain negative values")
    if train == 0 or validation == 0 or holdout == 0:
        raise ScriptError("--split-ratios must keep all three splits non-empty")
    return SplitRatios(train=train, validation=validation, holdout=holdout)


def parse_families(text: str) -> tuple[str, ...]:
    families: list[str] = []
    for raw_part in text.split(","):
        part = raw_part.strip()
        if not part:
            continue
        expanded = FAMILY_ALIASES.get(part, (part,))
        for family in expanded:
            if family not in PATTERN_SPECS:
                raise ScriptError(f"unknown pattern family: {family}")
            if family not in families:
                families.append(family)
    if not families:
        raise ScriptError("--families selected no pattern families")
    return tuple(families)


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


def split_name_for_row(row: dict[str, Any], ratios: SplitRatios, seed: int) -> str:
    board = row.get("board_text")
    if not isinstance(board, str):
        board = str(row.get("board") or "")
    move = normalize_move(row.get("move")) or ""
    material = f"{seed}\n{board_key(board)}\n{move}".encode("utf-8")
    bucket = int.from_bytes(hashlib.sha256(material).digest()[:8], "big") % ratios.total
    if bucket < ratios.train:
        return "train"
    if bucket < ratios.train + ratios.validation:
        return "validation"
    return "holdout"


def write_split_files(rows: list[dict[str, Any]], out_dir: Path, ratios: SplitRatios, seed: int) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    split_rows: dict[str, list[dict[str, Any]]] = {
        "train": [],
        "validation": [],
        "holdout": [],
    }
    for row in rows:
        split_rows[split_name_for_row(row, ratios, seed)].append(row)
    for split_name, records in split_rows.items():
        path = out_dir / f"teacher_{split_name}.jsonl"
        with path.open("w", encoding="utf-8") as output:
            for record in records:
                output.write(json.dumps(record, sort_keys=True) + "\n")


def empty_count(board_text: str) -> int:
    rows, _ = parse_board(board_text)
    return sum(row.count(".") for row in rows)


def load_exact_best(paths: list[Path]) -> dict[str, set[str]]:
    exact: dict[str, set[str]] = {}
    for path in paths:
        for record in read_jsonl(path):
            board = record.get("board")
            if not isinstance(board, str):
                continue
            best = record.get("best_moves")
            if isinstance(best, list):
                exact[board_key(board)] = {str(move).lower() for move in best}
            else:
                move = normalize_move(record.get("best_move"))
                exact[board_key(board)] = {move} if move else set()
    return exact


def accepted_teacher_rows(paths: list[Path], limit: int | None) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in paths:
        for record in read_jsonl(path):
            if (
                record.get("status") == "ok"
                and record.get("legal_move_valid") is True
                and record.get("move_token_valid") is True
                and normalize_move(record.get("move")) is not None
            ):
                rows.append(record)
                if limit is not None and len(rows) >= limit:
                    return rows
    return rows


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


def parse_board(board_text: str) -> tuple[list[str], str]:
    lines = [line.strip() for line in board_text.strip().splitlines() if line.strip()]
    if len(lines) != 9:
        raise ScriptError("expected board9 text with 8 rows plus side")
    side_line = lines[8]
    if not side_line.startswith("side=") or len(side_line) != 6:
        raise ScriptError("expected board9 side line")
    return lines[:8], side_line[-1]


def pattern_indexes(board_text: str, side: str, specs: tuple[tuple[int, ...], ...]) -> list[int]:
    rows, _ = parse_board(board_text)
    square_index_rows = board9_rows_to_square_index_rows(rows)
    return [pattern_index(square_index_rows, side, spec) for spec in specs]


def pattern_indexes_by_family(board_text: str, side: str, families: tuple[str, ...]) -> dict[str, list[int]]:
    return {
        family: pattern_indexes(board_text, side, PATTERN_SPECS[family])
        for family in families
    }


def apply_preference_update(
    *,
    board_text: str,
    teacher_child_board: str,
    compared_child_board: str,
    family_counts: dict[str, collections.Counter[int]],
    families: tuple[str, ...],
) -> None:
    _, side = parse_board(board_text)
    teacher_indexes = pattern_indexes_by_family(teacher_child_board, side, families)
    compared_indexes = pattern_indexes_by_family(compared_child_board, side, families)
    for family in families:
        family_counts[family].update(teacher_indexes[family])
        family_counts[family].subtract(compared_indexes[family])


def swapped_index(index: int, cells: int) -> int:
    swapped = 0
    place = 1
    for _ in range(cells):
        state = index % 3
        index //= 3
        if state == 1:
            state = 2
        elif state == 2:
            state = 1
        swapped += state * place
        place *= 3
    return swapped


def clamp(value: int, maximum: int) -> int:
    return max(-maximum, min(maximum, value))


def sparse_entries(
    counts: collections.Counter[int],
    *,
    cells: int,
    limit_pairs: int,
    min_abs_diff: int,
    scale: int,
    max_abs_weight: int,
) -> list[tuple[int, int]]:
    pairs: list[tuple[int, int, int]] = []
    visited: set[int] = set()
    for index in set(counts):
        if index in visited or index == 0:
            continue
        partner = swapped_index(index, cells)
        visited.add(index)
        visited.add(partner)
        if index > partner:
            continue
        diff = counts[index] - counts[partner]
        if abs(diff) < min_abs_diff:
            continue
        weight = clamp(round(diff / scale), max_abs_weight)
        if weight != 0:
            pairs.append((abs(diff), index, weight))

    pairs.sort(reverse=True)
    entries: list[tuple[int, int]] = []
    for _, index, weight in pairs[:limit_pairs]:
        partner = swapped_index(index, cells)
        entries.append((index, weight))
        if partner != index:
            entries.append((partner, -weight))
    return sorted(entries)


def render_table(
    *,
    name: str = "pattern_teacher_v0",
    corner_entries: list[tuple[int, int]] | None = None,
    edge_entries: list[tuple[int, int]] | None = None,
    family_entries: dict[str, list[tuple[int, int]]] | None = None,
    stats: dict[str, int],
    command: list[str],
) -> str:
    entries_by_family: dict[str, list[tuple[int, int]]] = {}
    if family_entries is not None:
        entries_by_family.update(family_entries)
    if corner_entries is not None:
        entries_by_family["corner_2x3"] = corner_entries
    if edge_entries is not None:
        entries_by_family["edge_8"] = edge_entries

    lines = [
        "# schema_version: pattern_table.v1",
        f"# name: {name}",
        "# generated_by: tools/scripts/pattern_teacher_v0_train.py",
        f"# command: {quote_command(command)}",
    ]
    for key in sorted(stats):
        lines.append(f"# {key}: {stats[key]}")
    lines.append("")
    for family in FAMILY_ORDER:
        lines.extend(
            f"{family}\t{index}\t{value}"
            for index, value in entries_by_family.get(family, [])
        )
    return "\n".join(lines) + "\n"


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
