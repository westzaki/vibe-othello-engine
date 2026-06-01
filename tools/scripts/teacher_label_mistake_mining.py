#!/usr/bin/env python3
"""Mine evaluator move-choice mistakes against validated teacher labels."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from common import ScriptError, quote_command, read_jsonl
from dataset_paths import resolve_path_references
from eval_config_tuner import sha256_file


@dataclass(frozen=True)
class EvalTarget:
    label: str
    path: Path


@dataclass(frozen=True)
class AnalyzeResult:
    best_move: str | None
    legal_moves: list[str]
    phase: str | None
    occupied_count: int | None
    empty_count: int | None
    root_scores: dict[str, int]


@dataclass(frozen=True)
class MiningConfig:
    teacher_labels: tuple[Path, ...]
    exact_labels: tuple[Path, ...]
    targets: tuple[EvalTarget, ...]
    out_dir: Path
    build_dir: Path
    analyze_position: Path
    depth: int
    exact_endgame_threshold: int
    limit: int | None
    dry_run: bool
    allow_failures: bool
    invocation: list[str]


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


def parse_target(value: str) -> EvalTarget:
    label, separator, path = value.partition("=")
    if not separator or not label or not path:
        raise argparse.ArgumentTypeError("expected LABEL=PATH")
    return EvalTarget(label=label, path=Path(path))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare evaluator move choices with validated teacher-label JSONL."
    )
    parser.add_argument("--teacher-labels", nargs="+", required=True)
    parser.add_argument("--exact-labels", nargs="*", default=[])
    parser.add_argument(
        "--dataset-root",
        help="shared dataset root for dataset: references; overrides VIBE_OTHELLO_DATASET_ROOT",
    )
    parser.add_argument("--config", action="append", type=parse_target, required=True)
    parser.add_argument("--out", required=True, help="output directory, normally under runs/")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--analyze-position", help="override othello_analyze_position path")
    parser.add_argument("--depth", type=parse_non_negative_int, default=1)
    parser.add_argument("--exact-endgame-threshold", type=parse_non_negative_int, default=0)
    parser.add_argument("--limit", type=parse_positive_int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-failures", action="store_true")
    return parser.parse_args(argv)


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> MiningConfig:
    build_dir = Path(args.build_dir)
    return MiningConfig(
        teacher_labels=tuple(
            resolve_path_references(args.teacher_labels, explicit_root=args.dataset_root)
        ),
        exact_labels=tuple(
            resolve_path_references(args.exact_labels, explicit_root=args.dataset_root)
        ),
        targets=tuple(args.config),
        out_dir=Path(args.out),
        build_dir=build_dir,
        analyze_position=Path(args.analyze_position)
        if args.analyze_position
        else build_dir / "othello_analyze_position",
        depth=args.depth,
        exact_endgame_threshold=args.exact_endgame_threshold,
        limit=args.limit,
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        invocation=invocation or [],
    )


def normalize_move(move: Any) -> str | None:
    if move is None:
        return None
    text = str(move).strip()
    return text.lower() if text else None


def board_key(board_text: str) -> str:
    return "\n".join(line.rstrip() for line in board_text.strip().splitlines())


def load_teacher_rows(paths: tuple[Path, ...], limit: int | None) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in paths:
        for record in read_jsonl(path, require_object=True):
            if record.get("status") == "ok" and record.get("legal_move_valid") is True:
                row = dict(record)
                row["_teacher_labels_path"] = str(path)
                rows.append(row)
                if limit is not None and len(rows) >= limit:
                    return rows
    return rows


def load_exact_by_board(paths: tuple[Path, ...]) -> dict[str, dict[str, Any]]:
    exact: dict[str, dict[str, Any]] = {}
    for path in paths:
        for record in read_jsonl(path, require_object=True):
            board = record.get("board")
            if isinstance(board, str):
                normalized = dict(record)
                normalized["_exact_labels_path"] = str(path)
                exact[board_key(board)] = normalized
    return exact


def parse_int_value(value: str) -> int | None:
    try:
        return int(value)
    except ValueError:
        return None


def parse_analysis_stdout(text: str) -> AnalyzeResult:
    best_move: str | None = None
    legal_moves: list[str] = []
    phase: str | None = None
    occupied_count: int | None = None
    empty_count: int | None = None
    root_scores: dict[str, int] = {}
    current_candidate: str | None = None
    in_root_candidates = False
    in_evaluation_breakdown = False

    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped:
            continue
        if line == "root_candidates:":
            in_root_candidates = True
            in_evaluation_breakdown = False
            current_candidate = None
            continue
        if line == "evaluation_breakdown:":
            in_evaluation_breakdown = True
            in_root_candidates = False
            current_candidate = None
            continue
        if not line.startswith(" ") and stripped.endswith(":"):
            in_root_candidates = False
            in_evaluation_breakdown = False
            current_candidate = None

        if not line.startswith(" "):
            key, separator, value = stripped.partition(":")
            if separator and key == "best_move":
                best_move = normalize_move(value)
            elif separator and key == "legal_moves":
                legal_moves = [part.lower() for part in value.split()]
            continue

        if in_evaluation_breakdown and line.startswith("  "):
            key, separator, value = stripped.partition(":")
            if not separator:
                continue
            if key == "phase":
                phase = value.strip()
            elif key == "occupied_count":
                occupied_count = parse_int_value(value.strip())
            elif key == "empty_count":
                empty_count = parse_int_value(value.strip())

        if in_root_candidates:
            if line.startswith("  - move:"):
                current_candidate = normalize_move(stripped.split(":", 1)[1])
            elif current_candidate is not None and line.startswith("    score:"):
                score = parse_int_value(stripped.split(":", 1)[1].strip())
                if score is not None:
                    root_scores[current_candidate] = score

    return AnalyzeResult(
        best_move=best_move,
        legal_moves=legal_moves,
        phase=phase,
        occupied_count=occupied_count,
        empty_count=empty_count,
        root_scores=root_scores,
    )


def analyze_command(config: MiningConfig, target: EvalTarget) -> list[str]:
    return [
        str(config.analyze_position),
        "--stdin",
        "--depth",
        str(config.depth),
        "--exact-endgame-threshold",
        str(config.exact_endgame_threshold),
        "--eval-config",
        str(target.path),
        "--root-candidates",
    ]


def run_analysis(config: MiningConfig, target: EvalTarget, board_text: str) -> AnalyzeResult:
    command = analyze_command(config, target)
    completed = subprocess.run(
        command,
        input=board_text,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise ScriptError(
            f"analysis failed for {target.label}: {quote_command(command)}\n{completed.stderr}",
            exit_code=1,
        )
    return parse_analysis_stdout(completed.stdout)


def exact_best_moves(exact: dict[str, Any] | None) -> list[str]:
    if exact is None:
        return []
    moves = exact.get("best_moves")
    if isinstance(moves, list):
        return [str(move).lower() for move in moves]
    move = normalize_move(exact.get("best_move"))
    return [move] if move else []


def exact_score_map(exact: dict[str, Any] | None) -> dict[str, int]:
    if exact is None:
        return {}
    scores: dict[str, int] = {}
    for entry in exact.get("move_scores", []):
        if not isinstance(entry, dict):
            continue
        move = normalize_move(entry.get("move"))
        score = entry.get("exact_score_side_to_move")
        if move is not None and isinstance(score, int):
            scores[move] = score
    return scores


def rank_for_move(root_scores: dict[str, int], move: str | None) -> int | None:
    if move is None or move not in root_scores:
        return None
    score = root_scores[move]
    return 1 + sum(1 for value in root_scores.values() if value > score)


def exact_gap_for_move(scores: dict[str, int], move: str | None) -> int | None:
    if move is None or move not in scores:
        return None
    return max(scores.values()) - scores[move]


def cell(value: Any) -> Any:
    return "" if value is None else value


def classify_bucket(
    *,
    teacher_move: str | None,
    selected_move: str | None,
    exact_best: list[str],
    metadata: dict[str, Any],
    phase: str | None,
    empty_count: int | None,
) -> str:
    tags = str(metadata.get("tags", ""))
    if exact_best and teacher_move not in exact_best:
        return "teacher_exact_disagreement"
    if teacher_move == selected_move:
        return "unknown_or_needs_manual_review"
    if "corner_access" in tags or "corner_available" in tags:
        return "corner_access_miss"
    if "x_square" in tags:
        return "x_square_trap"
    if "frontier" in tags:
        return "frontier_over_penalty"
    if "edge_pattern" in tags or "edge_heavy" in tags:
        return "edge_greed"
    if "mobility" in tags:
        return "mobility_illusion"
    if "pass" in tags:
        return "pass_trap"
    if empty_count is not None and empty_count <= 12:
        return "late_disc_count_greed"
    if phase and "late" in phase:
        return "late_disc_count_greed"
    return "unknown_or_needs_manual_review"


def append_tsv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def git_sha() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def render_report(
    *,
    config: MiningConfig,
    rows: list[dict[str, Any]],
    summary_rows: list[dict[str, Any]],
    category_rows: list[dict[str, Any]],
) -> str:
    lines = [
        "# Teacher Label Mistake Mining",
        "",
        "Status: local analysis report. No strength claim. No default promotion.",
        "",
        "## Metadata",
        "",
        f"- generated_at: `{dt.datetime.now(dt.UTC).strftime('%Y-%m-%dT%H:%M:%SZ')}`",
        f"- git_sha: `{git_sha()}`",
        f"- command: `{quote_command(config.invocation) if config.invocation else 'unknown'}`",
        f"- teacher_labels: `{', '.join(str(path) for path in config.teacher_labels)}`",
        f"- exact_labels: `{', '.join(str(path) for path in config.exact_labels) if config.exact_labels else 'none'}`",
        f"- analyze_position: `{config.analyze_position}`",
        f"- depth: `{config.depth}`",
        f"- exact_endgame_threshold: `{config.exact_endgame_threshold}`",
        "",
        "## Configs",
        "",
        "| Label | Path | sha256 |",
        "|---|---|---|",
    ]
    for target in config.targets:
        lines.append(f"| `{target.label}` | `{target.path}` | `{sha256_file(target.path)}` |")

    lines.extend(
        [
            "",
            "## Agreement Summary",
            "",
            "| Config | Rows | Teacher Agreements | Exact Rows | Exact Agreements | Teacher Move Rank Sum | Exact-Best Rank Sum |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for row in summary_rows:
        lines.append(
            f"| `{row['config']}` | {row['rows']} | {row['teacher_agreements']} | "
            f"{row['exact_rows']} | {row['exact_agreements']} | "
            f"{row['teacher_rank_sum']} | {row['exact_best_rank_sum']} |"
        )

    lines.extend(
        [
            "",
            "## Top Buckets",
            "",
            "| Config | Bucket | Count |",
            "|---|---|---:|",
        ]
    )
    for row in category_rows:
        lines.append(f"| `{row['config']}` | `{row['bucket']}` | {row['count']} |")

    mismatches = [row for row in rows if row["teacher_agreement"] == "false"]
    lines.extend(
        [
            "",
            "## Representative Mismatches",
            "",
            "| Position | Config | Bucket | Teacher | Selected | Exact Best | Teacher Rank | Exact-Best Rank |",
            "|---|---|---|---|---|---|---:|---:|",
        ]
    )
    for row in mismatches[:15]:
        lines.append(
            f"| `{row['position_id']}` | `{row['config']}` | `{row['bucket']}` | "
            f"`{row['teacher_move']}` | `{row['selected_move']}` | "
            f"`{row['exact_best_moves'] or '-'}` | {row['teacher_rank'] or 'n/a'} | "
            f"{row['exact_best_rank'] or 'n/a'} |"
        )
    lines.extend(
        [
            "",
            "## Outputs",
            "",
            f"- rows_tsv: `{config.out_dir / 'rows.tsv'}`",
            f"- summary_tsv: `{config.out_dir / 'summary.tsv'}`",
            f"- categories_tsv: `{config.out_dir / 'categories.tsv'}`",
            "",
            "## Caveats",
            "",
            "- Teacher rows are used only when `status=ok` and `legal_move_valid=true`.",
            "- Evaluator choices are collected through `othello_analyze_position`; this script does not implement Othello legal move generation.",
            "- Buckets are conservative diagnostics and may need manual review.",
        ]
    )
    return "\n".join(lines) + "\n"


def summarize(rows: list[dict[str, Any]], targets: tuple[EvalTarget, ...]) -> list[dict[str, Any]]:
    summary: list[dict[str, Any]] = []
    for target in targets:
        target_rows = [row for row in rows if row["config"] == target.label]
        exact_rows = [row for row in target_rows if row["exact_best_moves"]]
        summary.append(
            {
                "config": target.label,
                "rows": len(target_rows),
                "teacher_agreements": sum(1 for row in target_rows if row["teacher_agreement"] == "true"),
                "exact_rows": len(exact_rows),
                "exact_agreements": sum(1 for row in exact_rows if row["exact_best_agreement"] == "true"),
                "teacher_rank_sum": sum(
                    int(row["teacher_rank"]) for row in target_rows if row["teacher_rank"]
                ),
                "exact_best_rank_sum": sum(
                    int(row["exact_best_rank"]) for row in exact_rows if row["exact_best_rank"]
                ),
            }
        )
    return summary


def categorize(rows: list[dict[str, Any]], targets: tuple[EvalTarget, ...]) -> list[dict[str, Any]]:
    category_rows: list[dict[str, Any]] = []
    for target in targets:
        counts: dict[str, int] = {}
        for row in rows:
            if row["config"] != target.label or row["teacher_agreement"] == "true":
                continue
            counts[row["bucket"]] = counts.get(row["bucket"], 0) + 1
        for bucket, count in sorted(counts.items(), key=lambda item: (-item[1], item[0])):
            category_rows.append({"config": target.label, "bucket": bucket, "count": count})
    return category_rows


def run_mining(config: MiningConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    teacher_rows = load_teacher_rows(config.teacher_labels, config.limit)
    exact_by_board = load_exact_by_board(config.exact_labels)

    output_rows: list[dict[str, Any]] = []
    if config.dry_run:
        commands = [quote_command(analyze_command(config, target)) for target in config.targets]
        (config.out_dir / "report.md").write_text("\n".join(commands) + "\n", encoding="utf-8")
        return 0

    failures: list[str] = []
    for index, teacher in enumerate(teacher_rows, start=1):
        board_text = teacher.get("board_text")
        if not isinstance(board_text, str):
            failures.append(f"teacher row {index}: missing board_text")
            continue
        teacher_move = normalize_move(teacher.get("move"))
        metadata = teacher.get("position_metadata") if isinstance(teacher.get("position_metadata"), dict) else {}
        exact = exact_by_board.get(board_key(board_text))
        exact_best = exact_best_moves(exact)
        exact_scores = exact_score_map(exact)
        position_id = str(teacher.get("position_name") or f"teacher-{teacher.get('position_index', index)}")

        analyses: dict[str, AnalyzeResult] = {}
        for target in config.targets:
            try:
                analyses[target.label] = run_analysis(config, target, board_text)
            except ScriptError as exc:
                failures.append(str(exc))
                if not config.allow_failures:
                    raise

        phase = next((analysis.phase for analysis in analyses.values() if analysis.phase), None)
        occupied = next(
            (analysis.occupied_count for analysis in analyses.values() if analysis.occupied_count is not None),
            None,
        )
        empty = next(
            (analysis.empty_count for analysis in analyses.values() if analysis.empty_count is not None),
            None,
        )

        for target in config.targets:
            analysis = analyses.get(target.label)
            if analysis is None:
                continue
            selected = analysis.best_move
            exact_rank_values = [
                rank_for_move(analysis.root_scores, move)
                for move in exact_best
                if rank_for_move(analysis.root_scores, move) is not None
            ]
            exact_best_rank = min(exact_rank_values) if exact_rank_values else None
            bucket = classify_bucket(
                teacher_move=teacher_move,
                selected_move=selected,
                exact_best=exact_best,
                metadata=metadata,
                phase=phase,
                empty_count=empty,
            )
            teacher_rank = rank_for_move(analysis.root_scores, teacher_move)
            exact_teacher_gap = exact_gap_for_move(exact_scores, teacher_move)
            exact_selected_gap = exact_gap_for_move(exact_scores, selected)
            output_rows.append(
                {
                    "position_id": position_id,
                    "teacher_labels_path": teacher.get("_teacher_labels_path", ""),
                    "config": target.label,
                    "phase": phase or "",
                    "occupied_count": occupied if occupied is not None else "",
                    "empty_count": empty if empty is not None else "",
                    "tags": metadata.get("tags", ""),
                    "teacher_engine_move": normalize_move(teacher.get("engine_move")) or "",
                    "teacher_move": teacher_move or "",
                    "legal_move_valid": str(teacher.get("legal_move_valid")).lower(),
                    "legal_moves": " ".join(str(move).lower() for move in teacher.get("legal_moves", [])),
                    "selected_move": selected or "",
                    "teacher_agreement": "true" if selected == teacher_move else "false",
                    "exact_best_moves": " ".join(exact_best),
                    "exact_best_agreement": "true" if selected in exact_best else "false" if exact_best else "",
                    "teacher_exact_disagreement": "true"
                    if exact_best and teacher_move not in exact_best
                    else "false"
                    if exact_best
                    else "",
                    "teacher_rank": cell(teacher_rank),
                    "exact_best_rank": cell(exact_best_rank),
                    "teacher_eval_score": cell(analysis.root_scores.get(teacher_move)) if teacher_move else "",
                    "selected_eval_score": cell(analysis.root_scores.get(selected)) if selected else "",
                    "teacher_exact_gap": cell(exact_teacher_gap),
                    "selected_exact_gap": cell(exact_selected_gap),
                    "bucket": bucket,
                    "board_text": board_text,
                }
            )

    fieldnames = [
        "position_id",
        "teacher_labels_path",
        "config",
        "phase",
        "occupied_count",
        "empty_count",
        "tags",
        "teacher_engine_move",
        "teacher_move",
        "legal_move_valid",
        "legal_moves",
        "selected_move",
        "teacher_agreement",
        "exact_best_moves",
        "exact_best_agreement",
        "teacher_exact_disagreement",
        "teacher_rank",
        "exact_best_rank",
        "teacher_eval_score",
        "selected_eval_score",
        "teacher_exact_gap",
        "selected_exact_gap",
        "bucket",
        "board_text",
    ]
    summary_rows = summarize(output_rows, config.targets)
    category_rows = categorize(output_rows, config.targets)
    append_tsv(config.out_dir / "rows.tsv", output_rows, fieldnames)
    append_tsv(
        config.out_dir / "summary.tsv",
        summary_rows,
        [
            "config",
            "rows",
            "teacher_agreements",
            "exact_rows",
            "exact_agreements",
            "teacher_rank_sum",
            "exact_best_rank_sum",
        ],
    )
    append_tsv(config.out_dir / "categories.tsv", category_rows, ["config", "bucket", "count"])
    (config.out_dir / "report.md").write_text(
        render_report(
            config=config,
            rows=output_rows,
            summary_rows=summary_rows,
            category_rows=category_rows,
        ),
        encoding="utf-8",
    )
    if failures:
        (config.out_dir / "failures.txt").write_text("\n".join(failures) + "\n", encoding="utf-8")
        return 1 if not config.allow_failures else 0
    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    config = config_from_args(args, invocation=[Path(sys.argv[0]).name, *(argv or sys.argv[1:])])
    try:
        return run_mining(config)
    except ScriptError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
