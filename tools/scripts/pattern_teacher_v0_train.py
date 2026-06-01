#!/usr/bin/env python3
"""Learn a small sparse pattern table from validated teacher residuals."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from common import ScriptError, parse_csv_values, quote_command
from dataset_paths import resolve_path_references


CORNER_SPECS = (
    (0, 1, 2, 8, 9, 10),
    (7, 6, 5, 15, 14, 13),
    (56, 57, 58, 48, 49, 50),
    (63, 62, 61, 55, 54, 53),
)
EDGE_SPECS = (
    (0, 1, 2, 3, 4, 5, 6, 7),
    (56, 57, 58, 59, 60, 61, 62, 63),
    (0, 8, 16, 24, 32, 40, 48, 56),
    (7, 15, 23, 31, 39, 47, 55, 63),
)


@dataclass(frozen=True)
class Candidate:
    move: str
    score: int | None
    child_board: str | None


@dataclass(frozen=True)
class AnalyzeResult:
    candidates: tuple[Candidate, ...]


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
    parser.add_argument("--out", required=True, help="output pattern table TSV")
    parser.add_argument("--table-name", default="pattern_teacher_v0")
    parser.add_argument("--exact-labels", help="optional comma-separated exact labels for filtering")
    parser.add_argument("--limit", type=int, help="optional accepted teacher row limit")
    parser.add_argument("--corner-pairs", type=int, default=32)
    parser.add_argument("--edge-pairs", type=int, default=32)
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
    return [
        args.analyze_position,
        "--stdin",
        "--depth",
        str(args.depth),
        "--exact-endgame-threshold",
        "0",
        "--eval-config",
        args.eval_config,
        "--root-candidates",
    ]


def parse_analysis_stdout(text: str) -> AnalyzeResult:
    candidates: list[Candidate] = []
    current_move: str | None = None
    current_score: int | None = None
    child_lines: list[str] | None = None

    def finish_candidate() -> None:
        nonlocal current_move, current_score, child_lines
        if current_move is not None:
            child_board = "\n".join(child_lines) if child_lines else None
            candidates.append(Candidate(move=current_move, score=current_score, child_board=child_board))
        current_move = None
        current_score = None
        child_lines = None

    for raw_line in text.splitlines():
        if raw_line.startswith("  - move:"):
            finish_candidate()
            current_move = normalize_move(raw_line.split(":", 1)[1]) or "pass"
            continue
        if current_move is None:
            continue
        if raw_line == "    child_board:":
            child_lines = []
            continue
        if child_lines is not None and len(child_lines) < 9 and raw_line.startswith("      "):
            child_lines.append(raw_line[6:])
            continue
        if raw_line.startswith("    score:"):
            try:
                current_score = int(raw_line.split(":", 1)[1].strip())
            except ValueError:
                current_score = None
    finish_candidate()

    return AnalyzeResult(candidates=tuple(candidates))


def run_analysis(args: argparse.Namespace, board_text: str) -> AnalyzeResult:
    command = analyze_command(args)
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


def parse_board(board_text: str) -> tuple[list[str], str]:
    lines = [line.strip() for line in board_text.strip().splitlines() if line.strip()]
    if len(lines) != 9:
        raise ScriptError("expected board9 text with 8 rows plus side")
    side_line = lines[8]
    if not side_line.startswith("side=") or len(side_line) != 6:
        raise ScriptError("expected board9 side line")
    return lines[:8], side_line[-1]


def board_cell(rows: list[str], square_index: int) -> str:
    return rows[square_index // 8][square_index % 8]


def pattern_index(rows: list[str], side: str, spec: tuple[int, ...]) -> int:
    opponent = "W" if side == "B" else "B"
    index = 0
    place = 1
    for square_index in spec:
        cell = board_cell(rows, square_index)
        state = 1 if cell == side else 2 if cell == opponent else 0
        index += state * place
        place *= 3
    return index


def pattern_indexes(board_text: str, side: str) -> tuple[list[int], list[int]]:
    rows, _ = parse_board(board_text)
    corners = [pattern_index(rows, side, spec) for spec in CORNER_SPECS]
    edges = [pattern_index(rows, side, spec) for spec in EDGE_SPECS]
    return corners, edges


def apply_preference_update(
    *,
    board_text: str,
    teacher_child_board: str,
    compared_child_board: str,
    corner_counts: collections.Counter[int],
    edge_counts: collections.Counter[int],
) -> None:
    _, side = parse_board(board_text)
    teacher_corners, teacher_edges = pattern_indexes(teacher_child_board, side)
    compared_corners, compared_edges = pattern_indexes(compared_child_board, side)
    corner_counts.update(teacher_corners)
    corner_counts.subtract(compared_corners)
    edge_counts.update(teacher_edges)
    edge_counts.subtract(compared_edges)


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
    corner_entries: list[tuple[int, int]],
    edge_entries: list[tuple[int, int]],
    stats: dict[str, int],
    command: list[str],
) -> str:
    lines = [
        "# schema_version: pattern_table.v1",
        f"# name: {name}",
        "# generated_by: tools/scripts/pattern_teacher_v0_train.py",
        f"# command: {quote_command(command)}",
    ]
    for key in sorted(stats):
        lines.append(f"# {key}: {stats[key]}")
    lines.append("")
    lines.extend(f"corner_2x3\t{index}\t{value}" for index, value in corner_entries)
    lines.extend(f"edge_8\t{index}\t{value}" for index, value in edge_entries)
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
    if args.empty_min is not None and args.empty_max is not None and args.empty_min > args.empty_max:
        raise ScriptError("--empty-min cannot be greater than --empty-max")
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

    corner_counts: collections.Counter[int] = collections.Counter()
    edge_counts: collections.Counter[int] = collections.Counter()
    stats = collections.Counter[str]()
    stats["accepted_teacher_rows"] = len(rows)
    stats["split_seed"] = args.split_seed
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

        analysis = run_analysis(args, board_text)
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
                            corner_counts=corner_counts,
                            edge_counts=edge_counts,
                        )
                        rank_updates += 1
            if rank_updates == 0:
                apply_preference_update(
                    board_text=board_text,
                    teacher_child_board=teacher.child_board,
                    compared_child_board=selected.child_board,
                    corner_counts=corner_counts,
                    edge_counts=edge_counts,
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
                corner_counts=corner_counts,
                edge_counts=edge_counts,
            )
            stats["residual_updates"] += 1

    corner_entries = sparse_entries(
        corner_counts,
        cells=6,
        limit_pairs=args.corner_pairs,
        min_abs_diff=args.min_abs_diff,
        scale=args.scale,
        max_abs_weight=args.max_abs_weight,
    )
    edge_entries = sparse_entries(
        edge_counts,
        cells=8,
        limit_pairs=args.edge_pairs,
        min_abs_diff=args.min_abs_diff,
        scale=args.scale,
        max_abs_weight=args.max_abs_weight,
    )
    stats["corner_entries"] = len(corner_entries)
    stats["edge_entries"] = len(edge_entries)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        render_table(
            name=args.table_name,
            corner_entries=corner_entries,
            edge_entries=edge_entries,
            stats=dict(stats),
            command=["tools/scripts/pattern_teacher_v0_train.py", *(argv or sys.argv[1:])],
        ),
        encoding="utf-8",
    )
    print(f"wrote {out} corner_entries={len(corner_entries)} edge_entries={len(edge_entries)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(exc.exit_code)
