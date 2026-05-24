#!/usr/bin/env python3
"""Run one NTest best-move request through the proof-of-life adapter."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from common import ScriptError
from external_engines.common import ExternalEngineError
from external_engines.ntest import NTestConfig, request_best_move


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
    if "--ntest-cmd" in values:
        command_index = values.index("--ntest-cmd")
        adapter_args = values[:command_index]
        ntest_args = values[command_index + 1 :]
    else:
        adapter_args = values
        ntest_args = []

    parser = argparse.ArgumentParser(
        description="Run one NTest best-move request.",
        usage=(
            "%(prog)s (--board-file PATH | --stdin-board) [--timeout-ms N] "
            "[--workdir PATH] [--env KEY=VALUE] [--protocol nboard|one-shot] "
            "[--depth N] --ntest-cmd -- COMMAND [ARGS...]"
        ),
    )
    board_input = parser.add_mutually_exclusive_group(required=True)
    board_input.add_argument("--board-file", help="read board text from a file")
    board_input.add_argument("--stdin-board", action="store_true", help="read board text from stdin")
    parser.add_argument("--timeout-ms", type=int, default=1000, help="NTest timeout in milliseconds")
    parser.add_argument("--workdir", help="working directory for the NTest process")
    parser.add_argument(
        "--protocol",
        choices=("nboard", "one-shot"),
        default="nboard",
        help="NTest protocol profile; use one-shot for local wrapper commands",
    )
    parser.add_argument("--depth", type=int, default=26, help="NBoard search depth for the nboard profile")
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="environment override for the NTest process; repeatable",
    )
    args = parser.parse_args(adapter_args)

    if "--ntest-cmd" not in values:
        raise ScriptError("--ntest-cmd is required")
    if not ntest_args or ntest_args[0] != "--" or len(ntest_args) == 1:
        raise ScriptError("--ntest-cmd must be followed by '--' and a command")
    if args.timeout_ms <= 0:
        raise ScriptError("--timeout-ms must be positive")
    if args.depth <= 0:
        raise ScriptError("--depth must be positive")

    args.ntest_command = ntest_args[1:]
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
        config = NTestConfig(
            command=args.ntest_command,
            timeout_ms=args.timeout_ms,
            workdir=args.workdir,
            env=args.env_overrides,
            profile=args.protocol,
            depth=args.depth,
        )
        result = request_best_move(config, board_text)
        print_result(result)
        if result.timed_out or result.exit_code != 0 or result.move is None:
            return 1
        return 0
    except (ScriptError, ExternalEngineError) as exc:
        print(exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
