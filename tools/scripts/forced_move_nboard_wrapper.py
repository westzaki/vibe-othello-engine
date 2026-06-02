#!/usr/bin/env python3
"""Wrap an NBoard engine and force one move when a target board is reached."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import threading
from pathlib import Path

from common import ScriptError
from legacy_othello_rules import (
    BoardState,
    apply_move,
    format_board,
    legal_moves,
    move_to_string,
    pass_turn,
    parse_board,
    parse_move,
)


INITIAL_BOARD = "\n".join(
    (
        "........",
        "........",
        "........",
        "...BW...",
        "...WB...",
        "........",
        "........",
        "........",
        "side=B",
    )
)

MOVE_PROPERTY_RE = re.compile(r"(?<![A-Za-z])([BW])\[([^\]]*)\]")


def normalize_board_text(text: str) -> str:
    return format_board(parse_board(text))


def extract_moves_from_game_text(text: str) -> list[str]:
    stripped = text.strip()
    if not stripped:
        return []
    if stripped.startswith("(;"):
        moves: list[str] = []
        for match in MOVE_PROPERTY_RE.finditer(stripped):
            token = match.group(2).strip().lower()
            moves.append("pass" if token == "pa" else token)
        return moves
    return [part.strip().lower() for part in stripped.split() if part.strip()]


def board_after_game_text(text: str) -> BoardState:
    board = parse_board(INITIAL_BOARD)
    for move in extract_moves_from_game_text(text):
        board = pass_turn(board) if move == "pass" else apply_move(board, move)
    return board


def command_tail(line: str, command: str) -> str | None:
    if line == command:
        return ""
    prefix = f"{command} "
    if line.startswith(prefix):
        return line[len(prefix) :]
    return None


def forced_move_for_board(board: BoardState, target_board_text: str, force_move: str) -> str | None:
    if format_board(board) != target_board_text:
        return None
    square = parse_move(force_move)
    if square not in legal_moves(board):
        raise ScriptError(f"forced move {force_move} is illegal for target board")
    return move_to_string(square)


def apply_response_to_board(board: BoardState, move: str) -> BoardState:
    return pass_turn(board) if move == "pass" else apply_move(board, move)


def pump_child_stdout(child: subprocess.Popen[str]) -> None:
    assert child.stdout is not None
    for line in child.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()


def run_wrapper(args: argparse.Namespace) -> int:
    if not args.engine_command:
        raise ScriptError("missing child engine command after --")

    target_board_text = normalize_board_text(args.target_board_file.read_text(encoding="utf-8"))
    force_move = args.force_move.lower()
    parse_move(force_move)

    child = subprocess.Popen(
        args.engine_command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=None,
        text=True,
        bufsize=1,
    )
    assert child.stdin is not None
    stdout_thread = threading.Thread(target=pump_child_stdout, args=(child,), daemon=True)
    stdout_thread.start()

    board = parse_board(INITIAL_BOARD)
    forced_count = 0

    try:
        for raw_line in sys.stdin:
            line = raw_line.rstrip("\n")
            game_tail = command_tail(line.strip(), "set game")
            if game_tail is not None:
                board = board_after_game_text(game_tail)
                child.stdin.write(raw_line)
                child.stdin.flush()
                continue

            move_tail = command_tail(line.strip(), "move")
            usermove_tail = command_tail(line.strip(), "usermove")
            if move_tail is not None or usermove_tail is not None:
                token = move_tail if move_tail is not None else usermove_tail
                assert token is not None
                board = apply_response_to_board(board, token.strip().lower())
                child.stdin.write(raw_line)
                child.stdin.flush()
                continue

            if line.strip() == "go":
                forced = forced_move_for_board(board, target_board_text, force_move)
                if forced is not None:
                    forced_count += 1
                    print(f"=== {forced}", flush=True)
                    continue

            child.stdin.write(raw_line)
            child.stdin.flush()
            if line.strip() in {"quit", "exit"}:
                break
    finally:
        try:
            child.stdin.close()
        except OSError:
            pass
        child.wait(timeout=5)
        if args.verbose:
            print(f"forced_move_wrapper: forced_count={forced_count}", file=sys.stderr)

    return int(child.returncode or 0)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-board-file", required=True, type=Path)
    parser.add_argument("--force-move", required=True)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("engine_command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if args.engine_command and args.engine_command[0] == "--":
        args.engine_command = args.engine_command[1:]
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        return run_wrapper(parse_args(sys.argv[1:] if argv is None else argv))
    except ScriptError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
