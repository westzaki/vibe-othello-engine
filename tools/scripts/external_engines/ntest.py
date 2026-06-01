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
import os
import queue
import subprocess
import threading
import time
from collections.abc import Mapping, Sequence
from collections import deque
from dataclasses import dataclass, field

from external_engines.common import EngineMoveResult, ExternalEngineError, run_external_process
from external_engines.one_shot import request_best_move as request_one_shot_best_move


_MOVE_TOKEN_PATTERN = re.compile(r"\b(?:[a-h][1-8]|pass)\b", re.IGNORECASE)
_NBOARD_MOVE_PATTERN = re.compile(r"^===\s+(\S+)")
_MAX_CAPTURED_CHARS = 65536
_MAX_QUEUED_LINES = 1024
_MAX_LINE_CHARS = 4096


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


def parse_ntest_nboard_final_move(output: str) -> str | None:
    for line in output.splitlines():
        match = _NBOARD_MOVE_PATTERN.match(line.strip())
        if match is None:
            continue
        raw_move = match.group(1).split("/", maxsplit=1)[0]
        return normalize_ntest_move(raw_move)
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


def _build_env(overrides: Mapping[str, str] | None) -> dict[str, str] | None:
    if overrides is None:
        return None
    environment = dict(os.environ)
    environment.update(overrides)
    return environment


class _LineQueue:
    def __init__(self, *, max_lines: int = _MAX_QUEUED_LINES) -> None:
        self._lines: deque[str | None] = deque()
        self._max_lines = max_lines
        self._condition = threading.Condition()

    def put(self, line: str | None) -> None:
        if line is not None and len(line) > _MAX_LINE_CHARS:
            line = line[:_MAX_LINE_CHARS] + "...<truncated>"
        with self._condition:
            while len(self._lines) >= self._max_lines:
                self._lines.popleft()
            self._lines.append(line)
            self._condition.notify()

    def get(self, *, timeout: float) -> str | None:
        deadline = time.monotonic() + timeout
        with self._condition:
            while not self._lines:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise queue.Empty
                self._condition.wait(remaining)
            return self._lines.popleft()

    def drain(self) -> list[str]:
        with self._condition:
            lines = [line for line in self._lines if line is not None]
            self._lines.clear()
            return lines


class _BoundedText:
    def __init__(self, *, max_chars: int = _MAX_CAPTURED_CHARS) -> None:
        self._parts: deque[str] = deque()
        self._max_chars = max_chars
        self._chars = 0
        self._dropped = False

    def append(self, text: str) -> None:
        if not text:
            return
        self._parts.append(text)
        self._chars += len(text)
        while self._chars > self._max_chars and self._parts:
            removed = self._parts.popleft()
            self._chars -= len(removed)
            self._dropped = True

    def text(self) -> str:
        prefix = "<truncated earlier output>\n" if self._dropped else ""
        return prefix + "".join(self._parts)


