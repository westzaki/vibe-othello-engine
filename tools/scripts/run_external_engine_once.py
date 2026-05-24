#!/usr/bin/env python3
"""Run one external engine request through the generic adapter."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from common import ScriptError
from external_engines.one_shot import request_best_move
from external_engines.common import ExternalEngineError


def _parse_env(values: list[str]) -> dict[str, str]:
    environment: dict[str, str] = {}
    for value in values:
        key, separator, env_value = value.partition("=")
        if not separator or key == "":
            raise ScriptError(f"--env must be KEY=VALUE, got: {value}")
        environment[key] = env_value
    return environment


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    values = list(sys.argv[1:] if argv is None else argv)
    if "--engine-cmd" in values:
        engine_index = values.index("--engine-cmd")
        adapter_args = values[:engine_index]
        engine_args = values[engine_index + 1 :]
    else:
        adapter_args = values
        engine_args = []

    parser = argparse.ArgumentParser(
        description="Run one external engine request through the one-shot adapter.",
        usage=(
            "%(prog)s (--board-file PATH | --stdin-board) [--timeout-ms N] "
            "[--workdir PATH] [--env KEY=VALUE] --engine-cmd -- COMMAND [ARGS...]"
        ),
    )
    board_input = parser.add_mutually_exclusive_group(required=True)
    board_input.add_argument("--board-file", help="read board text from a file")
    board_input.add_argument("--stdin-board", action="store_true", help="read board text from stdin")
    parser.add_argument("--timeout-ms", type=int, default=1000, help="engine timeout in milliseconds")
    parser.add_argument("--workdir", help="working directory for the engine process")
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="environment override for the engine process; repeatable",
    )

    args = parser.parse_args(adapter_args)

    if "--engine-cmd" not in values:
        raise ScriptError("--engine-cmd is required")
    if not engine_args or engine_args[0] != "--" or len(engine_args) == 1:
        raise ScriptError("--engine-cmd must be followed by '--' and a command")

    if args.timeout_ms <= 0:
        raise ScriptError("--timeout-ms must be positive")

    args.engine_command = engine_args[1:]
    args.env_overrides = _parse_env(args.env)
    return args


def read_board_text(args: argparse.Namespace) -> str:
    if args.stdin_board:
        return sys.stdin.read()
    try:
        return Path(args.board_file).read_text(encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to read board file: {args.board_file}: {exc}") from exc


def print_result(result: object) -> None:
    move = getattr(result, "move")
    raw_error = getattr(result, "raw_error")
    error = getattr(result, "error")
    print(f"move: {move if move is not None else '-'}")
    print(f"elapsed_ms: {getattr(result, 'elapsed_ms'):.2f}")
    print(f"exit_code: {getattr(result, 'exit_code')}")
    print(f"timed_out: {'true' if getattr(result, 'timed_out') else 'false'}")
    print(f"error: {error if error else '-'}")
    print(f"stderr: {raw_error.strip() if raw_error else '-'}")


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        board_text = read_board_text(args)
        result = request_best_move(
            args.engine_command,
            board_text=board_text,
            timeout_ms=args.timeout_ms,
            workdir=args.workdir,
            env=args.env_overrides,
        )
        print_result(result)
        if result.timed_out or result.exit_code != 0 or result.move is None:
            return 1
        return 0
    except (ScriptError, ExternalEngineError) as exc:
        print(exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
