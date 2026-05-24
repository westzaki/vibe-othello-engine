#!/usr/bin/env python3
"""Fake external engine used by adapter tests."""

from __future__ import annotations

import argparse
import os
import sys
import time


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fake external Othello engine.")
    parser.add_argument("--move", help="move string to print to stdout")
    parser.add_argument("--sleep-ms", type=int, default=0, help="delay before producing output")
    parser.add_argument("--exit-code", type=int, default=0, help="process exit code")
    parser.add_argument("--stderr", default="", help="text to print to stderr")
    parser.add_argument("--print-env", help="print an environment variable as the move")
    parser.add_argument("--print-cwd", action="store_true", help="print the current working directory")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    sys.stdin.read()

    if args.sleep_ms > 0:
        time.sleep(args.sleep_ms / 1000.0)

    if args.stderr:
        print(args.stderr, file=sys.stderr)
    if args.print_env:
        print(os.environ.get(args.print_env, ""))
    elif args.print_cwd:
        print(os.getcwd())
    elif args.move is not None:
        print(args.move)
    return int(args.exit_code)


if __name__ == "__main__":
    raise SystemExit(main())
