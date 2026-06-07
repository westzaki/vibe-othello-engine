#!/usr/bin/env python3
"""Diagnose spatial and color symmetry coverage for Othello pattern tables."""

from __future__ import annotations

import argparse
import collections
import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from common import ScriptError
from pattern_specs import (
    D4_TRANSFORMS,
    FAMILY_ORDER,
    PATTERN_SPECS,
    color_inverted_pattern_index,
    invert_board9_colors,
    parse_board9_square_index_rows,
    reverse_pattern_index,
    transform_board9_text,
    transform_move,
    transform_pattern_spec,
)


SYMMETRY_DIAGNOSTIC_BOARD = (
    ".BBBBBB.\n"
    ".BBWWB..\n"
    "BWWWWWWW\n"
    "BWWBBBBB\n"
    "WWBWBBBB\n"
    "WWWBBBB.\n"
    ".WWWBWWW\n"
    "W.B.BBB.\n"
    "side=B"
)
SYMMETRY_TEACHER_MOVE = "d1"
SYMMETRY_OTHER_MOVE = "b1"
SYMMETRY_ROOT_SCORES = {"d1": 10, "b1": 4}
SYMMETRY_EXACT_BEST = ("d1",)

Weights = dict[str, dict[int, int]]
SYMMETRIZE_MODES = ("reversed-index", "color-inversion", "diagonal-reversal")
DIAGONAL_FAMILIES = ("diagonal_4", "diagonal_5", "diagonal_6", "diagonal_7", "diagonal_8")
REVERSIBLE_LINE_FAMILIES = ("edge_8", "inner_row_8", *DIAGONAL_FAMILIES)


@dataclass(frozen=True)
class TableViolationSummary:
    label: str
    checked_pairs: int
    violations: int
    max_abs_delta: int
    examples: tuple[tuple[str, int, str, int, int, int], ...]


@dataclass(frozen=True)
class SymmetrizeSummary:
    modes: tuple[str, ...]
    output_path: str
    families_processed: tuple[str, ...]
    entries_read: int
    entries_written: int
    changed_entries: int
    zero_entries_introduced: int
    zero_entries_removed: int
    max_abs_delta_before: int
    violations_before: dict[str, int]
    violations_after: dict[str, int]
    max_abs_delta_before_by_check: dict[str, int]
    max_abs_delta_after_by_check: dict[str, int]


def pattern_table_size(family: str) -> int:
    return 3 ** len(PATTERN_SPECS[family][0])


def read_pattern_table(path: Path) -> Weights:
    weights: Weights = {family: {} for family in FAMILY_ORDER}
    try:
        with path.open("r", encoding="utf-8", newline="") as input_file:
            reader = csv.reader(input_file, delimiter="\t")
            for line_number, row in enumerate(reader, start=1):
                if not row or not row[0].strip() or row[0].lstrip().startswith("#"):
                    continue
                if len(row) != 3:
                    raise ScriptError(f"{path}:{line_number}: expected family, index, weight")
                family = row[0].strip()
                if family not in PATTERN_SPECS:
                    raise ScriptError(f"{path}:{line_number}: unknown pattern family {family!r}")
                try:
                    index = int(row[1])
                    value = int(row[2])
                except ValueError as exc:
                    raise ScriptError(f"{path}:{line_number}: index and weight must be integers") from exc
                if index < 0 or index >= pattern_table_size(family):
                    raise ScriptError(f"{path}:{line_number}: index out of range for {family}")
                if index in weights[family]:
                    raise ScriptError(f"{path}:{line_number}: duplicate entry for {family}:{index}")
                weights[family][index] = value
    except OSError as exc:
        raise ScriptError(f"failed to read {path}: {exc}") from exc
    return weights


