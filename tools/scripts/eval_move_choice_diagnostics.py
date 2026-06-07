#!/usr/bin/env python3
"""Diagnose evaluator move choice, ties, and teacher/exact ranks with batch analysis."""

from __future__ import annotations

import argparse
import collections
import json
import sys
import time
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from common import ScriptError, parse_csv_values, quote_command, read_jsonl
from dataset_paths import resolve_path_references
from pattern_training.analyzer import (
    AnalyzerConfig,
    run_batch_analysis,
    run_parallel_batch_analysis,
)
from pattern_training.board9 import board_key
from pattern_training.root_candidates import RootAnalysis, normalize_move


PHASES = ("opening", "midgame", "late")
SPLITS = ("train", "validation", "holdout")


@dataclass(frozen=True)
class Target:
    label: str
    eval_config: Path


@dataclass(frozen=True)
class LabelRow:
    position_id: str
    split: str
    board_text: str
    teacher_move: str
    exact_best_moves: tuple[str, ...] = ()
    exact_scores: dict[str, int] = field(default_factory=dict)


@dataclass
class BucketStats:
    rows: int = 0
    selected_teacher_agreements: int = 0
    teacher_rank_sum: int = 0
    teacher_ranked_rows: int = 0
    teacher_rank1: int = 0
    teacher_rank2: int = 0
    teacher_rank3: int = 0
    teacher_missing_rank: int = 0
    top_group_size_sum: int = 0
    top_group_tie_rows: int = 0
    teacher_in_top_group: int = 0
    selected_teacher_score_ties: int = 0
    selected_minus_teacher_margin_sum: int = 0
    exact_rows: int = 0
    exact_best_rank_sum: int = 0
    exact_best_ranked_rows: int = 0
    exact_best_top_group_hits: int = 0
    exact_best_missing_rank: int = 0

    def update(self, row: LabelRow, analysis: RootAnalysis) -> None:
        scores = analysis.root_scores
        selected = analysis.best_move
        self.rows += 1
        if selected == row.teacher_move:
            self.selected_teacher_agreements += 1

        if scores:
            top_score = max(scores.values())
            top_group = {move for move, score in scores.items() if score == top_score}
            self.top_group_size_sum += len(top_group)
            if len(top_group) > 1:
                self.top_group_tie_rows += 1
            if row.teacher_move in top_group:
                self.teacher_in_top_group += 1

        teacher_score = scores.get(row.teacher_move)
        selected_score = scores.get(selected) if selected is not None else None
        teacher_rank = rank_for_move(scores, row.teacher_move)
        if teacher_rank is None:
            self.teacher_missing_rank += 1
        else:
            self.teacher_ranked_rows += 1
            self.teacher_rank_sum += teacher_rank
            if teacher_rank == 1:
                self.teacher_rank1 += 1
            if teacher_rank <= 2:
                self.teacher_rank2 += 1
            if teacher_rank <= 3:
                self.teacher_rank3 += 1
        if teacher_score is not None and selected_score is not None:
            if teacher_score == selected_score:
                self.selected_teacher_score_ties += 1
            self.selected_minus_teacher_margin_sum += selected_score - teacher_score

        if row.exact_best_moves:
            self.exact_rows += 1
            exact_rank = best_rank_for_moves(scores, row.exact_best_moves)
            if exact_rank is None:
                self.exact_best_missing_rank += 1
            else:
                self.exact_best_ranked_rows += 1
                self.exact_best_rank_sum += exact_rank
            if scores:
                top_score = max(scores.values())
                if any(scores.get(move) == top_score for move in row.exact_best_moves):
                    self.exact_best_top_group_hits += 1

    def as_dict(self) -> dict[str, Any]:
        rows = self.rows
        ranked = self.teacher_ranked_rows
        exact_ranked = self.exact_best_ranked_rows
        exact_rows = self.exact_rows
        return {
            "rows": rows,
            "selected_teacher_agreements": self.selected_teacher_agreements,
            "selected_teacher_agreement_rate": rate(self.selected_teacher_agreements, rows),
            "avg_teacher_rank": rate(self.teacher_rank_sum, ranked),
            "teacher_rank_sum": self.teacher_rank_sum,
            "teacher_ranked_rows": ranked,
            "teacher_rank1": self.teacher_rank1,
            "teacher_rank2_or_better": self.teacher_rank2,
            "teacher_rank3_or_better": self.teacher_rank3,
            "teacher_missing_rank": self.teacher_missing_rank,
            "avg_top_group_size": rate(self.top_group_size_sum, rows),
            "top_group_tie_rows": self.top_group_tie_rows,
            "top_group_tie_rate": rate(self.top_group_tie_rows, rows),
            "teacher_in_top_group": self.teacher_in_top_group,
            "teacher_in_top_group_rate": rate(self.teacher_in_top_group, rows),
            "selected_teacher_score_ties": self.selected_teacher_score_ties,
            "selected_teacher_score_tie_rate": rate(self.selected_teacher_score_ties, rows),
            "avg_selected_minus_teacher_margin": rate(
                self.selected_minus_teacher_margin_sum, rows
            ),
            "exact_rows": exact_rows,
            "exact_best_rank_sum": self.exact_best_rank_sum,
            "avg_exact_best_rank": rate(self.exact_best_rank_sum, exact_ranked),
            "exact_best_ranked_rows": exact_ranked,
            "exact_best_missing_rank": self.exact_best_missing_rank,
            "exact_best_top_group_hits": self.exact_best_top_group_hits,
            "exact_best_top_group_hit_rate": rate(
                self.exact_best_top_group_hits, exact_rows
            ),
        }


