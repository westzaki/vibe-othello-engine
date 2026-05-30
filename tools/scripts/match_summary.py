#!/usr/bin/env python3
"""Summarize JSONL output from othello_match_runner.

This script is an experimental Python orchestration/reporting helper. The C++
match runner remains the source of truth for game execution.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from common import ScriptError


REQUIRED_FIELDS = (
    "game_index",
    "player_a_spec",
    "player_b_spec",
    "black_spec",
    "white_spec",
    "black_is_player_a",
    "opening_index",
    "opening_name",
    "winner",
    "black_score",
    "white_score",
    "score_diff_from_player_a",
    "plies",
    "passes",
    "illegal_or_error",
)

OPTIONAL_NUMERIC_FIELDS = (
    "nodes_black",
    "nodes_white",
    "nodes_player_a",
    "nodes_player_b",
    "exact_roots_black",
    "exact_roots_white",
    "exact_roots_player_a",
    "exact_roots_player_b",
    "time_ms_black",
    "time_ms_white",
    "time_ms_player_a",
    "time_ms_player_b",
)


@dataclass
class GameRecord:
    values: dict[str, Any]

    def __getitem__(self, key: str) -> Any:
        return self.values[key]

    def get(self, key: str, default: Any = None) -> Any:
        return self.values.get(key, default)


@dataclass
class OpeningSummary:
    opening_index: int
    opening_name: str
    games: int = 0
    valid_games: int = 0
    error_games: int = 0
    player_a_wins: int = 0
    player_b_wins: int = 0
    draws: int = 0
    total_diff: int = 0

    @property
    def average_diff(self) -> float:
        if self.valid_games == 0:
            return 0.0
        return self.total_diff / self.valid_games


@dataclass
class Summary:
    games: int = 0
    valid_games: int = 0
    error_games: int = 0
    player_a_wins: int = 0
    player_b_wins: int = 0
    draws: int = 0
    total_diff: int = 0
    total_plies: int = 0
    total_passes: int = 0
    player_a_specs: list[str] = field(default_factory=list)
    player_b_specs: list[str] = field(default_factory=list)
    openings: "OrderedDict[tuple[int, str], OpeningSummary]" = field(default_factory=OrderedDict)
    optional_totals: dict[str, float] = field(default_factory=dict)
    optional_counts: dict[str, int] = field(default_factory=dict)

    @property
    def player_a_win_rate(self) -> float:
        return self.player_a_wins / self.valid_games if self.valid_games else 0.0

    @property
    def player_b_win_rate(self) -> float:
        return self.player_b_wins / self.valid_games if self.valid_games else 0.0

    @property
    def average_diff(self) -> float:
        return self.total_diff / self.valid_games if self.valid_games else 0.0

    @property
    def average_plies(self) -> float:
        return self.total_plies / self.valid_games if self.valid_games else 0.0

    @property
    def average_passes(self) -> float:
        return self.total_passes / self.valid_games if self.valid_games else 0.0

    def optional_average(self, field_name: str) -> float | None:
        count = self.optional_counts.get(field_name, 0)
        if count == 0:
            return None
        return self.optional_totals[field_name] / count

    def optional_total(self, field_name: str) -> float | None:
        if self.optional_counts.get(field_name, 0) == 0:
            return None
        return self.optional_totals[field_name]


def _require_number(record: dict[str, Any], key: str) -> None:
    value = record[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ScriptError(f"field {key} must be numeric")


def _require_bool(record: dict[str, Any], key: str) -> None:
    if not isinstance(record[key], bool):
        raise ScriptError(f"field {key} must be boolean")


def _require_string(record: dict[str, Any], key: str) -> None:
    if not isinstance(record[key], str):
        raise ScriptError(f"field {key} must be string")


def parse_record_object(value: Any) -> GameRecord:
    if not isinstance(value, dict):
        raise ScriptError("record must be a JSON object")

    missing = [field_name for field_name in REQUIRED_FIELDS if field_name not in value]
    if missing:
        raise ScriptError("missing required field")

    for key in (
        "game_index",
        "opening_index",
        "black_score",
        "white_score",
        "score_diff_from_player_a",
        "plies",
        "passes",
    ):
        _require_number(value, key)
    for key in ("player_a_spec", "player_b_spec", "black_spec", "white_spec", "opening_name", "winner"):
        _require_string(value, key)
    _require_bool(value, "black_is_player_a")
    _require_bool(value, "illegal_or_error")

    for key in OPTIONAL_NUMERIC_FIELDS:
        if key in value:
            _require_number(value, key)

    return GameRecord(value)


def parse_jsonl_line(line: str, line_number: int) -> GameRecord:
    try:
        value = json.loads(line)
        return parse_record_object(value)
    except json.JSONDecodeError as exc:
        raise ScriptError(f"line {line_number}: invalid JSON: {exc.msg}") from exc
    except ScriptError as exc:
        raise ScriptError(f"line {line_number}: {exc}") from exc


def load_records(input_path: Path) -> list[GameRecord]:
    records: list[GameRecord] = []
    try:
        with input_path.open("r", encoding="utf-8") as input_file:
            for line_number, line in enumerate(input_file, start=1):
                if not line.strip():
                    continue
                records.append(parse_jsonl_line(line, line_number))
    except OSError as exc:
        raise ScriptError(f"failed to open input file: {input_path}: {exc}") from exc

    if not records:
        raise ScriptError(f"input file contains no records: {input_path}")
    return records


def _add_unique(values: list[str], value: str) -> None:
    if value not in values:
        values.append(value)


def summarize(records: list[GameRecord]) -> Summary:
    summary = Summary(games=len(records))

    for record in records:
        _add_unique(summary.player_a_specs, record["player_a_spec"])
        _add_unique(summary.player_b_specs, record["player_b_spec"])

        opening_key = (int(record["opening_index"]), str(record["opening_name"]))
        opening = summary.openings.get(opening_key)
        if opening is None:
            opening = OpeningSummary(opening_index=opening_key[0], opening_name=opening_key[1])
            summary.openings[opening_key] = opening
        opening.games += 1

        if record["illegal_or_error"]:
            summary.error_games += 1
            opening.error_games += 1
            continue

        summary.valid_games += 1
        opening.valid_games += 1

        diff = int(record["score_diff_from_player_a"])
        summary.total_diff += diff
        opening.total_diff += diff
        summary.total_plies += int(record["plies"])
        summary.total_passes += int(record["passes"])

        if diff > 0:
            summary.player_a_wins += 1
            opening.player_a_wins += 1
        elif diff < 0:
            summary.player_b_wins += 1
            opening.player_b_wins += 1
        else:
            summary.draws += 1
            opening.draws += 1

        for key in OPTIONAL_NUMERIC_FIELDS:
            value = record.get(key)
            if value is not None:
                summary.optional_totals[key] = summary.optional_totals.get(key, 0.0) + float(value)
                summary.optional_counts[key] = summary.optional_counts.get(key, 0) + 1

    return summary


def spec_label(specs: list[str]) -> str:
    if not specs:
        return "-"
    if len(specs) == 1:
        return specs[0]
    return f"mixed ({len(specs)} unique)"


def format_summary(input_path: Path, summary: Summary, by_opening: bool) -> str:
    lines = [
        f"input: {input_path}",
        f"games: {summary.games}",
        f"valid games: {summary.valid_games}",
        f"error games: {summary.error_games}",
        f"player A spec: {spec_label(summary.player_a_specs)}",
        f"player B spec: {spec_label(summary.player_b_specs)}",
        f"A wins: {summary.player_a_wins}",
        f"B wins: {summary.player_b_wins}",
        f"draws: {summary.draws}",
        f"A win rate: {summary.player_a_win_rate * 100.0:.2f}%",
        f"B win rate: {summary.player_b_win_rate * 100.0:.2f}%",
        f"average disc diff from A perspective: {summary.average_diff:.2f}",
        f"average plies: {summary.average_plies:.2f}",
        f"average passes: {summary.average_passes:.2f}",
    ]

    optional_labels = (
        ("nodes_player_a", "average nodes player A"),
        ("nodes_player_b", "average nodes player B"),
        ("time_ms_player_a", "average time ms player A"),
        ("time_ms_player_b", "average time ms player B"),
    )
    for field_name, label in optional_labels:
        average = summary.optional_average(field_name)
        if average is not None:
            lines.append(f"{label}: {average:.2f}")

    exact_root_labels = (
        ("exact_roots_player_a", "exact roots player A"),
        ("exact_roots_player_b", "exact roots player B"),
    )
    for field_name, label in exact_root_labels:
        total = summary.optional_total(field_name)
        average = summary.optional_average(field_name)
        if total is not None and average is not None:
            lines.append(f"total {label}: {total:.0f}")
            lines.append(f"average {label}: {average:.2f}")

    lines.append(f"unique openings: {len(summary.openings)}")

    if by_opening:
        lines.append("")
        lines.append("by opening:")
        lines.append(
            f"{'index':<8}{'name':<24}{'games':>8}{'errors':>8}"
            f"{'A wins':>8}{'B wins':>8}{'draws':>8}{'avg diff':>12}"
        )
        for opening in summary.openings.values():
            lines.append(
                f"{opening.opening_index:<8}{opening.opening_name:<24}{opening.games:>8}"
                f"{opening.error_games:>8}{opening.player_a_wins:>8}"
                f"{opening.player_b_wins:>8}{opening.draws:>8}{opening.average_diff:>12.2f}"
            )

    return "\n".join(lines) + "\n"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize othello_match_runner JSONL output.")
    parser.add_argument("--input", required=True, help="match runner JSONL file")
    parser.add_argument("--by-opening", action="store_true", help="print per-opening summary rows")
    parser.add_argument(
        "--allow-errors",
        action="store_true",
        help="return success even when records contain illegal_or_error=true",
    )
    parser.add_argument("--format", default="text", choices=("text",), help="output format")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        input_path = Path(args.input)
        records = load_records(input_path)
        summary = summarize(records)
        sys.stdout.write(format_summary(input_path, summary, args.by_opening))
        if summary.error_games > 0 and not args.allow_errors:
            return 1
        return 0
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
