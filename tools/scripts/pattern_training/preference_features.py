from __future__ import annotations

import collections

from common import ScriptError
from pattern_specs import PATTERN_SPECS, board9_rows_to_square_index_rows, pattern_index


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


def pattern_indexes_by_family(
    board_text: str, side: str, families: tuple[str, ...]
) -> dict[str, list[int]]:
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
