"""One-shot external engine adapter scaffolding."""

from __future__ import annotations

import re
from collections.abc import Mapping, Sequence

from external_engines.common import EngineMoveResult, run_external_process


_MOVE_PATTERN = re.compile(r"^[a-h][1-8]$", re.IGNORECASE)


def first_non_empty_line(text: str) -> str | None:
    for line in text.splitlines():
        stripped = line.strip()
        if stripped:
            return stripped
    return None


def normalize_move_token(value: str) -> str | None:
    if _MOVE_PATTERN.fullmatch(value):
        return value.lower()
    if value.lower() == "pass":
        return "pass"
    return None


def request_best_move(
    command: Sequence[str],
    *,
    board_text: str,
    timeout_ms: int,
    workdir: str | None = None,
    env: Mapping[str, str] | None = None,
) -> EngineMoveResult:
    result = run_external_process(
        command,
        stdin_text=board_text,
        timeout_ms=timeout_ms,
        workdir=workdir,
        env=env,
    )
    if result.timed_out or result.exit_code != 0:
        return result

    raw_move = first_non_empty_line(result.raw_output)
    if raw_move is None:
        return EngineMoveResult(
            move=None,
            raw_output=result.raw_output,
            raw_error=result.raw_error,
            exit_code=result.exit_code,
            elapsed_ms=result.elapsed_ms,
            timed_out=result.timed_out,
            error="engine produced no move",
        )

    move = normalize_move_token(raw_move)
    if move is None:
        return EngineMoveResult(
            move=None,
            raw_output=result.raw_output,
            raw_error=result.raw_error,
            exit_code=result.exit_code,
            elapsed_ms=result.elapsed_ms,
            timed_out=result.timed_out,
            error=f"invalid move format: {raw_move}",
        )

    return EngineMoveResult(
        move=move,
        raw_output=result.raw_output,
        raw_error=result.raw_error,
        exit_code=result.exit_code,
        elapsed_ms=result.elapsed_ms,
        timed_out=result.timed_out,
        error=None,
    )