def d4_spec_sharing_report() -> dict[str, Any]:
    report: dict[str, Any] = {}
    for family in FAMILY_ORDER:
        specs = PATTERN_SPECS[family]
        spec_set = set(specs)
        reversed_spec_set = {tuple(reversed(spec)) for spec in specs}
        exact = 0
        reversed_only = 0
        missing = 0
        examples: list[dict[str, Any]] = []
        for instance, spec in enumerate(specs):
            for transform in D4_TRANSFORMS:
                mapped = transform_pattern_spec(spec, transform)
                if mapped in spec_set:
                    exact += 1
                    relation = "exact"
                elif mapped in reversed_spec_set:
                    reversed_only += 1
                    relation = "reversed_index"
                else:
                    missing += 1
                    relation = "not_represented_in_family"
                if relation != "exact" and len(examples) < 8:
                    examples.append(
                        {
                            "instance": instance,
                            "transform": transform,
                            "relation": relation,
                            "mapped_spec": mapped,
                        }
                    )
        report[family] = {
            "instances": len(specs),
            "cells": len(specs[0]),
            "exact_order_mappings": exact,
            "reversed_index_mappings": reversed_only,
            "missing_family_mappings": missing,
            "examples": examples,
        }
    return report


def cross_family_sharing_report() -> dict[str, Any]:
    checks = {
        "row_8_to_column_8": ("row_8", "column_8"),
        "column_8_to_row_8": ("column_8", "row_8"),
        "edge_8_to_edge_8": ("edge_8", "edge_8"),
        "edge_x_10_to_edge_x_10": ("edge_x_10", "edge_x_10"),
        "inner_row_8_to_inner_row_8": ("inner_row_8", "inner_row_8"),
    }
    report: dict[str, Any] = {}
    for label, (source_family, target_family) in checks.items():
        target_specs = set(PATTERN_SPECS[target_family])
        reversed_targets = {tuple(reversed(spec)) for spec in PATTERN_SPECS[target_family]}
        exact = 0
        reversed_only = 0
        missing = 0
        for spec in PATTERN_SPECS[source_family]:
            for transform in D4_TRANSFORMS:
                mapped = transform_pattern_spec(spec, transform)
                if mapped in target_specs:
                    exact += 1
                elif mapped in reversed_targets:
                    reversed_only += 1
                else:
                    missing += 1
        report[label] = {
            "source_family": source_family,
            "target_family": target_family,
            "same_table": source_family == target_family,
            "exact_order_mappings": exact,
            "reversed_index_mappings": reversed_only,
            "missing_mappings": missing,
        }
    return report


def _record_violation(
    examples: list[tuple[str, int, str, int, int, int]],
    left_family: str,
    left_index: int,
    right_family: str,
    right_index: int,
    left_value: int,
    right_value: int,
) -> None:
    if len(examples) < 8:
        examples.append((left_family, left_index, right_family, right_index, left_value, right_value))


def compare_weight_maps(
    weights: Weights,
    *,
    label: str,
    pairs: list[tuple[str, int, str, int]],
    expected_sign: int = 1,
) -> TableViolationSummary:
    violations = 0
    max_abs_delta = 0
    examples: list[tuple[str, int, str, int, int, int]] = []
    seen: set[tuple[str, int, str, int]] = set()
    for left_family, left_index, right_family, right_index in pairs:
        key = (left_family, left_index, right_family, right_index)
        reverse_key = (right_family, right_index, left_family, left_index)
        if key in seen or reverse_key in seen:
            continue
        seen.add(key)
        left_value = weights.get(left_family, {}).get(left_index, 0)
        right_value = weights.get(right_family, {}).get(right_index, 0)
        delta = abs(left_value - expected_sign * right_value)
        if delta:
            violations += 1
            max_abs_delta = max(max_abs_delta, delta)
            _record_violation(
                examples,
                left_family,
                left_index,
                right_family,
                right_index,
                left_value,
                right_value,
            )
    return TableViolationSummary(
        label=label,
        checked_pairs=len(seen),
        violations=violations,
        max_abs_delta=max_abs_delta,
        examples=tuple(examples),
    )


def reversed_index_pairs(families: tuple[str, ...]) -> list[tuple[str, int, str, int]]:
    pairs: list[tuple[str, int, str, int]] = []
    for family in families:
        cells = len(PATTERN_SPECS[family][0])
        for index in range(pattern_table_size(family)):
            pairs.append((family, index, family, reverse_pattern_index(index, cells)))
    return pairs


