from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from common import ScriptError
from pattern_training.board9 import board_key
from pattern_training.dataset import normalize_move


@dataclass(frozen=True)
class SplitRatios:
    train: int
    validation: int
    holdout: int

    @property
    def total(self) -> int:
        return self.train + self.validation + self.holdout


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
