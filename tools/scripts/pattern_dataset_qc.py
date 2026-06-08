#!/usr/bin/env python3
"""Run PatternOnly dataset QC diagnostics at several dataset sizes.

This wrapper only orchestrates ``pattern_only_train.py --diagnose-dataset``.
It does not train weights, write candidate eval tables, or change source
controlled evaluator artifacts.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import glob
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from common import ScriptError, parse_csv_values, quote_command
from dataset_paths import (
    DATASET_REFERENCE_PREFIX,
    resolve_dataset_reference,
    resolve_dataset_root,
)


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "tools" / "scripts" / "pattern_only_train.py"
DEFAULT_TEACHER_LABELS = (
    "dataset:teacher/{dataset_name}/labels/ntest12-local/shards/labels-*.jsonl"
)
DEFAULT_EXACT_LABELS = (
    "dataset:teacher/{dataset_name}-exact-overlap-v0/exact-overlap/labels.jsonl"
)


def parse_size(value: str) -> int | None:
    text = value.strip().lower()
    if text == "full":
        return None
    multipliers = {"k": 1_000, "m": 1_000_000}
    suffix = text[-1:]
    try:
        if suffix in multipliers:
            return int(text[:-1]) * multipliers[suffix]
        return int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid size: {value}") from exc


def size_name(size: int | None) -> str:
    if size is None:
        return "full"
    if size % 1_000 == 0:
        return f"{size // 1_000}k"
    return str(size)


def expand_dataset_pattern(pattern: str, *, dataset_root: Path) -> list[Path]:
    if not pattern.startswith(DATASET_REFERENCE_PREFIX):
        return []
    body = pattern[len(DATASET_REFERENCE_PREFIX) :]
    if ":" in body and not any(ch in body for ch in "*?[]"):
        return [resolve_dataset_reference(pattern, explicit_root=dataset_root)]
    relative = body
    if ":" in body:
        raise ScriptError(f"dataset catalog references cannot contain glob wildcards: {pattern}")
    if Path(relative).is_absolute() or any(part == ".." for part in Path(relative).parts):
        raise ScriptError(f"dataset path must stay under dataset root: {pattern}")
    return [Path(path) for path in sorted(glob.glob(str(dataset_root / relative)))]


def expand_label_patterns(patterns: str, *, dataset_root: Path, dataset_name: str) -> list[Path]:
    paths: list[Path] = []
    for raw_pattern in parse_csv_values(patterns, error_label="label path list"):
        pattern = raw_pattern.format(dataset_name=dataset_name)
        if pattern.startswith(DATASET_REFERENCE_PREFIX):
            expanded = expand_dataset_pattern(pattern, dataset_root=dataset_root)
        else:
            expanded = [Path(path) for path in sorted(glob.glob(pattern))]
            if not expanded and not any(ch in pattern for ch in "*?[]"):
                expanded = [Path(pattern)]
        if not expanded:
            raise ScriptError(f"label pattern matched no files: {pattern}")
        paths.extend(expanded)
    unique: dict[str, Path] = {}
    for path in paths:
        unique[str(path)] = path
    return [unique[key] for key in sorted(unique)]


def write_subset(paths: list[Path], *, output: Path, limit: int) -> int:
    output.parent.mkdir(parents=True, exist_ok=True)
    rows = 0
    with output.open("w", encoding="utf-8") as out:
        for path in paths:
            with path.open("r", encoding="utf-8") as input_file:
                for line in input_file:
                    if not line.strip():
                        continue
                    out.write(line)
                    rows += 1
                    if rows >= limit:
                        return rows
    return rows


def nested_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, ensure_ascii=False)


def coverage_digest(summary: dict[str, Any]) -> tuple[int, int, str, str, str]:
    overall = summary.get("overall", {})
    return (
        int(overall.get("complete_rows", 0) or 0),
        int(overall.get("rows", 0) or 0),
        nested_json(summary.get("by_split", {})),
        nested_json(summary.get("by_phase", {})),
        nested_json(summary.get("by_source_bucket", {})),
    )


def top_group_distribution(diagnostics: dict[str, Any]) -> dict[str, int]:
    result: dict[str, int] = {}
    for group in diagnostics.get("by_phase", {}).values():
        distribution = group.get("exact_best_top_group_size_distribution", {})
        if not isinstance(distribution, dict):
            continue
        for key, value in distribution.items():
            result[str(key)] = result.get(str(key), 0) + int(value)
    return dict(sorted(result.items(), key=lambda item: item[0]))


def summarize_run(run_name: str, summary_path: Path) -> dict[str, Any]:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    rows = summary.get("rows", {})
    qc = rows.get("qc_summary", {})
    diagnostics = rows.get("dataset_diagnostics", {})
    exact_complete, exact_rows, exact_by_split, exact_by_phase, exact_by_source = coverage_digest(
        qc.get("complete_exact_move_scores", {})
    )
    teacher_complete, teacher_rows, teacher_by_split, teacher_by_phase, teacher_by_source = coverage_digest(
        qc.get("complete_teacher_move_scores", {})
    )
    leakage = qc.get("duplicate_group_split_leakage_check", {})
    return {
        "run": run_name,
        "teacher_rows": rows.get("teacher_rows", 0),
        "accepted_teacher_rows": rows.get("accepted_teacher_rows", 0),
        "training_examples": rows.get("training_examples", 0),
        "exact_unavailable": sum(
            int(group.get("exact_unavailable", 0))
            for group in diagnostics.get("by_split", {}).values()
            if isinstance(group, dict)
        ),
        "teacher_exact_disagreement": sum(
            int(group.get("teacher_exact_disagreement", 0))
            for group in diagnostics.get("by_split", {}).values()
            if isinstance(group, dict)
        ),
        "exact_complete_rows": exact_complete,
        "exact_coverage_rows": exact_rows,
        "teacher_complete_rows": teacher_complete,
        "teacher_coverage_rows": teacher_rows,
        "duplicate_groups": leakage.get("duplicate_groups", 0),
        "leaking_groups": leakage.get("leaking_groups", 0),
        "leaking_rows": leakage.get("leaking_rows", 0),
        "root_phase_counts": nested_json(qc.get("root_phase_counts", {})),
        "child_phase_counts": nested_json(qc.get("child_phase_counts", {})),
        "root_to_child_phase_counts": nested_json(qc.get("root_to_child_phase_counts", {})),
        "legal_move_count_distribution": nested_json(qc.get("legal_move_count_distribution", {})),
        "complete_exact_move_scores_by_split": exact_by_split,
        "complete_exact_move_scores_by_phase": exact_by_phase,
        "complete_exact_move_scores_by_source_bucket": exact_by_source,
        "complete_teacher_move_scores_by_split": teacher_by_split,
        "complete_teacher_move_scores_by_phase": teacher_by_phase,
        "complete_teacher_move_scores_by_source_bucket": teacher_by_source,
        "source_bucket_counts": nested_json(qc.get("source_bucket_counts", {})),
        "training_bucket_counts": nested_json(qc.get("training_bucket_counts", {})),
        "duplicate_group_split_leakage_check": nested_json(leakage),
        "dataset_diagnostics_by_split": nested_json(diagnostics.get("by_split", {})),
        "dataset_diagnostics_by_phase": nested_json(diagnostics.get("by_phase", {})),
        "dataset_diagnostics_by_source_bucket": nested_json(diagnostics.get("by_source_bucket", {})),
        "exact_best_top_group_size": nested_json(top_group_distribution(diagnostics)),
    }


def write_summary_tsv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_markdown_summary(path: Path, *, rows: list[dict[str, Any]], command_path: Path) -> None:
    lines = [
        "# PatternOnly Dataset QC Summary",
        "",
        "This summary reports dataset QC diagnostics only. It makes no playing-strength claim, "
        "does not train weights, and does not promote or modify any evaluator artifact.",
        "",
        f"- generated_at: `{dt.datetime.now(dt.UTC).strftime('%Y-%m-%dT%H:%M:%SZ')}`",
        f"- commands: `{command_path}`",
        "",
        "## Scope",
        "",
        "- source_bucket is provenance from the literal label-row `source_bucket` field.",
        "- training_bucket is the weighting bucket selected by `--bucket-field`.",
        "- coverage and duplicate diagnostics are collected before the selected split filter.",
        "- child phase, pattern family, and training bucket diagnostics come from generated listwise examples.",
        "- generated artifacts are under `runs/` only.",
        "",
        "## Runs",
        "",
        "| run | teacher rows | accepted | examples | exact unavailable | teacher/exact disagreement | exact complete | teacher complete | duplicate groups | leaking groups |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        lines.append(
            "| "
            + " | ".join(
                [
                    str(row["run"]),
                    str(row["teacher_rows"]),
                    str(row["accepted_teacher_rows"]),
                    str(row["training_examples"]),
                    str(row["exact_unavailable"]),
                    str(row["teacher_exact_disagreement"]),
                    f"{row['exact_complete_rows']} / {row['exact_coverage_rows']}",
                    f"{row['teacher_complete_rows']} / {row['teacher_coverage_rows']}",
                    str(row["duplicate_groups"]),
                    str(row["leaking_groups"]),
                ]
            )
            + " |"
        )
    lines.extend(
        [
            "",
            "See `summary.tsv` and each run's `summary.json` for full phase, source bucket, coverage, "
            "and duplicate leakage details.",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def command_for_run(
    *,
    teacher_labels: list[Path],
    exact_labels: list[Path],
    out_dir: Path,
    args: argparse.Namespace,
) -> list[str]:
    command = [
        sys.executable,
        str(SCRIPT_PATH),
        "--teacher-labels",
        ",".join(str(path) for path in teacher_labels),
        "--eval-config",
        str(args.eval_config),
        "--analyze-position",
        str(args.analyze_position),
        "--out-dir",
        str(out_dir),
        "--families",
        args.families,
        "--split",
        args.split,
        "--bucket-field",
        args.bucket_field,
        "--analysis-runner",
        args.analysis_runner,
        "--analysis-jobs",
        str(args.analysis_jobs),
        "--analysis-cache-dir",
        str(out_dir / "analysis-cache"),
        "--analysis-cache-mode",
        "read-write",
        "--diagnose-dataset",
    ]
    if exact_labels:
        command.extend(["--exact-labels", ",".join(str(path) for path in exact_labels)])
    return command


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run PatternOnly dataset QC diagnostics at 1K, 10K, and full sizes."
    )
    parser.add_argument("--dataset-root")
    parser.add_argument("--dataset-name", default="ntest-balanced300k-v0")
    parser.add_argument("--teacher-labels", default=DEFAULT_TEACHER_LABELS)
    parser.add_argument("--exact-labels", default=DEFAULT_EXACT_LABELS)
    parser.add_argument("--eval-config", required=True)
    parser.add_argument("--analyze-position", required=True)
    parser.add_argument("--out-root", default="runs/pattern-training")
    parser.add_argument("--sizes", default="1k,10k,full")
    parser.add_argument("--families", default="broad_all")
    parser.add_argument("--split", default="all", choices=("train", "validation", "holdout", "all"))
    parser.add_argument("--bucket-field", default="source_bucket")
    parser.add_argument("--analysis-runner", default="batch", choices=("subprocess", "batch"))
    parser.add_argument("--analysis-jobs", type=int, default=4)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.analysis_jobs <= 0:
        raise ScriptError("--analysis-jobs must be positive")
    dataset_root = resolve_dataset_root(args.dataset_root, require_exists=True).path
    teacher_labels = expand_label_patterns(
        args.teacher_labels,
        dataset_root=dataset_root,
        dataset_name=args.dataset_name,
    )
    exact_labels = (
        expand_label_patterns(args.exact_labels, dataset_root=dataset_root, dataset_name=args.dataset_name)
        if args.exact_labels
        else []
    )
    sizes = [parse_size(value) for value in parse_csv_values(args.sizes, error_label="sizes")]
    out_root = Path(args.out_root) / args.dataset_name / "qc"
    inputs_dir = out_root / "inputs"
    out_root.mkdir(parents=True, exist_ok=True)
    commands: list[str] = []
    summaries: list[dict[str, Any]] = []
    for size in sizes:
        name = size_name(size)
        run_dir = out_root / name
        if size is None:
            run_teacher_labels = teacher_labels
        else:
            subset_path = inputs_dir / f"teacher_first_{size}.jsonl"
            selected = write_subset(teacher_labels, output=subset_path, limit=size)
            if selected < size:
                raise ScriptError(f"requested {size} teacher rows but only found {selected}")
            run_teacher_labels = [subset_path]
        command = command_for_run(
            teacher_labels=run_teacher_labels,
            exact_labels=exact_labels,
            out_dir=run_dir,
            args=args,
        )
        commands.append(quote_command(command))
        if args.dry_run:
            continue
        subprocess.run(command, cwd=REPO_ROOT, check=True)
        diagnostic_report = run_dir / "dataset_diagnostic.md"
        report = run_dir / "report.md"
        if diagnostic_report.exists():
            shutil.copyfile(diagnostic_report, report)
        summaries.append(summarize_run(name, run_dir / "summary.json"))
    command_path = out_root / "commands.md"
    command_path.write_text(
        "# PatternOnly Dataset QC Commands\n\n"
        + "\n\n".join(f"```sh\n{command}\n```" for command in commands)
        + "\n",
        encoding="utf-8",
    )
    if not args.dry_run:
        write_summary_tsv(out_root / "summary.tsv", summaries)
        write_markdown_summary(out_root / "summary.md", rows=summaries, command_path=command_path)
    print(f"wrote {out_root}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ScriptError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
