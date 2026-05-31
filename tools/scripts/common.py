#!/usr/bin/env python3
"""Small shared helpers for experimental tool orchestration scripts."""

from __future__ import annotations

import json
import shlex
import subprocess
from collections.abc import Sequence
from pathlib import Path
from typing import Any


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


def parse_csv_values(
    value: str,
    *,
    error_label: str = "CSV list",
    empty_segment_message: str | None = None,
) -> list[str]:
    parts = [part.strip() for part in value.split(",")]
    if not parts or any(not part for part in parts):
        if empty_segment_message is not None:
            raise ScriptError(empty_segment_message)
        raise ScriptError(f"invalid {error_label}: {value}")
    return parts


def parse_csv_paths(
    value: str,
    *,
    error_label: str = "CSV path list",
    empty_segment_message: str | None = None,
) -> list[Path]:
    return [
        Path(part)
        for part in parse_csv_values(
            value,
            error_label=error_label,
            empty_segment_message=empty_segment_message,
        )
    ]


def slugify(value: str, *, fallback: str = "target") -> str:
    slug = "".join(char if char.isalnum() or char in ("-", "_") else "-" for char in value)
    return slug.strip("-") or fallback


def read_jsonl(
    path: Path,
    *,
    require_object: bool = False,
    empty_error: str | None = None,
    json_error_format: str = "line {line_number}: invalid JSON: {message}",
) -> list[Any]:
    records: list[Any] = []
    try:
        with path.open("r", encoding="utf-8") as input_file:
            for line_number, line in enumerate(input_file, start=1):
                if not line.strip():
                    continue
                try:
                    value = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ScriptError(
                        json_error_format.format(
                            line_number=line_number,
                            message=exc.msg,
                            error=exc,
                        )
                    ) from exc
                if require_object and not isinstance(value, dict):
                    raise ScriptError(f"line {line_number}: record must be an object")
                records.append(value)
    except OSError as exc:
        raise ScriptError(f"failed to read {path}: {exc}") from exc

    if empty_error is not None and not records:
        raise ScriptError(empty_error.format(path=path))
    return records


def write_report_section(lines: list[str], heading: str, body: str | Sequence[str]) -> None:
    lines.extend((f"## {heading}", ""))
    if isinstance(body, str):
        lines.append(body)
    else:
        lines.extend(body)
    lines.append("")


def quote_command(command: Sequence[str]) -> str:
    return " ".join(shlex.quote(part) for part in command)


def run_command(command: Sequence[str]) -> int:
    completed = subprocess.run(command, check=False)
    return int(completed.returncode)