def color_inversion_pairs(families: tuple[str, ...]) -> list[tuple[str, int, str, int]]:
    pairs: list[tuple[str, int, str, int]] = []
    for family in families:
        cells = len(PATTERN_SPECS[family][0])
        for index in range(pattern_table_size(family)):
            pairs.append((family, index, family, color_inverted_pattern_index(index, cells)))
    return pairs


def row_column_pairs() -> list[tuple[str, int, str, int]]:
    pairs: list[tuple[str, int, str, int]] = []
    for index in range(pattern_table_size("row_8")):
        pairs.append(("row_8", index, "column_8", index))
        pairs.append(("row_8", index, "column_8", reverse_pattern_index(index, 8)))
    return pairs


def table_symmetry_violation_report(weights: Weights) -> list[TableViolationSummary]:
    present = tuple(family for family in FAMILY_ORDER if weights.get(family))
    summaries = [
        compare_weight_maps(
            weights,
            label="index_vs_reversed_index",
            pairs=reversed_index_pairs(present),
        ),
        compare_weight_maps(
            weights,
            label="index_vs_color_inverted_index",
            pairs=color_inversion_pairs(present),
            expected_sign=-1,
        ),
    ]
    if weights.get("row_8") or weights.get("column_8"):
        summaries.append(
            compare_weight_maps(weights, label="row_8_vs_column_8_rotational", pairs=row_column_pairs())
        )
    for family in ("edge_8", "inner_row_8"):
        if weights.get(family):
            summaries.append(
                compare_weight_maps(
                    weights,
                    label=f"{family}_reversal",
                    pairs=reversed_index_pairs((family,)),
                )
            )
    for family in ("diagonal_4", "diagonal_5", "diagonal_6", "diagonal_7", "diagonal_8"):
        if weights.get(family):
            summaries.append(
                compare_weight_maps(
                    weights,
                    label=f"{family}_diagonal_reversal",
                    pairs=reversed_index_pairs((family,)),
                )
            )
    return summaries


def parse_symmetrize_modes(text: str | None) -> tuple[str, ...]:
    if text is None or not text.strip():
        return ()
    modes: list[str] = []
    for raw_part in text.split(","):
        mode = raw_part.strip()
        if not mode:
            continue
        if mode not in SYMMETRIZE_MODES:
            raise ScriptError(
                f"unknown symmetrize mode {mode!r}; expected one of {', '.join(SYMMETRIZE_MODES)}"
            )
        if mode not in modes:
            modes.append(mode)
    return tuple(modes)


class SignedDisjointSet:
    def __init__(self, size: int) -> None:
        self.parent = list(range(size))
        self.sign_to_parent = [1] * size
        self.forced_zero = [False] * size

    def find(self, value: int) -> tuple[int, int]:
        parent = self.parent[value]
        if parent == value:
            return value, 1
        root, sign_to_root = self.find(parent)
        self.parent[value] = root
        self.sign_to_parent[value] *= sign_to_root
        return self.parent[value], self.sign_to_parent[value]

    def union(self, left: int, right: int, sign: int) -> None:
        left_root, left_sign = self.find(left)
        right_root, right_sign = self.find(right)
        if left_root == right_root:
            if left_sign != sign * right_sign:
                self.forced_zero[left_root] = True
            return
        self.parent[right_root] = left_root
        # Enforce left_value == sign * right_value.
        self.sign_to_parent[right_root] = left_sign * sign * right_sign
        self.forced_zero[left_root] = self.forced_zero[left_root] or self.forced_zero[right_root]


def symmetrize_pairs_for_family(family: str, modes: tuple[str, ...]) -> list[tuple[int, int, int]]:
    cells = len(PATTERN_SPECS[family][0])
    size = pattern_table_size(family)
    pairs: list[tuple[int, int, int]] = []
    if "reversed-index" in modes and family in REVERSIBLE_LINE_FAMILIES:
        for index in range(size):
            pairs.append((index, reverse_pattern_index(index, cells), 1))
    if "diagonal-reversal" in modes and family in DIAGONAL_FAMILIES:
        for index in range(size):
            pairs.append((index, reverse_pattern_index(index, cells), 1))
    if "color-inversion" in modes:
        for index in range(size):
            pairs.append((index, color_inverted_pattern_index(index, cells), -1))
    return pairs


