from __future__ import annotations

import collections
import concurrent.futures
import datetime as dt
import json
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from common import ScriptError
from pattern_training.board9 import board_hash, board_key, sha256_file, sha256_text
from pattern_training.root_candidates import RootAnalysis, normalize_move, parse_analysis_stdout


ROOT_ANALYSIS_CACHE_SCHEMA = "root_analysis_cache.v1"
ANALYSIS_CACHE_MODES = ("off", "read-write", "read-only", "refresh")


@dataclass(frozen=True)
class AnalysisCacheConfig:
    directory: Path | None
    mode: str


@dataclass(frozen=True)
class AnalysisRequest:
    source_index: int
    board_text: str
    cache_key: str
    position_id: str = ""
    source: object | None = None


@dataclass(frozen=True)
class AnalysisRunnerConfig:
    analysis_cache: AnalysisCacheConfig
    analysis_jobs: int
    analyze_position: Path
    eval_config: Path
    analysis_depth: int
    require_stdout_cache: bool = False


Analyzer = Callable[[str], RootAnalysis]


def analysis_cache_key(*, board_hash: str, eval_config_hash: str, analysis_depth: int) -> str:
    material = {
        "schema": ROOT_ANALYSIS_CACHE_SCHEMA,
        "board_hash": board_hash,
        "eval_config_hash": eval_config_hash,
        "analysis_depth": analysis_depth,
    }
    return sha256_text(json.dumps(material, sort_keys=True, separators=(",", ":")))


def analysis_cache_path(cache_dir: Path) -> Path:
    return cache_dir / "root_analysis.jsonl"


def analyze_position_hash(path: Path) -> str | None:
    try:
        if path.is_file():
            return sha256_file(path)
    except ScriptError:
        return None
    return None


def valid_cached_analysis_row(
    row: object,
    *,
    key: str,
    board_hash: str,
    eval_config_hash: str,
    expected_analyze_position_hash: str | None,
    analysis_depth: int,
    require_stdout: bool = False,
) -> RootAnalysis | None:
    if expected_analyze_position_hash is None:
        return None
    if not isinstance(row, dict):
        return None
    if row.get("schema") != ROOT_ANALYSIS_CACHE_SCHEMA:
        return None
    if row.get("key") != key:
        return None
    if row.get("board_hash") != board_hash:
        return None
    if row.get("eval_config_hash") != eval_config_hash:
        return None
    if row.get("analyze_position_hash") != expected_analyze_position_hash:
        return None
    if row.get("analysis_depth") != analysis_depth:
        return None
    if row.get("status") != "ok" or row.get("exit_status") not in (None, 0):
        return None
    stdout = row.get("stdout")
    if isinstance(stdout, str) and stdout:
        return parse_analysis_stdout(stdout)
    if require_stdout:
        return None
    raw_scores = row.get("root_scores")
    if not isinstance(raw_scores, dict) or not raw_scores:
        return None
    root_scores: dict[str, int] = {}
    for move, score in raw_scores.items():
        normalized = normalize_move(move)
        if normalized is None or not isinstance(score, int):
            return None
        root_scores[normalized] = score
    best_move = normalize_move(row.get("best_move"))
    return RootAnalysis(best_move=best_move, root_scores=root_scores)


def load_analysis_cache(
    *,
    cache_dir: Path,
    expected: dict[str, tuple[str, str]],
    eval_config_hash: str,
    expected_analyze_position_hash: str | None,
    analysis_depth: int,
    require_stdout: bool = False,
) -> dict[str, RootAnalysis]:
    path = analysis_cache_path(cache_dir)
    if not path.exists():
        return {}
    loaded: dict[str, RootAnalysis] = {}
    try:
        with path.open("r", encoding="utf-8") as input_file:
            for line in input_file:
                if not line.strip():
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    continue
                key = row.get("key") if isinstance(row, dict) else None
                if not isinstance(key, str) or key not in expected:
                    continue
                expected_board_hash, expected_eval_hash = expected[key]
                if expected_eval_hash != eval_config_hash:
                    continue
                result = valid_cached_analysis_row(
                    row,
                    key=key,
                    board_hash=expected_board_hash,
                    eval_config_hash=eval_config_hash,
                    expected_analyze_position_hash=expected_analyze_position_hash,
                    analysis_depth=analysis_depth,
                    require_stdout=require_stdout,
                )
                if result is not None:
                    loaded[key] = result
    except OSError as exc:
        raise ScriptError(f"failed to read analysis cache {path}: {exc}") from exc
    return loaded