def rate(numerator: int | float, denominator: int | float) -> float | None:
    if denominator == 0:
        return None
    return numerator / denominator


def rank_for_move(scores: dict[str, int], move: str | None) -> int | None:
    if move is None or move not in scores:
        return None
    score = scores[move]
    return 1 + sum(1 for value in scores.values() if value > score)


def best_rank_for_moves(scores: dict[str, int], moves: Iterable[str]) -> int | None:
    ranks = [rank_for_move(scores, move) for move in moves]
    present = [rank for rank in ranks if rank is not None]
    return min(present) if present else None


def parse_target(value: str) -> Target:
    label, separator, path = value.partition("=")
    if not separator or not label or not path:
        raise argparse.ArgumentTypeError("expected LABEL=PATH")
    return Target(label=label, eval_config=Path(path))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Batch evaluator move-choice diagnostics against teacher/exact labels."
    )
    parser.add_argument("--teacher-labels", required=True, help="comma-separated teacher JSONL paths")
    parser.add_argument("--exact-labels", help="optional comma-separated exact-label JSONL paths")
    parser.add_argument(
        "--dataset-root",
        help="shared dataset root for dataset: references; overrides VIBE_OTHELLO_DATASET_ROOT",
    )
    parser.add_argument("--config", action="append", type=parse_target, required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument(
        "--analyze-position",
        default=str(Path("build") / "release" / "othello_analyze_position"),
    )
    parser.add_argument("--analysis-jobs", type=int, default=1)
    parser.add_argument("--depth", type=int, default=1)
    parser.add_argument("--limit", type=int)
    return parser.parse_args(argv)


def parse_label_paths(value: str | None, *, dataset_root: str | None) -> tuple[Path, ...]:
    if not value:
        return ()
    return tuple(
        resolve_path_references(
            parse_csv_values(value, error_label="label path list"),
            explicit_root=dataset_root,
        )
    )


def exact_best_moves(record: dict[str, Any] | None) -> tuple[str, ...]:
    if record is None:
        return ()
    moves = record.get("best_moves")
    if isinstance(moves, list):
        return tuple(sorted(move for move in (normalize_move(value) for value in moves) if move))
    move = normalize_move(record.get("best_move"))
    return (move,) if move else ()


def exact_score_map(record: dict[str, Any] | None) -> dict[str, int]:
    if record is None:
        return {}
    scores: dict[str, int] = {}
    move_scores = record.get("move_scores")
    if not isinstance(move_scores, list):
        return scores
    for entry in move_scores:
        if not isinstance(entry, dict):
            continue
        move = normalize_move(entry.get("move"))
        score = entry.get("exact_score_side_to_move")
        if move is not None and isinstance(score, int):
            scores[move] = score
    return scores


def load_exact_by_board(paths: tuple[Path, ...]) -> dict[str, dict[str, Any]]:
    exact: dict[str, dict[str, Any]] = {}
    for path in paths:
        for record in read_jsonl(path, require_object=True):
            board = record.get("board")
            if isinstance(board, str):
                exact[board_key(board)] = record
    return exact


def load_label_rows(
    teacher_paths: tuple[Path, ...],
    *,
    exact_by_board: dict[str, dict[str, Any]],
    limit: int | None,
) -> list[LabelRow]:
    rows: list[LabelRow] = []
    for path in teacher_paths:
        for record in read_jsonl(path, require_object=True):
            if record.get("status") != "ok" or record.get("legal_move_valid") is not True:
                continue
            board_text = record.get("board_text")
            teacher_move = normalize_move(record.get("move") or record.get("teacher_move"))
            if not isinstance(board_text, str) or teacher_move is None:
                continue
            exact = exact_by_board.get(board_key(board_text))
            rows.append(
                LabelRow(
                    position_id=str(record.get("position_id") or f"row-{len(rows)}"),
                    split=str(record.get("position_split") or "unknown"),
                    board_text=board_text,
                    teacher_move=teacher_move,
                    exact_best_moves=exact_best_moves(exact),
                    exact_scores=exact_score_map(exact),
                )
            )
            if limit is not None and len(rows) >= limit:
                return rows
    return rows


def occupied_count(board_text: str) -> int:
    rows = [line.strip() for line in board_text.splitlines() if line.strip()]
    return sum(1 for line in rows[:8] for cell in line if cell in ("B", "W"))


def phase_for_board(board_text: str) -> str:
    occupied = occupied_count(board_text)
    if occupied <= 20:
        return "opening"
    if occupied <= 44:
        return "midgame"
    return "late"


def empties_bucket(board_text: str) -> str:
    empties = 64 - occupied_count(board_text)
    if empties <= 12:
        return "00-12"
    if empties <= 24:
        return "13-24"
    if empties <= 36:
        return "25-36"
    if empties <= 48:
        return "37-48"
    return "49-60"


def move_family(move: str | None) -> str:
    if move is None:
        return "none"
    if move == "pass":
        return "pass"
    corners = {"a1", "h1", "a8", "h8"}
    x_squares = {"b2", "g2", "b7", "g7"}
    c_squares = {"b1", "g1", "a2", "h2", "a7", "h7", "b8", "g8"}
    if move in corners:
        return "corner"
    if move in x_squares:
        return "x-square"
    if move in c_squares:
        return "c-square"
    file = move[0]
    rank = move[1:]
    if file in ("a", "h") or rank in ("1", "8"):
        return "edge"
    return "interior"


def summarize_target(rows: list[LabelRow], analyses: dict[str, RootAnalysis]) -> dict[str, Any]:
    overall = BucketStats()
    by_split: dict[str, BucketStats] = collections.defaultdict(BucketStats)
    by_phase: dict[str, BucketStats] = collections.defaultdict(BucketStats)
    by_empties: dict[str, BucketStats] = collections.defaultdict(BucketStats)
    by_teacher_family: dict[str, BucketStats] = collections.defaultdict(BucketStats)
    by_selected_family: dict[str, BucketStats] = collections.defaultdict(BucketStats)

    for row in rows:
        analysis = analyses[row.position_id]
        buckets = (
            overall,
            by_split[row.split],
            by_phase[phase_for_board(row.board_text)],
            by_empties[empties_bucket(row.board_text)],
            by_teacher_family[move_family(row.teacher_move)],
            by_selected_family[move_family(analysis.best_move)],
        )
        for bucket in buckets:
            bucket.update(row, analysis)

    def render(mapping: dict[str, BucketStats]) -> dict[str, Any]:
        return {key: mapping[key].as_dict() for key in sorted(mapping)}

    return {
        "overall": overall.as_dict(),
        "by_split": render(by_split),
        "by_phase": render(by_phase),
        "by_empties": render(by_empties),
        "by_teacher_move_family": render(by_teacher_family),
        "by_selected_move_family": render(by_selected_family),
    }


def run_target(
    *,
    target: Target,
    rows: list[LabelRow],
    analyze_position: Path,
    depth: int,
    jobs: int,
) -> dict[str, Any]:
    start = time.perf_counter()
    config = AnalyzerConfig(analyze_position=analyze_position, eval_config=target.eval_config, depth=depth)
    requests = ((row.position_id, row.board_text) for row in rows)
    runner = (
        run_parallel_batch_analysis(config, requests, jobs=jobs)
        if jobs > 1
        else run_batch_analysis(config, requests)
    )
    analyses: dict[str, RootAnalysis] = {}
    for position_id, analysis in runner:
        analyses[position_id] = analysis
    missing = [row.position_id for row in rows if row.position_id not in analyses]
    if missing:
        raise ScriptError(f"{target.label}: missing {len(missing)} analysis rows")
    summary = summarize_target(rows, analyses)
    summary["elapsed_seconds"] = round(time.perf_counter() - start, 6)
    summary["eval_config"] = str(target.eval_config)
    return summary


def render_markdown(results: dict[str, Any], invocation: list[str]) -> str:
    lines = [
        "# Evaluator Move Choice Diagnostics",
        "",
        "This is diagnostic move-choice evidence. It is not an Elo estimate or a default-promotion recommendation.",
        "",
        "## Command",
        "",
        f"`{quote_command(invocation)}`",
        "",
        "## Overall",
        "",
        "| config | rows | selected teacher agreement | avg teacher rank | rank sum | rank1 | rank2+ | rank3+ | avg top group | top tie rate | teacher in top group | exact-best top group | avg exact-best rank |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for label, target in results["targets"].items():
        row = target["overall"]
        lines.append(
            "| "
            f"{label} | {row['rows']} | {fmt_pct(row['selected_teacher_agreement_rate'])} | "
            f"{fmt_float(row['avg_teacher_rank'])} | {row['teacher_rank_sum']} | "
            f"{row['teacher_rank1']} | {row['teacher_rank2_or_better']} | "
            f"{row['teacher_rank3_or_better']} | {fmt_float(row['avg_top_group_size'])} | "
            f"{fmt_pct(row['top_group_tie_rate'])} | {fmt_pct(row['teacher_in_top_group_rate'])} | "
            f"{fmt_pct(row['exact_best_top_group_hit_rate'])} | {fmt_float(row['avg_exact_best_rank'])} |"
        )
    append_bucket_section(lines, results, "Splits", "by_split", "split")
    append_bucket_section(lines, results, "Phases", "by_phase", "phase")
    append_bucket_section(lines, results, "Empties Buckets", "by_empties", "empties")
    append_bucket_section(
        lines,
        results,
        "Teacher Move Families",
        "by_teacher_move_family",
        "teacher family",
    )
    append_bucket_section(
        lines,
        results,
        "Selected Move Families",
        "by_selected_move_family",
        "selected family",
    )
    return "\n".join(lines) + "\n"


def append_bucket_section(
    lines: list[str],
    results: dict[str, Any],
    title: str,
    key: str,
    label_name: str,
) -> None:
    lines.extend(["", f"## {title}", ""])
    for label, target in results["targets"].items():
        lines.extend([f"### {label}", ""])
        lines.append(
            f"| {label_name} | rows | selected agreement | avg teacher rank | "
            "rank1 | rank2+ | rank3+ | avg top group | teacher top group | "
            "exact-best top group |"
        )
        lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
        for bucket, row in target[key].items():
            lines.append(
                f"| {bucket} | {row['rows']} | {fmt_pct(row['selected_teacher_agreement_rate'])} | "
                f"{fmt_float(row['avg_teacher_rank'])} | {row['teacher_rank1']} | "
                f"{row['teacher_rank2_or_better']} | {row['teacher_rank3_or_better']} | "
                f"{fmt_float(row['avg_top_group_size'])} | "
                f"{fmt_pct(row['teacher_in_top_group_rate'])} | "
                f"{fmt_pct(row['exact_best_top_group_hit_rate'])} |"
            )
        lines.append("")


def fmt_float(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.3f}"


def fmt_pct(value: float | None) -> str:
    return "n/a" if value is None else f"{value * 100:.2f}%"


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        teacher_paths = parse_label_paths(args.teacher_labels, dataset_root=args.dataset_root)
        exact_paths = parse_label_paths(args.exact_labels, dataset_root=args.dataset_root)
        exact_by_board = load_exact_by_board(exact_paths)
        rows = load_label_rows(teacher_paths, exact_by_board=exact_by_board, limit=args.limit)
        targets: dict[str, Any] = {}
        for target in args.config:
            targets[target.label] = run_target(
                target=target,
                rows=rows,
                analyze_position=Path(args.analyze_position),
                depth=args.depth,
                jobs=max(1, args.analysis_jobs),
            )
        output = {
            "script": "tools/scripts/eval_move_choice_diagnostics.py",
            "command": quote_command(
                ["tools/scripts/eval_move_choice_diagnostics.py", *(argv or sys.argv[1:])]
            ),
            "rows": len(rows),
            "targets": targets,
        }
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        markdown_path = out_path.with_suffix(".md")
        markdown_path.write_text(
            render_markdown(output, ["tools/scripts/eval_move_choice_diagnostics.py", *(argv or sys.argv[1:])]),
            encoding="utf-8",
        )
        print(f"wrote {out_path} {markdown_path}")
        return 0
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