class PersistentNTestWorker:
    """Persistent NBoard worker for one NTest process.

    The worker is intentionally narrow: it only supports the NBoard protocol,
    keeps stdout/stderr capture bounded, and restarts after crashes or
    per-position timeouts.
    """

    def __init__(self, config: NTestConfig) -> None:
        if config.profile not in {"nboard", "nboard-pipe"}:
            raise ExternalEngineError("persistent NTest worker only supports nboard protocol")
        if not config.command:
            raise ExternalEngineError("engine command must not be empty")
        if config.timeout_ms <= 0:
            raise ExternalEngineError("timeout-ms must be positive")
        if config.depth <= 0:
            raise ExternalEngineError("NTest depth must be positive")
        self.config = config
        self._process: subprocess.Popen[str] | None = None
        self._stdout = _LineQueue()
        self._stderr = _LineQueue()
        self._request_id = 0

    def _reader(self, stream: object, output: _LineQueue) -> None:
        try:
            for line in stream:  # type: ignore[union-attr]
                output.put(str(line))
        finally:
            output.put(None)

    def _start(self) -> None:
        try:
            self._process = subprocess.Popen(
                list(self.config.command),
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                cwd=self.config.workdir,
                env=_build_env(self.config.env),
            )
        except OSError as exc:
            raise ExternalEngineError(f"failed to start engine: {exc}") from exc
        assert self._process.stdout is not None
        assert self._process.stderr is not None
        threading.Thread(target=self._reader, args=(self._process.stdout, self._stdout), daemon=True).start()
        threading.Thread(target=self._reader, args=(self._process.stderr, self._stderr), daemon=True).start()
        self._write_lines(["nboard 2", f"set depth {self.config.depth}"])

    def _ensure_started(self) -> None:
        if self._process is None or self._process.poll() is not None:
            self._start()

    def _write_lines(self, lines: Sequence[str]) -> None:
        if self._process is None or self._process.stdin is None:
            raise ExternalEngineError("engine process is not running")
        try:
            self._process.stdin.write("\n".join(lines) + "\n")
            self._process.stdin.flush()
        except (BrokenPipeError, OSError) as exc:
            self._terminate()
            raise ExternalEngineError(f"failed to write to engine: {exc}") from exc

    def _terminate(self) -> None:
        process = self._process
        self._process = None
        if process is None:
            return
        if process.poll() is None:
            process.kill()
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=1)
        self._close_process_pipes(process)

    def _close_process_pipes(self, process: subprocess.Popen[str]) -> None:
        for stream in (process.stdin, process.stdout, process.stderr):
            if stream is None or stream.closed:
                continue
            try:
                stream.close()
            except OSError:
                pass

    def shutdown(self) -> None:
        process = self._process
        self._process = None
        if process is None:
            return
        if process.poll() is None and process.stdin is not None:
            try:
                process.stdin.write("quit\n")
                process.stdin.flush()
            except OSError:
                pass
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=1)
        self._close_process_pipes(process)

    def request_best_move(self, board_text: str) -> EngineMoveResult:
        game = board_text.strip()
        if not game:
            raise ExternalEngineError("NTest board text must not be empty")
        self._ensure_started()
        self._stdout.drain()
        self._stderr.drain()
        self._request_id += 1
        started = time.monotonic()
        stdout = _BoundedText()
        stderr = _BoundedText()

        try:
            self._write_lines([f"set game {game}", f"ping {self._request_id}", "go"])
        except ExternalEngineError as exc:
            elapsed_ms = (time.monotonic() - started) * 1000.0
            return EngineMoveResult(
                move=None,
                raw_output="",
                raw_error=str(exc),
                exit_code=-1,
                elapsed_ms=elapsed_ms,
                timed_out=False,
                error=str(exc),
            )

        deadline = started + self.config.timeout_ms / 1000.0
        while True:
            for line in self._stderr.drain():
                stderr.append(line)
            process = self._process
            if process is not None and process.poll() is not None:
                for line in self._stdout.drain():
                    stdout.append(line)
                for line in self._stderr.drain():
                    stderr.append(line)
                elapsed_ms = (time.monotonic() - started) * 1000.0
                self._process = None
                self._close_process_pipes(process)
                return EngineMoveResult(
                    move=parse_ntest_nboard_final_move(stdout.text()),
                    raw_output=stdout.text(),
                    raw_error=stderr.text(),
                    exit_code=int(process.returncode or 0),
                    elapsed_ms=elapsed_ms,
                    timed_out=False,
                    error=None,
                )

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                for line in self._stdout.drain():
                    stdout.append(line)
                for line in self._stderr.drain():
                    stderr.append(line)
                self._terminate()
                elapsed_ms = (time.monotonic() - started) * 1000.0
                return EngineMoveResult(
                    move=None,
                    raw_output=stdout.text(),
                    raw_error=stderr.text(),
                    exit_code=-1,
                    elapsed_ms=elapsed_ms,
                    timed_out=True,
                    error="engine timed out",
                )

            try:
                line = self._stdout.get(timeout=min(0.05, remaining))
            except queue.Empty:
                continue
            if line is None:
                continue
            stdout.append(line)
            if _NBOARD_MOVE_PATTERN.match(line.strip()) is None:
                continue
            move = parse_ntest_nboard_final_move(line)
            elapsed_ms = (time.monotonic() - started) * 1000.0
            for extra in self._stdout.drain():
                stdout.append(extra)
            for extra in self._stderr.drain():
                stderr.append(extra)
            return EngineMoveResult(
                move=move,
                raw_output=stdout.text(),
                raw_error=stderr.text(),
                exit_code=0,
                elapsed_ms=elapsed_ms,
                timed_out=False,
                error=None,
            )

    def __enter__(self) -> PersistentNTestWorker:
        self._ensure_started()
        return self

    def __exit__(self, *_: object) -> bool:
        self.shutdown()
        return False


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
