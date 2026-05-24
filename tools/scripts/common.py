#!/usr/bin/env python3
"""Small shared helpers for experimental tool orchestration scripts."""

from __future__ import annotations

import shlex
import subprocess
from collections.abc import Sequence


class ScriptError(Exception):
    """User-facing script error with an intended process exit code."""

    def __init__(self, message: str, exit_code: int = 2) -> None:
        super().__init__(message)
        self.exit_code = exit_code


def parse_bool(value: str) -> bool:
    if value == "true":
        return True
    if value == "false":
        return False
    raise ScriptError(f"expected true or false, got: {value}")


def quote_command(command: Sequence[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def run_command(command: Sequence[str]) -> int:
    completed = subprocess.run(command, check=False)
    return int(completed.returncode)
