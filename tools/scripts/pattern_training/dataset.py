from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from common import ScriptError, parse_csv_values
from dataset_paths import resolve_path_references
from pattern_training.board9 import board_key, parse_board


def normalize_move(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    return text or None


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
