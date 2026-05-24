"""Shared helpers for external Othello engine process adapters."""

from __future__ import annotations

import subprocess
import time
import os
from collections.abc import Mapping
from collections.abc import Sequence
from dataclasses import dataclass


@dataclass(frozen=True)
class EngineMoveResult:
    move: str | None
    raw_output: str
    raw_error: str
    exit_code: int
    elapsed_ms: float
    timed_out: bool
    error: str | None = None


class ExternalEngineError(Exception):
    """Raised for invalid adapter usage or external engine setup errors."""


def _text_or_empty(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def _build_env(overrides: Mapping[str, str] | None) -> dict[str, str] | None:
    if overrides is None:
        return None
    environment = dict(os.environ)
    environment.update(overrides)
    return environment


def run_external_process(
    command: Sequence[str],
    *,
    stdin_text: str,
    timeout_ms: int,
    workdir: str | None = None,
    env: Mapping[str, str] | None = None,
) -> EngineMoveResult:
    if not command:
        raise ExternalEngineError("engine command must not be empty")
    if timeout_ms <= 0:
        raise ExternalEngineError("timeout-ms must be positive")

    started = time.monotonic()
    try:
        completed = subprocess.run(
            list(command),
            input=stdin_text,
            capture_output=True,
            text=True,
            timeout=timeout_ms / 1000.0,
            check=False,
            cwd=workdir,
            env=_build_env(env),
        )
        elapsed_ms = (time.monotonic() - started) * 1000.0
        return EngineMoveResult(
            move=None,
            raw_output=completed.stdout,
            raw_error=completed.stderr,
            exit_code=int(completed.returncode),
            elapsed_ms=elapsed_ms,
            timed_out=False,
            error=None,
        )
    except subprocess.TimeoutExpired as exc:
        elapsed_ms = (time.monotonic() - started) * 1000.0
        return EngineMoveResult(
            move=None,
            raw_output=_text_or_empty(exc.stdout),
            raw_error=_text_or_empty(exc.stderr),
            exit_code=-1,
            elapsed_ms=elapsed_ms,
            timed_out=True,
            error="engine timed out",
        )
    except OSError as exc:
        raise ExternalEngineError(f"failed to start engine: {exc}") from exc