def symmetrize_family_weights(
    family: str,
    family_weights: dict[int, int],
    modes: tuple[str, ...],
) -> dict[int, int]:
    pairs = symmetrize_pairs_for_family(family, modes)
    if not pairs:
        return dict(family_weights)

    size = pattern_table_size(family)
    groups = SignedDisjointSet(size)
    for left, right, sign in pairs:
        groups.union(left, right, sign)

    members_by_root: dict[int, list[tuple[int, int]]] = collections.defaultdict(list)
    forced_zero_roots: set[int] = set()
    for index in range(size):
        root, sign_to_root = groups.find(index)
        members_by_root[root].append((index, sign_to_root))
        if groups.forced_zero[groups.find(root)[0]]:
            forced_zero_roots.add(root)

    symmetrized: dict[int, int] = {}
    for root, members in members_by_root.items():
        if root in forced_zero_roots:
            root_value = 0
        else:
            projected_sum = sum(sign * family_weights.get(index, 0) for index, sign in members)
            root_value = round(projected_sum / len(members))
        for index, sign in members:
            value = sign * root_value
            if value:
                symmetrized[index] = value
    return symmetrized


def symmetrize_weights(weights: Weights, modes: tuple[str, ...]) -> Weights:
    if not modes:
        return {family: dict(values) for family, values in weights.items()}
    return {
        family: symmetrize_family_weights(family, values, modes) if values else {}
        for family, values in weights.items()
    }


def _summary_by_label(summaries: list[TableViolationSummary]) -> dict[str, TableViolationSummary]:
    return {summary.label: summary for summary in summaries}


def relevant_summary_labels(modes: tuple[str, ...], weights: Weights) -> tuple[str, ...]:
    labels: list[str] = []
    if "reversed-index" in modes:
        labels.append("index_vs_reversed_index")
        for family in ("edge_8", "inner_row_8", *DIAGONAL_FAMILIES):
            if weights.get(family):
                suffix = "diagonal_reversal" if family.startswith("diagonal_") else "reversal"
                labels.append(f"{family}_{suffix}")
    if "color-inversion" in modes:
        labels.append("index_vs_color_inverted_index")
    if "diagonal-reversal" in modes:
        for family in DIAGONAL_FAMILIES:
            if weights.get(family):
                labels.append(f"{family}_diagonal_reversal")
    return tuple(dict.fromkeys(labels))


def write_pattern_table(path: Path, weights: Weights) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output_file:
        output_file.write("# schema_version: pattern_table.v1\n")
        output_file.write("# generated_by: tools/scripts/pattern_symmetry_diagnostics.py\n")
        output_file.write("# no_strength_claim: true\n")
        output_file.write("# not_default_promotion: true\n")
        writer = csv.writer(output_file, delimiter="\t", lineterminator="\n")
        for family in FAMILY_ORDER:
            for index, value in sorted(weights.get(family, {}).items()):
                writer.writerow((family, index, value))


