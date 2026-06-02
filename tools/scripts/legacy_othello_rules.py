"""Legacy Python Othello helpers kept for transitional diagnostics only."""

from __future__ import annotations

from dataclasses import dataclass

from common import ScriptError


FILES = "abcdefgh"
RANKS = "12345678"
DIRECTIONS = (
    (-1, -1),
    (0, -1),
    (1, -1),
    (-1, 0),
    (1, 0),
    (-1, 1),
    (0, 1),
    (1, 1),
)


@dataclass(frozen=True)
class BoardState:
    black: frozenset[tuple[int, int]]
    white: frozenset[tuple[int, int]]
    side: str

    def discs(self, side: str) -> frozenset[tuple[int, int]]:
        return self.black if side == "B" else self.white

    def opponent_discs(self, side: str) -> frozenset[tuple[int, int]]:
        return self.white if side == "B" else self.black


def parse_board(text: str) -> BoardState:
    lines = [line for line in text.splitlines() if line]
    if len(lines) != 9:
        raise ScriptError("board text must contain 8 rows and side line")

    black: set[tuple[int, int]] = set()
    white: set[tuple[int, int]] = set()
    for row_index, row in enumerate(lines[:8]):
        if len(row) != 8:
            raise ScriptError("board row must contain 8 columns")
        rank = 8 - row_index
        for file_index, value in enumerate(row):
            file_number = file_index + 1
            if value == "B":
                black.add((file_number, rank))
            elif value == "W":
                white.add((file_number, rank))
            elif value != ".":
                raise ScriptError(f"invalid board character: {value}")

    side_line = lines[8]
    if side_line == "side=B":
        side = "B"
    elif side_line == "side=W":
        side = "W"
    else:
        raise ScriptError("board side line must be side=B or side=W")

    return BoardState(frozenset(black), frozenset(white), side)


def format_board(board: BoardState) -> str:
    rows: list[str] = []
    for rank in range(8, 0, -1):
        row = []
        for file_number in range(1, 9):
            square = (file_number, rank)
            if square in board.black:
                row.append("B")
            elif square in board.white:
                row.append("W")
            else:
                row.append(".")
        rows.append("".join(row))
    rows.append(f"side={board.side}")
    return "\n".join(rows)


def opponent(side: str) -> str:
    return "W" if side == "B" else "B"


def parse_move(move: str) -> tuple[int, int]:
    if len(move) != 2 or move[0] not in FILES or move[1] not in RANKS:
        raise ScriptError(f"invalid move token: {move}")
    return (FILES.index(move[0]) + 1, RANKS.index(move[1]) + 1)


def move_to_string(square: tuple[int, int]) -> str:
    return f"{FILES[square[0] - 1]}{RANKS[square[1] - 1]}"


def on_board(square: tuple[int, int]) -> bool:
    return 1 <= square[0] <= 8 and 1 <= square[1] <= 8


def flips_for_move(board: BoardState, square: tuple[int, int]) -> set[tuple[int, int]]:
    if square in board.black or square in board.white:
        return set()

    own = board.discs(board.side)
    theirs = board.opponent_discs(board.side)
    flips: set[tuple[int, int]] = set()

    for delta_file, delta_rank in DIRECTIONS:
        current = (square[0] + delta_file, square[1] + delta_rank)
        line: list[tuple[int, int]] = []
        while on_board(current) and current in theirs:
            line.append(current)
            current = (current[0] + delta_file, current[1] + delta_rank)
        if line and on_board(current) and current in own:
            flips.update(line)

    return flips


def legal_moves(board: BoardState) -> set[tuple[int, int]]:
    occupied = board.black | board.white
    moves: set[tuple[int, int]] = set()
    for file_number in range(1, 9):
        for rank in range(1, 9):
            square = (file_number, rank)
            if square not in occupied and flips_for_move(board, square):
                moves.add(square)
    return moves


def pass_turn(board: BoardState) -> BoardState:
    if legal_moves(board):
        raise ScriptError("cannot pass while legal moves exist")
    passed = BoardState(board.black, board.white, opponent(board.side))
    if not legal_moves(passed):
        raise ScriptError("cannot pass from a terminal board")
    return passed


def apply_move(board: BoardState, move: str) -> BoardState:
    if move == "pass":
        return pass_turn(board)

    square = parse_move(move)
    flips = flips_for_move(board, square)
    if not flips:
        raise ScriptError(f"illegal move {move} for board:\n{format_board(board)}")

    black = set(board.black)
    white = set(board.white)
    own = black if board.side == "B" else white
    theirs = white if board.side == "B" else black
    own.add(square)
    own.update(flips)
    theirs.difference_update(flips)
    return BoardState(frozenset(black), frozenset(white), opponent(board.side))
