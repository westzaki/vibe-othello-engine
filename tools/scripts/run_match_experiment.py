#!/usr/bin/env python3
"""Thin subprocess wrapper around othello_match_runner."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from common import ScriptError, parse_bool, quote_command, run_command


def parse_arg_bool(value: str) -> bool:
    try:
        return parse_bool(value)
    except ScriptError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def build_runner_command(args: argparse.Namespace) -> list[str]:
    command = [
        args.runner,
        "--black",
        args.black,
        "--white",
        args.white,
        "--games",
        str(args.games),
        "--swap-sides",
        "true" if args.swap_sides else "false",
        "--seed",
        str(args.seed),
    ]
    if args.openings:
        command.extend(["--openings", args.openings])
    command.extend(["--output", args.output])
    return command


def build_summary_command(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, args.summary_script, "--input", args.output]
    if args.allow_errors:
        command.append("--allow-errors")
    if args.by_opening:
        command.append("--by-opening")
    return command


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run an othello_match_runner experiment.")
    parser.add_argument("--runner", required=True, help="path to othello_match_runner")
    parser.add_argument(
        "--summary-script",
        default=str(Path(__file__).with_name("match_summary.py")),
        help="path to tools/scripts/match_summary.py",
    )
    parser.add_argument("--black", required=True, help="black / player A spec")
    parser.add_argument("--white", required=True, help="white / player B spec")
    parser.add_argument("--games", required=True, type=int, help="number of games")
    parser.add_argument("--swap-sides", required=True, type=parse_arg_bool, help="true or false")
    parser.add_argument("--seed", required=True, type=int, help="base random seed")
    parser.add_argument("--openings", help="opening suite text file")
    parser.add_argument("--output", required=True, help="JSONL output path")
    parser.add_argument("--summary", action="store_true", help="run summary script after runner")
    parser.add_argument(
        "--allow-errors",
        action="store_true",
        help="pass --allow-errors to summary script",
    )
    parser.add_argument(
        "--by-opening",
        action="store_true",
        help="pass --by-opening to summary script; requires --summary",
    )
    parser.add_argument("--dry-run", action="store_true", help="print commands without running")
    args = parser.parse_args(argv)
    if args.by_opening and not args.summary:
        parser.error("--by-opening requires --summary")
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        runner_command = build_runner_command(args)
        summary_command = build_summary_command(args) if args.summary else None

        if args.dry_run:
            print(quote_command(runner_command))
            if summary_command is not None:
                print(quote_command(summary_command))
            return 0

        runner_exit_code = run_command(runner_command)
        if runner_exit_code != 0:
            return runner_exit_code

        if summary_command is not None:
            return run_command(summary_command)
        return 0
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
