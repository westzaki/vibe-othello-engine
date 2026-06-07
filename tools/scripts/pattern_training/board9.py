from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any, Sequence

from common import ScriptError


BoardRows = list[list[str]]


def parse_board(board_text: str) -> tuple[BoardRows, str]:
    lines = [line.strip() for line in board_text.strip().splitlines() if line.strip()]
    if len(lines) != 9:
        raise ScriptError("expected board9 text with 8 rows plus side")
    rows = [list(line) for line in lines[:8]]
    for row in rows:
        if len(row) != 8 or any(cell not in {"B", "W", "."} for cell in row):
            raise ScriptError("expected 8 board rows containing only B, W, or .")
    side_line = lines[8]
    if not side_line.startswith("side=") or len(side_line) != 6 or side_line[-1] not in {"B", "W"}:
        raise ScriptError("expected board9 side line")
    return rows, side_line[-1]


def board_to_text(rows: Sequence[Sequence[str]], side: str) -> str:
    if side not in {"B", "W"}:
        raise ScriptError("expected board9 side line")
    rendered_rows: list[str] = []
    for row in rows:
        text = "".join(row)
        if len(text) != 8 or any(cell not in {"B", "W", "."} for cell in text):
            raise ScriptError("expected 8 board rows containing only B, W, or .")
        rendered_rows.append(text)
    if len(rendered_rows) != 8:
        raise ScriptError("expected board9 text with 8 rows plus side")
    return "\n".join(rendered_rows) + f"\nside={side}"


def occupied_count(board_text: str) -> int:
    rows, _ = parse_board(board_text)
    return sum(1 for row in rows for cell in row if cell in {"B", "W"})


def empty_count(board_text: str) -> int:
    rows, _ = parse_board(board_text)
    return sum(row.count(".") for row in rows)


def normalize_move(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text:
        return None
    if text in {"pass", "pa", "--", "-"}:
        return "pass"
    return text


def opponent(side: str) -> str:
    if side == "B":
        return "W"
    if side == "W":
        return "B"
    raise ScriptError("expected side B or W")


def _inside(row: int, col: int) -> bool:
    return 0 <= row < 8 and 0 <= col < 8


def _move_to_coord(move: str) -> tuple[int, int] | None:
    if len(move) != 2 or move[0] < "a" or move[0] > "h" or move[1] < "1" or move[1] > "8":
        return None
    return 8 - int(move[1]), ord(move[0]) - ord("a")


def _coord_to_move(row: int, col: int) -> str:
    return f"{chr(ord('a') + col)}{8 - row}"


DIRECTIONS = (
    (-1, -1),
    (-1, 0),
    (-1, 1),
    (0, -1),
    (0, 1),
    (1, -1),
    (1, 0),
    (1, 1),
)


def _flips_for_move(rows: BoardRows, side: str, row: int, col: int) -> list[tuple[int, int]]:
    if not _inside(row, col) or rows[row][col] != ".":
        return []
    other = opponent(side)
    flips: list[tuple[int, int]] = []
    for dr, dc in DIRECTIONS:
        current: list[tuple[int, int]] = []
        r = row + dr
        c = col + dc
        while _inside(r, c) and rows[r][c] == other:
            current.append((r, c))
            r += dr
            c += dc
        if current and _inside(r, c) and rows[r][c] == side:
            flips.extend(current)
    return flips


def legal_moves_for_board(board_text: str) -> set[str]:
    rows, side = parse_board(board_text)
    moves: set[str] = set()
    for row in range(8):
        for col in range(8):
            if _flips_for_move(rows, side, row, col):
                moves.add(_coord_to_move(row, col))
    if not moves:
        moves.add("pass")
    return moves


def apply_move_to_board(board_text: str, move: str) -> str:
    rows, side = parse_board(board_text)
    normalized = normalize_move(move)
    if normalized == "pass":
        if any(_flips_for_move(rows, side, row, col) for row in range(8) for col in range(8)):
            raise ScriptError("pass is not legal while a board move exists")
        return board_to_text(rows, opponent(side))
    if normalized is None:
        raise ScriptError("move cannot be empty")
    coord = _move_to_coord(normalized)
    if coord is None:
        raise ScriptError(f"invalid move coordinate: {move}")
    row, col = coord
    flips = _flips_for_move(rows, side, row, col)
    if not flips:
        raise ScriptError(f"illegal move for board: {move}")
    rows[row][col] = side
    for r, c in flips:
        rows[r][c] = side
    return board_to_text(rows, opponent(side))


def board_key(board_text: str) -> str:
    return "\n".join(line.rstrip() for line in board_text.strip().splitlines())


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as input_file:
            for chunk in iter(lambda: input_file.read(65536), b""):
                digest.update(chunk)
    except OSError as exc:
        raise ScriptError(f"failed to read file for SHA256: {path}: {exc}") from exc
    return digest.hexdigest()


def board_hash(board_text: str) -> str:
    return sha256_text(board_key(board_text))
