#!/usr/bin/env python3
"""Extract first base/head divergence positions from match-runner JSONL."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, cast

from common import ScriptError, read_jsonl
from legacy_othello_rules import (
    BoardState,
    apply_move,
    format_board,
    legal_moves,
    parse_board,
    pass_turn,
)


@dataclass(frozen=True)
class ReplayResult:
    board: BoardState
    turns: int
    passes: int


@dataclass(frozen=True)
class Divergence:
    pair_index: int
    opening_index: int
    opening_name: str
    head_game_index: int
    base_game_index: int
    ply: int
    side_to_move: str
    board_text: str
    head_move: str
    base_move: str
    head_final_diff: int
    base_game_head_final_diff: int
    illegal_or_error: bool
    preceding_moves: list[str]


def replay_moves(start_board: str, moves: list[str]) -> ReplayResult:
    board = parse_board(start_board)
    turns = 0
    passes = 0
    for move in moves:
        while not legal_moves(board):
            board = pass_turn(board)
            turns += 1
            passes += 1
        board = apply_move(board, move)
        turns += 1
    return ReplayResult(board=board, turns=turns, passes=passes)


def require_list_of_strings(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ScriptError(f"{label} must be a list of strings")
    return list(value)


def load_records(path: Path) -> list[dict[str, Any]]:
    return cast(
        list[dict[str, Any]],
        read_jsonl(
            path,
            require_object=True,
            empty_error="input file contains no records: {path}",
            json_error_format="invalid JSONL: {error}",
        ),
    )


def side_player(record: dict[str, Any], side: str) -> str:
    black_is_player_a = bool(record["black_is_player_a"])
    side_is_player_a = black_is_player_a if side == "B" else not black_is_player_a
    return "head" if side_is_player_a else "base"


def head_diff(record: dict[str, Any]) -> int:
    return int(record["score_diff_from_player_a"])


def first_different_move_index(first: list[str], second: list[str]) -> int | None:
    limit = min(len(first), len(second))
    for index in range(limit):
        if first[index] != second[index]:
            return index
    if len(first) != len(second):
        return limit
    return None


def check_pair(first: dict[str, Any], second: dict[str, Any]) -> None:
    for key in ("opening_index", "opening_name", "start_board"):
        if first.get(key) != second.get(key):
            raise ScriptError(f"paired records do not share {key}")
    if require_list_of_strings(first.get("opening_moves"), "opening_moves") != require_list_of_strings(
        second.get("opening_moves"), "opening_moves"
    ):
        raise ScriptError("paired records do not share opening_moves")


def extract_pair_divergence(pair_index: int, first: dict[str, Any], second: dict[str, Any]) -> Divergence | None:
    check_pair(first, second)
    first_moves = require_list_of_strings(first.get("moves"), "moves")
    second_moves = require_list_of_strings(second.get("moves"), "moves")
    divergence_index = first_different_move_index(first_moves, second_moves)
    if divergence_index is None:
        return None

    common_moves = first_moves[:divergence_index]
    replay = replay_moves(str(first["start_board"]), common_moves)
    side = replay.board.side
    first_player = side_player(first, side)
    second_player = side_player(second, side)
    if {first_player, second_player} != {"head", "base"}:
        raise ScriptError("paired records do not compare head and base at divergence")

    first_move = first_moves[divergence_index] if divergence_index < len(first_moves) else "none"
    second_move = second_moves[divergence_index] if divergence_index < len(second_moves) else "none"
    head_move = first_move if first_player == "head" else second_move
    base_move = first_move if first_player == "base" else second_move
    head_record = first if first_player == "head" else second
    base_record = first if first_player == "base" else second
    opening_moves = require_list_of_strings(first.get("opening_moves"), "opening_moves")

    return Divergence(
        pair_index=pair_index,
        opening_index=int(first["opening_index"]),
        opening_name=str(first["opening_name"]),
        head_game_index=int(head_record["game_index"]),
        base_game_index=int(base_record["game_index"]),
        ply=len(opening_moves) + replay.turns,
        side_to_move="black" if side == "B" else "white",
        board_text=format_board(replay.board),
        head_move=head_move,
        base_move=base_move,
        head_final_diff=head_diff(head_record),
        base_game_head_final_diff=head_diff(base_record),
        illegal_or_error=bool(first.get("illegal_or_error")) or bool(second.get("illegal_or_error")),
        preceding_moves=[*opening_moves, *common_moves],
    )


def extract_divergences(records: list[dict[str, Any]]) -> list[Divergence]:
    if len(records) % 2 != 0:
        raise ScriptError("expected an even number of records from a swap-side matrix")

    divergences: list[Divergence] = []
    for index in range(0, len(records), 2):
        divergence = extract_pair_divergence(index // 2, records[index], records[index + 1])
        if divergence is not None:
            divergences.append(divergence)
    return divergences


def render_markdown(divergences: list[Divergence]) -> str:
    lines = [
        "| pair | opening | head game | base game | ply | side | head move | base move | head diff | paired head diff | error | preceding moves |",
        "| ---: | :--- | ---: | ---: | ---: | :--- | :--- | :--- | ---: | ---: | :--- | :--- |",
    ]
    for divergence in divergences:
        preceding = " ".join(divergence.preceding_moves) if divergence.preceding_moves else "(none)"
        lines.append(
            "| "
            + " | ".join(
                (
                    str(divergence.pair_index),
                    divergence.opening_name,
                    str(divergence.head_game_index),
                    str(divergence.base_game_index),
                    str(divergence.ply),
                    divergence.side_to_move,
                    divergence.head_move,
                    divergence.base_move,
                    str(divergence.head_final_diff),
                    str(divergence.base_game_head_final_diff),
                    "yes" if divergence.illegal_or_error else "no",
                    preceding,
                )
            )
            + " |"
        )
    return "\n".join(lines) + "\n"


def render_jsonl(divergences: list[Divergence]) -> str:
    lines = []
    for divergence in divergences:
        lines.append(json.dumps(divergence.__dict__, sort_keys=True))
    return "\n".join(lines) + ("\n" if lines else "")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="match-runner JSONL file")
    parser.add_argument(
        "--format",
        choices=("markdown", "jsonl"),
        default="markdown",
        help="output format",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        divergences = extract_divergences(load_records(args.input))
        if args.format == "jsonl":
            sys.stdout.write(render_jsonl(divergences))
        else:
            sys.stdout.write(render_markdown(divergences))
    except ScriptError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.exit_code
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
