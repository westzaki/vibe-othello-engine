#!/usr/bin/env python3
"""Shared Othello pattern family specs for Python trainer tooling."""

from __future__ import annotations

from collections.abc import Sequence
from typing import Literal


PatternSpec = tuple[int, ...]
PatternFamilySpecs = tuple[PatternSpec, ...]
PatternRows = Sequence[Sequence[str]]
D4Transform = Literal[
    "identity",
    "flip_horizontal",
    "flip_vertical",
    "rotate_90",
    "rotate_180",
    "rotate_270",
    "transpose_main_diagonal",
    "transpose_anti_diagonal",
]

D4_TRANSFORMS: tuple[D4Transform, ...] = (
    "identity",
    "flip_horizontal",
    "flip_vertical",
    "rotate_90",
    "rotate_180",
    "rotate_270",
    "transpose_main_diagonal",
    "transpose_anti_diagonal",
)

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
ROW_8_SPECS: PatternFamilySpecs = tuple(
    tuple(row * 8 + file for file in range(8)) for row in range(8)
)
COLUMN_8_SPECS: PatternFamilySpecs = tuple(
    tuple(row * 8 + file for row in range(8)) for file in range(8)
)
DIAGONAL_4_SPECS: PatternFamilySpecs = (
    (4, 13, 22, 31),
    (32, 41, 50, 59),
    (3, 10, 17, 24),
    (39, 46, 53, 60),
)
DIAGONAL_5_SPECS: PatternFamilySpecs = (
    (3, 12, 21, 30, 39),
    (24, 33, 42, 51, 60),
    (4, 11, 18, 25, 32),
    (31, 38, 45, 52, 59),
)
DIAGONAL_6_SPECS: PatternFamilySpecs = (
    (2, 11, 20, 29, 38, 47),
    (16, 25, 34, 43, 52, 61),
    (5, 12, 19, 26, 33, 40),
    (23, 30, 37, 44, 51, 58),
)
DIAGONAL_7_SPECS: PatternFamilySpecs = (
    (1, 10, 19, 28, 37, 46, 55),
    (8, 17, 26, 35, 44, 53, 62),
    (6, 13, 20, 27, 34, 41, 48),
    (15, 22, 29, 36, 43, 50, 57),
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
CORNER_2X4_SPECS: PatternFamilySpecs = (
    (0, 1, 2, 3, 8, 9, 10, 11),
    (7, 6, 5, 4, 15, 14, 13, 12),
    (56, 57, 58, 59, 48, 49, 50, 51),
    (63, 62, 61, 60, 55, 54, 53, 52),
)

PATTERN_SPECS: dict[str, PatternFamilySpecs] = {
    "corner_2x3": CORNER_2X3_SPECS,
    "corner_3x3": CORNER_3X3_SPECS,
    "edge_8": EDGE_8_SPECS,
    "edge_x_10": EDGE_X_10_SPECS,
    "row_8": ROW_8_SPECS,
    "column_8": COLUMN_8_SPECS,
    "diagonal_4": DIAGONAL_4_SPECS,
    "diagonal_5": DIAGONAL_5_SPECS,
    "diagonal_6": DIAGONAL_6_SPECS,
    "diagonal_7": DIAGONAL_7_SPECS,
    "diagonal_8": DIAGONAL_8_SPECS,
    "inner_row_8": INNER_ROW_8_SPECS,
    "corner_2x4": CORNER_2X4_SPECS,
}
FAMILY_ORDER = tuple(PATTERN_SPECS)
COMMON_FAMILY_ALIASES: dict[str, tuple[str, ...]] = {
    "legacy": ("corner_2x3", "edge_8"),
    "broad_all": ("corner_3x3", "edge_8", "edge_x_10", "diagonal_8", "inner_row_8"),
    "broad_v2": (
        "corner_3x3",
        "edge_8",
        "edge_x_10",
        "diagonal_8",
        "inner_row_8",
        "row_8",
        "column_8",
        "diagonal_4",
        "diagonal_5",
        "diagonal_6",
        "diagonal_7",
        "corner_2x4",
    ),
}


def board9_rows_to_square_index_rows(rows: PatternRows) -> tuple[Sequence[str], ...]:
    """Convert board9 display rows to C++ square-index rows.

    Board9 text is written rank 8 down to rank 1 for human display. Pattern
    specs and the C++ evaluator use square indexes in a1..h1, then a2..h2,
    through a8..h8 order, so row 0 must be rank 1 before pattern indexing.
    """

    if len(rows) != 8:
        raise ValueError("expected exactly 8 board rows")
    return tuple(reversed(rows))


def parse_board9_square_index_rows(text: str) -> tuple[Sequence[str], ...]:
    """Parse board9 text and return rows in C++ square-index order.

    The input board rows are display order, rank 8 down to rank 1, followed by
    a side line. The returned rows are rank 1 up to rank 8 for pattern specs.
    """

    lines = [line.strip() for line in text.strip().splitlines() if line.strip()]
    if len(lines) != 9:
        raise ValueError("expected board9 text with 8 rows plus side")
    side_line = lines[8]
    if not side_line.startswith("side=") or len(side_line) != 6:
        raise ValueError("expected board9 side line")
    return board9_rows_to_square_index_rows(lines[:8])


def board_cell(rows: PatternRows, square_index: int) -> str:
    return rows[square_index // 8][square_index % 8]


def pattern_index(rows: PatternRows, side: str, spec: PatternSpec) -> int:
    opponent = "W" if side == "B" else "B"
    index = 0
    place = 1
    for square_index in spec:
        cell = board_cell(rows, square_index)
        state = 1 if cell == side else 2 if cell == opponent else 0
        index += state * place
        place *= 3
    return index


def transform_square_index(square_index: int, transform: D4Transform) -> int:
    """Map a C++ square index through one of the board's D4 symmetries."""

    if square_index < 0 or square_index >= 64:
        raise ValueError("square_index must be in [0, 64)")
    row = square_index // 8
    col = square_index % 8
    if transform == "identity":
        mapped_row, mapped_col = row, col
    elif transform == "flip_horizontal":
        mapped_row, mapped_col = row, 7 - col
    elif transform == "flip_vertical":
        mapped_row, mapped_col = 7 - row, col
    elif transform == "rotate_90":
        mapped_row, mapped_col = col, 7 - row
    elif transform == "rotate_180":
        mapped_row, mapped_col = 7 - row, 7 - col
    elif transform == "rotate_270":
        mapped_row, mapped_col = 7 - col, row
    elif transform == "transpose_main_diagonal":
        mapped_row, mapped_col = col, row
    elif transform == "transpose_anti_diagonal":
        mapped_row, mapped_col = 7 - col, 7 - row
    else:
        raise ValueError(f"unknown D4 transform: {transform}")
    return mapped_row * 8 + mapped_col


def transform_pattern_spec(spec: PatternSpec, transform: D4Transform) -> PatternSpec:
    return tuple(transform_square_index(square_index, transform) for square_index in spec)


def reverse_pattern_index(index: int, cell_count: int) -> int:
    """Reverse base-3 cell order for a pattern table index."""

    if index < 0:
        raise ValueError("index must be non-negative")
    reversed_index = 0
    for _ in range(cell_count):
        reversed_index = reversed_index * 3 + index % 3
        index //= 3
    if index:
        raise ValueError("index does not fit in cell_count ternary cells")
    return reversed_index


def color_inverted_pattern_index(index: int, cell_count: int) -> int:
    """Swap own/opponent ternary states in a side-relative pattern index."""

    if index < 0:
        raise ValueError("index must be non-negative")
    inverted = 0
    place = 1
    for _ in range(cell_count):
        digit = index % 3
        if digit == 1:
            digit = 2
        elif digit == 2:
            digit = 1
        inverted += digit * place
        place *= 3
        index //= 3
    if index:
        raise ValueError("index does not fit in cell_count ternary cells")
    return inverted


def _move_to_square_index(move: str) -> int | None:
    text = move.strip().lower()
    if text in {"pass", "pa", "--", "-"}:
        return None
    if len(text) != 2 or text[0] < "a" or text[0] > "h" or text[1] < "1" or text[1] > "8":
        raise ValueError(f"invalid move coordinate: {move}")
    return (int(text[1]) - 1) * 8 + ord(text[0]) - ord("a")


def _square_index_to_move(square_index: int) -> str:
    if square_index < 0 or square_index >= 64:
        raise ValueError("square_index must be in [0, 64)")
    return f"{chr(ord('a') + square_index % 8)}{square_index // 8 + 1}"


def transform_move(move: str, transform: D4Transform) -> str:
    square_index = _move_to_square_index(move)
    if square_index is None:
        return "pass"
    return _square_index_to_move(transform_square_index(square_index, transform))


def parse_board9_text(board_text: str) -> tuple[tuple[str, ...], str]:
    lines = [line.strip() for line in board_text.strip().splitlines() if line.strip()]
    if len(lines) != 9:
        raise ValueError("expected board9 text with 8 rows plus side")
    side_line = lines[8]
    if not side_line.startswith("side=") or len(side_line) != 6 or side_line[-1] not in {"B", "W"}:
        raise ValueError("expected board9 side line")
    rows = tuple(lines[:8])
    if any(len(row) != 8 or any(cell not in {"B", "W", "."} for cell in row) for row in rows):
        raise ValueError("expected 8 board rows containing only B, W, or .")
    return rows, side_line[-1]


def render_board9_text(rows: Sequence[Sequence[str]], side: str) -> str:
    if side not in {"B", "W"}:
        raise ValueError("side must be B or W")
    if len(rows) != 8:
        raise ValueError("expected exactly 8 board rows")
    rendered_rows: list[str] = []
    for row in rows:
        text = "".join(row)
        if len(text) != 8 or any(cell not in {"B", "W", "."} for cell in text):
            raise ValueError("expected 8 board rows containing only B, W, or .")
        rendered_rows.append(text)
    return "\n".join(rendered_rows) + f"\nside={side}"


def transform_board9_text(board_text: str, transform: D4Transform) -> str:
    rows, side = parse_board9_text(board_text)
    square_rows = [list(row) for row in board9_rows_to_square_index_rows(rows)]
    transformed = [["." for _ in range(8)] for _ in range(8)]
    for square_index in range(64):
        target = transform_square_index(square_index, transform)
        transformed[target // 8][target % 8] = square_rows[square_index // 8][square_index % 8]
    return render_board9_text(tuple(reversed(["".join(row) for row in transformed])), side)


def invert_board9_colors(board_text: str) -> str:
    rows, side = parse_board9_text(board_text)
    swapped = [
        row.translate(str.maketrans({"B": "W", "W": "B", ".": "."}))
        for row in rows
    ]
    inverted_side = "W" if side == "B" else "B"
    return render_board9_text(swapped, inverted_side)