def symmetrize_pattern_table(
    *,
    input_path: Path,
    output_path: Path,
    modes: tuple[str, ...],
) -> tuple[Weights, SymmetrizeSummary, list[TableViolationSummary], list[TableViolationSummary]]:
    if not modes:
        raise ScriptError("--symmetrize-output requires at least one --symmetrize mode")
    weights = read_pattern_table(input_path)
    before = table_symmetry_violation_report(weights)
    symmetrized = symmetrize_weights(weights, modes)
    after = table_symmetry_violation_report(symmetrized)
    write_pattern_table(output_path, symmetrized)

    before_by_label = _summary_by_label(before)
    after_by_label = _summary_by_label(after)
    labels = relevant_summary_labels(modes, weights)
    processed = tuple(family for family in FAMILY_ORDER if weights.get(family) and symmetrize_pairs_for_family(family, modes))
    all_keys = {
        (family, index)
        for family in FAMILY_ORDER
        for index in set(weights.get(family, {})) | set(symmetrized.get(family, {}))
    }
    changed_entries = sum(
        1
        for family, index in all_keys
        if weights.get(family, {}).get(index, 0) != symmetrized.get(family, {}).get(index, 0)
    )
    zero_entries_introduced = sum(
        1
        for family, index in all_keys
        if weights.get(family, {}).get(index, 0) != 0 and symmetrized.get(family, {}).get(index, 0) == 0
    )
    zero_entries_removed = sum(
        1
        for family, index in all_keys
        if weights.get(family, {}).get(index, 0) == 0 and symmetrized.get(family, {}).get(index, 0) != 0
    )
    summary = SymmetrizeSummary(
        modes=modes,
        output_path=str(output_path),
        families_processed=processed,
        entries_read=sum(len(values) for values in weights.values()),
        entries_written=sum(len(values) for values in symmetrized.values()),
        changed_entries=changed_entries,
        zero_entries_introduced=zero_entries_introduced,
        zero_entries_removed=zero_entries_removed,
        max_abs_delta_before=max((before_by_label[label].max_abs_delta for label in labels if label in before_by_label), default=0),
        violations_before={label: before_by_label[label].violations for label in labels if label in before_by_label},
        violations_after={label: after_by_label[label].violations for label in labels if label in after_by_label},
        max_abs_delta_before_by_check={
            label: before_by_label[label].max_abs_delta for label in labels if label in before_by_label
        },
        max_abs_delta_after_by_check={
            label: after_by_label[label].max_abs_delta for label in labels if label in after_by_label
        },
    )
    return symmetrized, summary, before, after


def run_synthetic_transform_diagnostic() -> dict[str, Any]:
    import regularized_pairwise_pattern_train as trainer

    legal_moves = trainer.legal_moves_for_board(SYMMETRY_DIAGNOSTIC_BOARD)
    preferred_child = trainer.apply_move_to_board(SYMMETRY_DIAGNOSTIC_BOARD, SYMMETRY_TEACHER_MOVE)
    other_child = trainer.apply_move_to_board(SYMMETRY_DIAGNOSTIC_BOARD, SYMMETRY_OTHER_MOVE)
    base_features = trainer.preference_features(
        root_board_text=SYMMETRY_DIAGNOSTIC_BOARD,
        teacher_child_board=preferred_child,
        engine_child_board=other_child,
        families=("corner_2x3", "edge_8", "row_8", "column_8", "diagonal_8"),
    )
    rows = parse_board9_square_index_rows(SYMMETRY_DIAGNOSTIC_BOARD)
    color_inverted = invert_board9_colors(SYMMETRY_DIAGNOSTIC_BOARD)
    inverted_rows = parse_board9_square_index_rows(color_inverted)
    color_invariance_failures = []
    for family in FAMILY_ORDER:
        for spec_index, spec in enumerate(PATTERN_SPECS[family]):
            black_index = trainer.pattern_index(rows, "B", spec)
            white_index = trainer.pattern_index(inverted_rows, "W", spec)
            if black_index != white_index:
                color_invariance_failures.append((family, spec_index, black_index, white_index))

    transform_rows = []
    for transform in D4_TRANSFORMS:
        board = transform_board9_text(SYMMETRY_DIAGNOSTIC_BOARD, transform)
        transformed_legal = trainer.legal_moves_for_board(board)
        expected_legal = {transform_move(move, transform) for move in legal_moves}
        teacher_move = transform_move(SYMMETRY_TEACHER_MOVE, transform)
        other_move = transform_move(SYMMETRY_OTHER_MOVE, transform)
        root_scores = {
            transform_move(move, transform): score
            for move, score in SYMMETRY_ROOT_SCORES.items()
        }
        exact_best = tuple(transform_move(move, transform) for move in SYMMETRY_EXACT_BEST)
        transformed_preferred_child = trainer.apply_move_to_board(board, teacher_move)
        transformed_other_child = trainer.apply_move_to_board(board, other_move)
        expected_preferred_child = transform_board9_text(preferred_child, transform)
        expected_other_child = transform_board9_text(other_child, transform)
        features = trainer.preference_features(
            root_board_text=board,
            teacher_child_board=transformed_preferred_child,
            engine_child_board=transformed_other_child,
            families=("corner_2x3", "edge_8", "row_8", "column_8", "diagonal_8"),
        )
        transform_rows.append(
            {
                "transform": transform,
                "teacher_move": teacher_move,
                "other_move": other_move,
                "root_scores": root_scores,
                "exact_best": exact_best,
                "legal_moves_ok": transformed_legal == expected_legal,
                "teacher_child_ok": transformed_preferred_child == expected_preferred_child,
                "other_child_ok": transformed_other_child == expected_other_child,
                "features_nonempty": bool(features),
            }
        )

    return {
        "synthetic_board": "ok",
        "base_legal_moves": sorted(legal_moves),
        "base_feature_count": len(base_features),
        "color_side_relative_invariance_failures": color_invariance_failures[:8],
        "transforms": transform_rows,
    }