def collect_git_sha() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def write_analysis_cache_row(
    *,
    cache_dir: Path,
    config: AnalysisRunnerConfig,
    request: AnalysisRequest,
    result: RootAnalysis,
    eval_config_hash: str,
    analyzer_hash: str | None,
    git_sha: str,
) -> None:
    cache_dir.mkdir(parents=True, exist_ok=True)
    current_board_hash = board_hash(request.board_text)
    row = {
        "schema": ROOT_ANALYSIS_CACHE_SCHEMA,
        "key": request.cache_key,
        "position_id": request.position_id,
        "board_hash": current_board_hash,
        "board_text_hash": current_board_hash,
        "eval_config": str(config.eval_config),
        "eval_config_hash": eval_config_hash,
        "analyze_position": str(config.analyze_position),
        "analyze_position_hash": analyzer_hash,
        "analysis_depth": config.analysis_depth,
        "best_move": result.best_move,
        "root_scores": result.root_scores,
        "stdout": result.stdout,
        "status": "ok",
        "exit_status": 0,
        "git_sha": git_sha,
        "generated_at": dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    path = analysis_cache_path(cache_dir)
    try:
        with path.open("a", encoding="utf-8") as output_file:
            output_file.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
            output_file.flush()
    except OSError as exc:
        raise ScriptError(f"failed to write analysis cache {path}: {exc}") from exc


def analyze_requests(
    *,
    config: AnalysisRunnerConfig,
    requests: list[AnalysisRequest],
    analyzer: Analyzer,
    stats: collections.Counter[str],
    eval_config_hash: str | None = None,
) -> dict[int, RootAnalysis]:
    start = time.perf_counter()
    try:
        return _analyze_requests(
            config=config,
            requests=requests,
            analyzer=analyzer,
            stats=stats,
            eval_config_hash=eval_config_hash or sha256_file(config.eval_config),
            analyzer_hash=analyze_position_hash(config.analyze_position),
        )
    finally:
        stats["analysis_elapsed_seconds"] = round(time.perf_counter() - start, 6)


def _analyze_requests(
    *,
    config: AnalysisRunnerConfig,
    requests: list[AnalysisRequest],
    analyzer: Analyzer,
    stats: collections.Counter[str],
    eval_config_hash: str,
    analyzer_hash: str | None,
) -> dict[int, RootAnalysis]:
    if not requests:
        return {}
    cache_dir = config.analysis_cache.directory
    cache_mode = config.analysis_cache.mode
    cached_by_key: dict[str, RootAnalysis] = {}
    if cache_dir is not None and cache_mode in {"read-write", "read-only"}:
        expected = {
            request.cache_key: (board_hash(request.board_text), eval_config_hash)
            for request in requests
        }
        cached_by_key = load_analysis_cache(
            cache_dir=cache_dir,
            expected=expected,
            eval_config_hash=eval_config_hash,
            expected_analyze_position_hash=analyzer_hash,
            analysis_depth=config.analysis_depth,
            require_stdout=config.require_stdout_cache,
        )

    results_by_source: dict[int, RootAnalysis] = {}
    missing: list[AnalysisRequest] = []
    for request in requests:
        cached = None if cache_mode == "refresh" else cached_by_key.get(request.cache_key)
        if cached is None:
            missing.append(request)
            continue
        stats["analysis_cache_hits"] += 1
        results_by_source[request.source_index] = cached

    stats["analysis_cache_misses"] += len(missing)
    if missing and cache_mode == "read-only":
        first = missing[0]
        raise ScriptError(
            "analysis cache missing required root analysis for "
            f"{first.position_id or first.source_index} key={first.cache_key}"
        )
    if not missing:
        return results_by_source

    should_write = cache_dir is not None and cache_mode in {"read-write", "refresh"}
    if cache_dir is not None and cache_mode == "refresh":
        path = analysis_cache_path(cache_dir)
        if path.exists():
            try:
                path.unlink()
            except OSError as exc:
                raise ScriptError(f"failed to refresh analysis cache {path}: {exc}") from exc
    git_sha = collect_git_sha() if should_write else "unknown"

    missing_by_key: dict[str, AnalysisRequest] = {}
    source_indices_by_key: dict[str, list[int]] = collections.defaultdict(list)
    for request in missing:
        missing_by_key.setdefault(request.cache_key, request)
        source_indices_by_key[request.cache_key].append(request.source_index)
    unique_missing = list(missing_by_key.values())

    def run_one(request: AnalysisRequest) -> RootAnalysis:
        return analyzer(request.board_text)

    results_by_key: dict[str, RootAnalysis] = {}
    if config.analysis_jobs == 1 or len(unique_missing) == 1:
        for request in unique_missing:
            results_by_key[request.cache_key] = run_one(request)
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=config.analysis_jobs) as executor:
            future_by_key = {
                executor.submit(run_one, request): request.cache_key for request in unique_missing
            }
            for future in concurrent.futures.as_completed(future_by_key):
                results_by_key[future_by_key[future]] = future.result()

    for request in unique_missing:
        result = results_by_key[request.cache_key]
        for source_index in source_indices_by_key[request.cache_key]:
            results_by_source[source_index] = result
        if should_write and cache_dir is not None:
            write_analysis_cache_row(
                cache_dir=cache_dir,
                config=config,
                request=request,
                result=result,
                eval_config_hash=eval_config_hash,
                analyzer_hash=analyzer_hash,
                git_sha=git_sha,
            )
            stats["analysis_cache_writes"] += 1
    return results_by_source
