#!/usr/bin/env python3
"""Shared Othello pattern family specs for Python trainer tooling."""

from __future__ import annotations

from collections.abc import Sequence


PatternSpec = tuple[int, ...]
PatternFamilySpecs = tuple[PatternSpec, ...]

CORNER_2X3_SPECS: PatternFamilySpecs = (
    (0, 1, 2, 8, 9, 10),
    (7, 6, 5, 15, 14, 13),
    (56, 57, 58, 48, 49, 50),
    (63, 62, 61, 55, 54, 53),
)
CORNER_3X3_SPECS: PatternFamilySpecs = (
    (0, 1, 2, 8, 9, 10, 16, 17, 18),
    (7, 6, 5, 15, 14, 13, 23, 22, 21),
    (56, 57, 58, 48, 49, 50, 40, 41, 42),
    (63, 62, 61, 55, 54, 53, 47, 46, 45),
)
EDGE_8_SPECS: PatternFamilySpecs = (
    (0, 1, 2, 3, 4, 5, 6, 7),
    (56, 57, 58, 59, 60, 61, 62, 63),
    (0, 8, 16, 24, 32, 40, 48, 56),
    (7, 15, 23, 31, 39, 47, 55, 63),
)
EDGE_X_10_SPECS: PatternFamilySpecs = (
    (0, 1, 2, 3, 4, 5, 6, 7, 9, 14),
    (56, 57, 58, 59, 60, 61, 62, 63, 49, 54),
    (0, 8, 16, 24, 32, 40, 48, 56, 9, 49),
    (7, 15, 23, 31, 39, 47, 55, 63, 14, 54),
)
DIAGONAL_8_SPECS: PatternFamilySpecs = (
    (0, 9, 18, 27, 36, 45, 54, 63),
    (7, 14, 21, 28, 35, 42, 49, 56),
)
INNER_ROW_8_SPECS: PatternFamilySpecs = (
    (8, 9, 10, 11, 12, 13, 14, 15),
    (48, 49, 50, 51, 52, 53, 54, 55),
    (1, 9, 17, 25, 33, 41, 49, 57),
    (6, 14, 22, 30, 38, 46, 54, 62),
)

PATTERN_SPECS: dict[str, PatternFamilySpecs] = {
    "corner_2x3": CORNER_2X3_SPECS,
    "corner_3x3": CORNER_3X3_SPECS,
    "edge_8": EDGE_8_SPECS,
    "edge_x_10": EDGE_X_10_SPECS,
    "diagonal_8": DIAGONAL_8_SPECS,
    "inner_row_8": INNER_ROW_8_SPECS,
}
FAMILY_ORDER = tuple(PATTERN_SPECS)
COMMON_FAMILY_ALIASES: dict[str, tuple[str, ...]] = {
    "legacy": ("corner_2x3", "edge_8"),
    "broad_all": ("corner_3x3", "edge_8", "edge_x_10", "diagonal_8", "inner_row_8"),
}


def board_cell(rows: Sequence[Sequence[str]], square_index: int) -> str:
    return rows[square_index // 8][square_index % 8]


def pattern_index(rows: Sequence[Sequence[str]], side: str, spec: PatternSpec) -> int:
    opponent = "W" if side == "B" else "B"
    index = 0
    place = 1
    for square_index in spec:
        cell = board_cell(rows, square_index)
        state = 1 if cell == side else 2 if cell == opponent else 0
        index += state * place
        place *= 3
    return index