def render_markdown_report(
    *,
    spec_report: dict[str, Any],
    cross_report: dict[str, Any],
    synthetic_report: dict[str, Any],
    tsv_path: Path | None,
    table_summaries: list[TableViolationSummary],
    symmetrize_summary: SymmetrizeSummary | None = None,
    symmetrized_table_summaries: list[TableViolationSummary] | None = None,
) -> str:
    lines = [
        "# Pattern Symmetry Diagnostic",
        "",
        "## Scope",
        "",
        "- No trainer behavior changes.",
        "- No `current_default.eval` changes.",
        "- D4 transforms are checked on C++ square-index coordinates and rendered back to board9 text only at IO boundaries.",
        "",
        "## Side-Relative Color Inversion",
        "",
        f"- synthetic_failures: `{len(synthetic_report['color_side_relative_invariance_failures'])}`",
        "- Result: color inversion plus side inversion preserves side-relative pattern indexes for the synthetic board."
        if not synthetic_report["color_side_relative_invariance_failures"]
        else "- Result: failures found; inspect JSON details.",
        "",
        "## Synthetic D4 Pair Diagnostic",
        "",
    ]
    for row in synthetic_report["transforms"]:
        ok = (
            row["legal_moves_ok"]
            and row["teacher_child_ok"]
            and row["other_child_ok"]
            and row["features_nonempty"]
        )
        lines.append(
            f"- {row['transform']}: ok=`{str(ok).lower()}`, "
            f"teacher=`{row['teacher_move']}`, other=`{row['other_move']}`, "
            f"exact_best=`{' '.join(row['exact_best'])}`"
        )
    lines.extend(["", "## Pattern Spec Sharing", ""])
    for family in FAMILY_ORDER:
        row = spec_report[family]
        lines.append(
            f"- {family}: exact_order=`{row['exact_order_mappings']}`, "
            f"reversed_index=`{row['reversed_index_mappings']}`, "
            f"missing_in_family=`{row['missing_family_mappings']}`"
        )
    lines.extend(["", "## Cross-Family Sharing", ""])
    for label, row in cross_report.items():
        lines.append(
            f"- {label}: same_table=`{str(row['same_table']).lower()}`, "
            f"exact_order=`{row['exact_order_mappings']}`, "
            f"reversed_index=`{row['reversed_index_mappings']}`, "
            f"missing=`{row['missing_mappings']}`"
        )
    if tsv_path is not None:
        lines.extend(["", "## TSV Symmetry Violations", "", f"- tsv: `{tsv_path}`"])
        for summary in table_summaries:
            lines.append(
                f"- {summary.label}: checked=`{summary.checked_pairs}`, "
                f"violations=`{summary.violations}`, max_abs_delta=`{summary.max_abs_delta}`"
            )
            for example in summary.examples[:3]:
                left_family, left_index, right_family, right_index, left_value, right_value = example
                lines.append(
                    f"  - `{left_family}:{left_index}`=`{left_value}` vs "
                    f"`{right_family}:{right_index}`=`{right_value}`"
                )
    if symmetrize_summary is not None:
        lines.extend(
            [
                "",
                "## TSV Symmetrization",
                "",
                f"- modes: `{','.join(symmetrize_summary.modes)}`",
                f"- output: `{symmetrize_summary.output_path}`",
                f"- families_processed: `{', '.join(symmetrize_summary.families_processed) or 'none'}`",
                f"- entries_read: `{symmetrize_summary.entries_read}`",
                f"- entries_written: `{symmetrize_summary.entries_written}`",
                f"- changed_entries: `{symmetrize_summary.changed_entries}`",
                f"- zero_entries_introduced: `{symmetrize_summary.zero_entries_introduced}`",
                f"- zero_entries_removed: `{symmetrize_summary.zero_entries_removed}`",
                f"- max_abs_delta_before: `{symmetrize_summary.max_abs_delta_before}`",
            ]
        )
        for label in symmetrize_summary.violations_before:
            lines.append(
                f"- {label}: before=`{symmetrize_summary.violations_before[label]}`, "
                f"after=`{symmetrize_summary.violations_after.get(label, 0)}`, "
                f"max_abs_delta_after=`{symmetrize_summary.max_abs_delta_after_by_check.get(label, 0)}`"
            )
        if symmetrized_table_summaries is not None:
            lines.extend(["", "## Symmetrized TSV Violations", ""])
            for summary in symmetrized_table_summaries:
                lines.append(
                    f"- {summary.label}: checked=`{summary.checked_pairs}`, "
                    f"violations=`{summary.violations}`, max_abs_delta=`{summary.max_abs_delta}`"
                )
    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "- `exact_order` means transformed pattern instances reuse the same family table entry for the same ternary index.",
            "- `reversed_index` means the transformed geometry is represented, but the entry becomes the reversed ternary index; sharing requires table symmetry or explicit symmetrization.",
            "- `same_table=false` means the geometry is represented only in a different family, so regularized training currently learns separate parameters.",
            "- `index_vs_color_inverted_index` expects opposite signs because own/opponent ternary states are swapped.",
            "",
            "## Next Decision",
            "",
            "- A follow-up can compare `--symmetry-augment` against post-training table symmetrization.",
            "- The likely high-value checks are reversed edge/line indexes and row_8/column_8 sharing.",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pattern-table", type=Path, help="optional learned pattern TSV to inspect")
    parser.add_argument(
        "--symmetrize",
        help=f"comma-separated optional post-training modes: {','.join(SYMMETRIZE_MODES)}",
    )
    parser.add_argument(
        "--symmetrize-output",
        type=Path,
        help="write a symmetrized TSV to this path; intended for runs/ experiment outputs",
    )
    parser.add_argument("--json", action="store_true", help="write machine-readable JSON instead of Markdown")
    parser.add_argument("--out", type=Path, help="optional report output path")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    modes = parse_symmetrize_modes(args.symmetrize)
    if args.symmetrize_output is not None and args.pattern_table is None:
        raise ScriptError("--symmetrize-output requires --pattern-table")
    if modes and args.symmetrize_output is None:
        raise ScriptError("--symmetrize requires --symmetrize-output")
    spec_report = d4_spec_sharing_report()
    cross_report = cross_family_sharing_report()
    synthetic_report = run_synthetic_transform_diagnostic()
    table_summaries: list[TableViolationSummary] = []
    symmetrized_table_summaries: list[TableViolationSummary] = []
    symmetrize_summary: SymmetrizeSummary | None = None
    if args.pattern_table is not None:
        if args.symmetrize_output is not None:
            _, symmetrize_summary, table_summaries, symmetrized_table_summaries = symmetrize_pattern_table(
                input_path=args.pattern_table,
                output_path=args.symmetrize_output,
                modes=modes,
            )
        else:
            table_summaries = table_symmetry_violation_report(read_pattern_table(args.pattern_table))

    if args.json:
        payload = {
            "spec_report": spec_report,
            "cross_family_report": cross_report,
            "synthetic_report": synthetic_report,
            "pattern_table": str(args.pattern_table) if args.pattern_table else None,
            "table_summaries": [summary.__dict__ for summary in table_summaries],
            "symmetrize_summary": symmetrize_summary.__dict__ if symmetrize_summary is not None else None,
            "symmetrized_table_summaries": [
                summary.__dict__ for summary in symmetrized_table_summaries
            ],
        }
        text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    else:
        text = render_markdown_report(
            spec_report=spec_report,
            cross_report=cross_report,
            synthetic_report=synthetic_report,
            tsv_path=args.pattern_table,
            table_summaries=table_summaries,
            symmetrize_summary=symmetrize_summary,
            symmetrized_table_summaries=symmetrized_table_summaries,
        )

    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
