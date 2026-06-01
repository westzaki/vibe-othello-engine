#!/usr/bin/env python3
"""Fake persistent NBoard engine used by workflow tests."""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fake persistent NBoard engine.")
    parser.add_argument("--counter-file")
    parser.add_argument("--sleep-ms", type=int, default=0)
    return parser.parse_args()


def append_counter(path: str | None, event: str) -> None:
    if not path:
        return
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with Path(path).open("a", encoding="utf-8") as output:
        output.write(f"{os.getpid()}\t{event}\n")


def main() -> int:
    args = parse_args()
    append_counter(args.counter_file, "start")
    current_game = ""
    for raw_line in sys.stdin:
        line = raw_line.rstrip("\n")
        if line.startswith("set game "):
            current_game = line
        elif line.startswith("ping "):
            print("pong " + line.split(maxsplit=1)[1], flush=True)
        elif line == "go":
            if "HANG" in current_game:
                time.sleep(10)
            if args.sleep_ms:
                time.sleep(args.sleep_ms / 1000.0)
            if "EXIT" in current_game:
                print("exiting", file=sys.stderr, flush=True)
                return 7
            print("search C4       +1.00 0   1", flush=True)
            if "BADTOKEN" in current_game:
                print("=== z9", flush=True)
            else:
                print("=== d3//0.00/0.01", flush=True)
            append_counter(args.counter_file, "request")
        elif line == "quit":
            append_counter(args.counter_file, "quit")
            return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
