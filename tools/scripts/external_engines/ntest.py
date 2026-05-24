"""NTest external engine adapter proof-of-life.

Local NTest builds expose an NBoard-style external viewer protocol in ``x``
mode. This module supports that protocol by launching one NTest process per
request, sending a small NBoard command script on stdin, and parsing the
``=== MOVE`` response from stdout. That keeps the process/protocol work in the
Python orchestration layer instead of the C++ engine core.

For local wrappers that already convert a board to a single stdout move, the
``one-shot`` profile remains available.
"""

from __future__ import annotations

import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field

from external_engines.common import EngineMoveResult, ExternalEngineError, run_external_process
from external_engines.one_shot import request_best_move as request_one_shot_best_move


_MOVE_TOKEN_PATTERN = re.compile(r"\b(?:[a-h][1-8]|pass)\b", re.IGNORECASE)
_NBOARD_MOVE_PATTERN = re.compile(r"^===\s+(\S+)")


@dataclass(frozen=True)
class NTestConfig:
    command: Sequence[str]
    timeout_ms: int = 1000
    workdir: str | None = None
    env: Mapping[str, str] = field(default_factory=dict)
    profile: str = "nboard"
    depth: int = 26


def normalize_ntest_move(value: str) -> str | None:
    stripped = value.strip()
    if re.fullmatch(r"[a-h][1-8]", stripped, flags=re.IGNORECASE):
        return stripped.lower()
    if stripped.lower() == "pass":
        return "pass"
    return None


def parse_ntest_move_output(output: str) -> str | None:
    for line in output.splitlines():
        match = _NBOARD_MOVE_PATTERN.match(line.strip())
        if match is None:
            continue
        raw_move = match.group(1).split("/", maxsplit=1)[0]
        normalized = normalize_ntest_move(raw_move)
        if normalized is not None:
            return normalized

    for match in _MOVE_TOKEN_PATTERN.finditer(output):
        normalized = normalize_ntest_move(match.group(0))
        if normalized is not None:
            return normalized
    return None


def _one_shot_best_move(config: NTestConfig, board_text: str) -> EngineMoveResult:
    result = request_one_shot_best_move(
        config.command,
        board_text=board_text,
        timeout_ms=config.timeout_ms,
        workdir=config.workdir,
        env=config.env,
    )
    if result.timed_out or result.exit_code != 0:
        return result

    move = parse_ntest_move_output(result.raw_output)
    if move is None:
        return EngineMoveResult(
            move=None,
            raw_output=result.raw_output,
            raw_error=result.raw_error,
            exit_code=result.exit_code,
            elapsed_ms=result.elapsed_ms,
            timed_out=result.timed_out,
            error="ntest produced no recognizable move",
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


def _nboard_script(board_text: str, depth: int) -> str:
    game = board_text.strip()
    if not game:
        raise ExternalEngineError("NTest board text must not be empty")
    if depth <= 0:
        raise ExternalEngineError("NTest depth must be positive")
    return "\n".join(
        [
            "nboard 2",
            f"set depth {depth}",
            f"set game {game}",
            "ping 1",
            "go",
            "quit",
            "",
        ]
    )


def _nboard_best_move(config: NTestConfig, board_text: str) -> EngineMoveResult:
    result = run_external_process(
        config.command,
        stdin_text=_nboard_script(board_text, config.depth),
        timeout_ms=config.timeout_ms,
        workdir=config.workdir,
        env=config.env,
    )
    if result.timed_out or result.exit_code != 0:
        return result

    move = parse_ntest_move_output(result.raw_output)
    if move is None:
        return EngineMoveResult(
            move=None,
            raw_output=result.raw_output,
            raw_error=result.raw_error,
            exit_code=result.exit_code,
            elapsed_ms=result.elapsed_ms,
            timed_out=result.timed_out,
            error="ntest produced no recognizable move",
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


def request_best_move(config: NTestConfig, board_text: str) -> EngineMoveResult:
    if config.profile in {"one-shot", "one-shot-stdin-stdout"}:
        return _one_shot_best_move(config, board_text)
    if config.profile in {"nboard", "nboard-pipe"}:
        return _nboard_best_move(config, board_text)
    raise ExternalEngineError(f"unknown NTest protocol profile: {config.profile}")
