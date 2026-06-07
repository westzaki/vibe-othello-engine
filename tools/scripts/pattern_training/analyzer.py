from __future__ import annotations

import errno
import json
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from collections.abc import Iterable
from queue import Queue

from common import ScriptError, quote_command
from pattern_training.root_candidates import RootAnalysis, normalize_move, parse_analysis_stdout


ANALYSIS_SPAWN_RETRIES = 5
ANALYSIS_SPAWN_BACKOFF_SECONDS = 0.25


@dataclass(frozen=True)
class AnalyzerConfig:
    analyze_position: Path
    eval_config: Path
    depth: int


def analyze_command(config: AnalyzerConfig) -> list[str]:
    return [
        str(config.analyze_position),
        "--stdin",
        "--depth",
        str(config.depth),
        "--exact-endgame-threshold",
        "0",
        "--eval-config",
        str(config.eval_config),
        "--root-candidates",
    ]


def batch_analyze_command(config: AnalyzerConfig) -> list[str]:
    return [
        str(config.analyze_position),
        "--batch-jsonl",
        "--stdin",
        "--depth",
        str(config.depth),
        "--exact-endgame-threshold",
        "0",
        "--eval-config",
        str(config.eval_config),
        "--root-candidates",
    ]


def run_analysis(config: AnalyzerConfig, board_text: str) -> RootAnalysis:
    command = analyze_command(config)
    completed: subprocess.CompletedProcess[str] | None = None
    for attempt in range(ANALYSIS_SPAWN_RETRIES + 1):
        try:
            completed = subprocess.run(
                command,
                input=board_text,
                check=False,
                capture_output=True,
                text=True,
            )
            break
        except OSError as exc:
            if exc.errno != errno.EAGAIN or attempt >= ANALYSIS_SPAWN_RETRIES:
                raise ScriptError(
                    f"analysis spawn failed: {quote_command(command)}\n{exc}",
                    exit_code=1,
                ) from exc
            time.sleep(ANALYSIS_SPAWN_BACKOFF_SECONDS * (attempt + 1))
    assert completed is not None
    if completed.returncode != 0:
        raise ScriptError(
            f"analysis failed: {quote_command(command)}\n{completed.stderr}",
            exit_code=1,
        )
    return parse_analysis_stdout(completed.stdout)


def _batch_request_line(position_id: str, board_text: str) -> str:
    return json.dumps(
        {"position_id": position_id, "board": board_text},
        sort_keys=True,
        separators=(",", ":"),
    )


def root_analysis_from_batch_row(row: object) -> tuple[str, RootAnalysis]:
    if not isinstance(row, dict):
        raise ScriptError("batch analysis output row must be an object")
    position_id = row.get("position_id")
    if not isinstance(position_id, str) or not position_id:
        raise ScriptError("batch analysis output row is missing position_id")
    status = row.get("status")
    if status != "ok":
        error = row.get("error")
        raise ScriptError(
            f"batch analysis failed for {position_id}: "
            f"{error if isinstance(error, str) and error else status}"
        )
    best_move = normalize_move(row.get("best_move"))
    raw_scores = row.get("root_scores")
    if not isinstance(raw_scores, dict) or not raw_scores:
        raise ScriptError(f"batch analysis output row for {position_id} has no root_scores")
    root_scores: dict[str, int] = {}
    for raw_move, raw_score in raw_scores.items():
        move = normalize_move(raw_move)
        if move is None or not isinstance(raw_score, int):
            raise ScriptError(f"batch analysis output row for {position_id} has invalid root_scores")
        root_scores[move] = raw_score
    return position_id, RootAnalysis(best_move=best_move, root_scores=root_scores)


def run_batch_analysis(
    config: AnalyzerConfig,
    requests: Iterable[tuple[str, str]],
) -> Iterable[tuple[str, RootAnalysis]]:
    command = batch_analyze_command(config)
    try:
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
    except OSError as exc:
        raise ScriptError(f"batch analysis spawn failed: {quote_command(command)}\n{exc}") from exc

    assert process.stdin is not None
    assert process.stdout is not None
    assert process.stderr is not None
    writer_error: list[BaseException] = []

    def write_input() -> None:
        try:
            with process.stdin:
                for position_id, board_text in requests:
                    process.stdin.write(_batch_request_line(position_id, board_text))
                    process.stdin.write("\n")
        except BaseException as exc:  # pragma: no cover - surfaced after process exits.
            writer_error.append(exc)

    writer = threading.Thread(target=write_input, daemon=True)
    writer.start()

    parse_error: BaseException | None = None
    try:
        for line_number, line in enumerate(process.stdout, start=1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as exc:
                parse_error = ScriptError(
                    f"batch analysis output line {line_number}: invalid JSON: {exc.msg}"
                )
                raise parse_error from exc
            try:
                yield root_analysis_from_batch_row(row)
            except BaseException as exc:
                parse_error = exc
                raise
    finally:
        if parse_error is not None and process.poll() is None:
            process.kill()
        writer.join()
        if parse_error is not None:
            process.stderr.read()
            process.wait()

    stderr = process.stderr.read()
    returncode = process.wait()
    if writer_error:
        raise ScriptError(f"batch analysis input write failed: {writer_error[0]}")
    if returncode != 0:
        raise ScriptError(f"batch analysis failed: {quote_command(command)}\n{stderr}")


def run_parallel_batch_analysis(
    config: AnalyzerConfig,
    requests: Iterable[tuple[str, str]],
    *,
    jobs: int,
) -> Iterable[tuple[str, RootAnalysis]]:
    request_list = list(requests)
    if jobs <= 1 or len(request_list) <= 1:
        yield from run_batch_analysis(config, request_list)
        return

    shard_count = min(jobs, len(request_list))
    shards = [request_list[index::shard_count] for index in range(shard_count)]
    output: Queue[tuple[str, tuple[str, RootAnalysis] | BaseException | None]] = Queue()

    def worker(shard: list[tuple[str, str]]) -> None:
        try:
            for row in run_batch_analysis(config, shard):
                output.put(("result", row))
        except BaseException as exc:  # pragma: no cover - exercised through callers.
            output.put(("error", exc))
        finally:
            output.put(("done", None))

    threads = [threading.Thread(target=worker, args=(shard,), daemon=True) for shard in shards]
    for thread in threads:
        thread.start()

    done = 0
    first_error: BaseException | None = None
    while done < len(threads):
        kind, payload = output.get()
        if kind == "done":
            done += 1
            continue
        if kind == "error":
            if first_error is None:
                assert isinstance(payload, BaseException)
                first_error = payload
            continue
        assert payload is not None and not isinstance(payload, BaseException)
        yield payload

    for thread in threads:
        thread.join()
    if first_error is not None:
        raise first_error
