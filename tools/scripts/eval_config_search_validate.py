#!/usr/bin/env python3
"""Run search bench and match-smoke validation for .eval candidates."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import subprocess
import sys
from dataclasses import dataclass, replace
from pathlib import Path

from common import ScriptError, parse_csv_paths, quote_command, slugify
from eval_config_tuner import parse_non_negative_int, parse_positive_int, sha256_file


DEFAULT_DEPTHS = "4,5"
DEFAULT_GAMES = 4
DEFAULT_SEED = 20260531
DEFAULT_EXACT_ENDGAME_THRESHOLD = 0
DEFAULT_POSITIONS = "smoke"


@dataclass(frozen=True)
class HeldoutRow:
    rank: str
    candidate: str
    status: str
    objective: str
    delta_vs_base: str
    config: str

    @property
    def numeric_rank(self) -> int | None:
        try:
            return int(self.rank)
        except ValueError:
            return None

    @property
    def numeric_objective(self) -> int | None:
        try:
            return int(self.objective)
        except ValueError:
            return None


@dataclass(frozen=True)
class HeldoutSummary:
    rows: tuple[HeldoutRow, ...]
    by_candidate: dict[str, tuple[HeldoutRow, ...]]
    by_config_path: dict[Path, tuple[HeldoutRow, ...]]
    by_config_stem: dict[str, tuple[HeldoutRow, ...]]
    missing_config_rows: tuple[HeldoutRow, ...]


@dataclass(frozen=True)
class Candidate:
    candidate_id: str
    config_path: Path
    slug: str
    heldout_row: HeldoutRow | None = None
    heldout_note: str = ""
    selected: bool = False


@dataclass(frozen=True)
class SearchMetrics:
    result_checksum: str
    work_checksum: str
    nodes: str
    elapsed_ms: str


@dataclass(frozen=True)
class SearchResult:
    candidate_id: str
    config_path: Path
    command: list[str]
    log_path: Path
    status: str
    metrics: SearchMetrics | None = None
    exit_code: int | None = None
    notes: str = ""
    error: str = ""


@dataclass(frozen=True)
class MatchResult:
    candidate_id: str
    config_path: Path
    command: list[str]
    summary_command: list[str]
    log_path: Path
    summary_log_path: Path
    jsonl_path: Path
    summary_path: Path
    status: str
    summary_text: str = ""
    exit_code: int | None = None
    summary_exit_code: int | None = None
    notes: str = ""
    error: str = ""


@dataclass(frozen=True)
class ValidationConfig:
    base_config: Path
    candidate_configs: tuple[Path, ...]
    candidate_source: str
    heldout_summary: Path | None
    build_dir: Path
    out_dir: Path
    top: int
    run_search_bench: bool
    run_match_smoke: bool
    search_bench: Path
    match_runner: Path
    match_summary: Path
    openings: Path
    depths: str
    games: int
    seed: int
    exact_endgame_threshold: int
    dry_run: bool
    allow_failures: bool
    invocation: list[str]


@dataclass(frozen=True)
class Metadata:
    timestamp: str
    git_sha: str
    base_config_sha256: str


def default_out_dir() -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("runs") / "eval-config-search-validation" / timestamp


def parse_depth_list(value: str) -> list[int]:
    depths: list[int] = []
    for raw_part in value.split(","):
        part = raw_part.strip()
        if not part:
            raise ScriptError(f"invalid depth list: {value}")
        try:
            depth = int(part)
        except ValueError as exc:
            raise ScriptError(f"invalid depth value: {part}") from exc
        if depth <= 0:
            raise ScriptError(f"depth must be positive: {depth}")
        depths.append(depth)
    if not depths:
        raise ScriptError("depth list must not be empty")
    return depths


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run search/match validation for experimental .eval configs."
    )
    parser.add_argument("--base-config", required=True, help="fully expanded base .eval config")
    parser.add_argument("--candidate-configs", help="comma-separated candidate .eval paths")
    parser.add_argument("--candidate-dir", help="directory containing candidate *.eval files")
    parser.add_argument("--heldout-summary", help="optional held-out validation summary.tsv")
    parser.add_argument("--build-dir", default="build", help="directory containing C++ tools")
    parser.add_argument(
        "--out",
        default=None,
        help="output directory (default: runs/eval-config-search-validation/<timestamp>)",
    )
    parser.add_argument("--top", type=parse_positive_int, default=3)
    parser.add_argument("--run-search-bench", action="store_true")
    parser.add_argument("--run-match-smoke", action="store_true")
    parser.add_argument("--search-bench", help="override othello_search_bench path")
    parser.add_argument("--match-runner", help="override othello_match_runner path")
    parser.add_argument("--match-summary", help="override match_summary.py path")
    parser.add_argument("--openings", help="opening suite for match smoke")
    parser.add_argument("--depths", default=DEFAULT_DEPTHS)
    parser.add_argument("--games", type=parse_positive_int, default=DEFAULT_GAMES)
    parser.add_argument("--seed", type=parse_non_negative_int, default=DEFAULT_SEED)
    parser.add_argument(
        "--exact-endgame-threshold",
        type=parse_non_negative_int,
        default=DEFAULT_EXACT_ENDGAME_THRESHOLD,
    )
    parser.add_argument("--dry-run", action="store_true", help="write planned commands only")
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="return zero while recording command failures",
    )
    return parser.parse_args(argv)


def discover_candidate_configs(args: argparse.Namespace) -> tuple[tuple[Path, ...], str]:
    has_configs = bool(args.candidate_configs)
    has_dir = bool(args.candidate_dir)
    if not has_configs and not has_dir:
        raise ScriptError("one of --candidate-configs or --candidate-dir is required")
    if has_configs and has_dir:
        raise ScriptError("cannot combine --candidate-configs and --candidate-dir")

    if has_configs:
        candidates = tuple(parse_csv_paths(args.candidate_configs, error_label="candidate config list"))
        source = f"--candidate-configs {args.candidate_configs}"
    else:
        candidate_dir = Path(args.candidate_dir)
        candidates = tuple(sorted(candidate_dir.glob("*.eval"), key=lambda path: path.name))
        source = f"--candidate-dir {candidate_dir}"
        if not candidates:
            raise ScriptError(f"no .eval candidates found in: {candidate_dir}")

    resolved: set[Path] = set()
    duplicate_paths: list[Path] = []
    for path in candidates:
        resolved_path = path.expanduser().resolve(strict=False)
        if resolved_path in resolved:
            duplicate_paths.append(path)
        resolved.add(resolved_path)
    if duplicate_paths:
        raise ScriptError(
            "duplicate candidate paths: " + ", ".join(str(path) for path in duplicate_paths)
        )

    seen_ids: set[str] = set()
    duplicate_ids: list[str] = []
    for path in candidates:
        candidate_id = path.stem
        if candidate_id in seen_ids:
            duplicate_ids.append(candidate_id)
        seen_ids.add(candidate_id)
    if duplicate_ids:
        raise ScriptError("duplicate candidate output ids: " + ", ".join(duplicate_ids))

    return candidates, source


def config_from_args(
    args: argparse.Namespace,
    invocation: list[str] | None = None,
) -> ValidationConfig:
    if not args.run_search_bench and not args.run_match_smoke:
        raise ScriptError("at least one of --run-search-bench or --run-match-smoke is required")
    parse_depth_list(args.depths)
    candidates, source = discover_candidate_configs(args)
    build_dir = Path(args.build_dir)
    return ValidationConfig(
        base_config=Path(args.base_config),
        candidate_configs=candidates,
        candidate_source=source,
        heldout_summary=Path(args.heldout_summary) if args.heldout_summary else None,
        build_dir=build_dir,
        out_dir=Path(args.out) if args.out else default_out_dir(),
        top=args.top,
        run_search_bench=args.run_search_bench,
        run_match_smoke=args.run_match_smoke,
        search_bench=Path(args.search_bench)
        if args.search_bench
        else build_dir / "othello_search_bench",
        match_runner=Path(args.match_runner)
        if args.match_runner
        else build_dir / "othello_match_runner",
        match_summary=Path(args.match_summary)
        if args.match_summary
        else Path("tools") / "scripts" / "match_summary.py",
        openings=Path(args.openings)
        if args.openings
        else Path("data") / "openings" / "smoke_openings.txt",
        depths=args.depths,
        games=args.games,
        seed=args.seed,
        exact_endgame_threshold=args.exact_endgame_threshold,
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        invocation=invocation or [],
    )


def collect_metadata(config: ValidationConfig) -> Metadata:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    git_sha = completed.stdout.strip() if completed.returncode == 0 else "unknown"
    return Metadata(
        timestamp=dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        git_sha=git_sha or "unknown",
        base_config_sha256=sha256_file(config.base_config),
    )


def parse_heldout_summary(path: Path) -> HeldoutSummary:
    try:
        with path.open("r", encoding="utf-8", newline="") as input_file:
            rows = list(csv.DictReader(input_file, delimiter="\t"))
    except OSError as exc:
        raise ScriptError(f"failed to read held-out summary: {path}: {exc}") from exc

    parsed_rows: list[HeldoutRow] = []
    for index, row in enumerate(rows, start=2):
        missing = {"rank", "candidate", "status", "objective", "delta_vs_base", "config"} - set(row)
        if missing:
            raise ScriptError(
                f"{path}: missing required held-out summary columns on line {index}: "
                + ", ".join(sorted(missing))
            )
        parsed_rows.append(
            HeldoutRow(
                rank=row["rank"],
                candidate=row["candidate"],
                status=row["status"],
                objective=row["objective"],
                delta_vs_base=row["delta_vs_base"],
                config=row["config"],
            )
        )

    by_candidate: dict[str, list[HeldoutRow]] = {}
    by_config_path: dict[Path, list[HeldoutRow]] = {}
    by_config_stem: dict[str, list[HeldoutRow]] = {}
    missing_config_rows: list[HeldoutRow] = []
    for row in parsed_rows:
        by_candidate.setdefault(row.candidate, []).append(row)
        if row.config:
            path_key = Path(row.config).expanduser().resolve(strict=False)
            by_config_path.setdefault(path_key, []).append(row)
            by_config_stem.setdefault(Path(row.config).stem, []).append(row)
        elif row.candidate != "base":
            missing_config_rows.append(row)

    return HeldoutSummary(
        rows=tuple(parsed_rows),
        by_candidate={key: tuple(value) for key, value in by_candidate.items()},
        by_config_path={key: tuple(value) for key, value in by_config_path.items()},
        by_config_stem={key: tuple(value) for key, value in by_config_stem.items()},
        missing_config_rows=tuple(missing_config_rows),
    )


def heldout_match_for(path: Path, heldout: HeldoutSummary) -> tuple[HeldoutRow | None, str]:
    candidate_id = path.stem
    matches = [
        *heldout.by_config_path.get(path.expanduser().resolve(strict=False), ()),
        *heldout.by_candidate.get(candidate_id, ()),
        *heldout.by_config_stem.get(candidate_id, ()),
    ]
    unique_matches = {id(row): row for row in matches}
    if len(unique_matches) == 1:
        return next(iter(unique_matches.values())), ""
    if len(unique_matches) > 1:
        return None, "ambiguous held-out summary match"
    return None, "unmatched held-out summary"


def candidate_sort_key(candidate: Candidate) -> tuple[int, int, int, str]:
    row = candidate.heldout_row
    if row is None:
        return (1, 10**18, 0, candidate.config_path.name)
    rank = row.numeric_rank
    if rank is not None:
        return (0, rank, 0, candidate.config_path.name)
    objective = row.numeric_objective
    return (0, 10**18, -(objective if objective is not None else -10**18), candidate.config_path.name)


def select_candidates(
    config: ValidationConfig,
    heldout: HeldoutSummary | None,
) -> tuple[list[Candidate], list[Candidate]]:
    candidates: list[Candidate] = []
    for path in config.candidate_configs:
        row: HeldoutRow | None = None
        note = "held-out ranking unavailable"
        if heldout is not None:
            row, note = heldout_match_for(path, heldout)
        candidates.append(
            Candidate(
                candidate_id=path.stem,
                config_path=path,
                slug=slugify(path.stem, fallback="candidate"),
                heldout_row=row,
                heldout_note=note,
            )
        )

    ordered = sorted(candidates, key=candidate_sort_key) if heldout is not None else candidates
    selected_ids = {candidate.candidate_id for candidate in ordered[: config.top]}
    all_candidates = [
        Candidate(
            candidate_id=candidate.candidate_id,
            config_path=candidate.config_path,
            slug=candidate.slug,
            heldout_row=candidate.heldout_row,
            heldout_note=candidate.heldout_note,
            selected=candidate.candidate_id in selected_ids,
        )
        for candidate in ordered
    ]
    selected = [candidate for candidate in all_candidates if candidate.selected]
    return all_candidates, selected


def search_command(
    config: ValidationConfig,
    *,
    eval_config: Path,
) -> list[str]:
    return [
        str(config.search_bench),
        "--mode",
        "both",
        "--depths",
        config.depths,
        "--positions",
        DEFAULT_POSITIONS,
        "--repetitions",
        "1",
        "--eval-config",
        str(eval_config),
        "--exact-endgame-threshold",
        str(config.exact_endgame_threshold),
        "--format",
        "jsonl",
    ]


def match_command(
    config: ValidationConfig,
    *,
    candidate: Candidate,
    jsonl_path: Path,
) -> list[str]:
    match_depth = parse_depth_list(config.depths)[0]
    return [
        str(config.match_runner),
        "--black",
        f"search:depth={match_depth},tt=on,pvs=on,exact=off,eval_config={candidate.config_path}",
        "--white",
        f"search:depth={match_depth},tt=on,pvs=on,exact=off,eval_config={config.base_config}",
        "--games",
        str(config.games),
        "--swap-sides",
        "true",
        "--seed",
        str(config.seed),
        "--openings",
        str(config.openings),
        "--output",
        str(jsonl_path),
        "--quiet",
    ]


def summary_command(config: ValidationConfig, *, jsonl_path: Path) -> list[str]:
    return [
        sys.executable,
        str(config.match_summary),
        "--input",
        str(jsonl_path),
        "--by-opening",
    ]


def write_command_log(
    path: Path,
    *,
    command: list[str],
    exit_code: int | None,
    stdout: str,
    stderr: str,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"command: {quote_command(command)}\n"
        f"exit_code: {exit_code if exit_code is not None else 'n/a'}\n\n"
        f"stdout:\n{stdout}\n"
        f"stderr:\n{stderr}\n",
        encoding="utf-8",
    )


def parse_search_metrics(stdout: str) -> SearchMetrics | None:
    result_parts: list[str] = []
    work_parts: list[str] = []
    nodes_total = 0
    elapsed_total = 0.0
    rows_seen = 0
    for line_number, line in enumerate(stdout.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ScriptError(f"search bench JSONL line {line_number}: invalid JSON: {exc.msg}") from exc
        if not isinstance(row, dict) or row.get("row") != "aggregate":
            continue
        rows_seen += 1
        label = f"{row.get('mode', 'mode')}:{row.get('depth', 'depth')}"
        result_parts.append(f"{label}={row.get('result_checksum', 'n/a')}")
        work_parts.append(f"{label}={row.get('work_checksum', 'n/a')}")
        nodes = row.get("nodes")
        if isinstance(nodes, (int, float)) and not isinstance(nodes, bool):
            nodes_total += int(nodes)
        elapsed = row.get("elapsed_ms")
        if isinstance(elapsed, (int, float)) and not isinstance(elapsed, bool):
            elapsed_total += float(elapsed)
    if rows_seen == 0:
        return None
    return SearchMetrics(
        result_checksum="; ".join(result_parts),
        work_checksum="; ".join(work_parts),
        nodes=str(nodes_total),
        elapsed_ms=f"{elapsed_total:.3f}",
    )


def run_search(
    *,
    candidate_id: str,
    config_path: Path,
    command: list[str],
    log_path: Path,
    config: ValidationConfig,
) -> SearchResult:
    if config.dry_run:
        write_command_log(log_path, command=command, exit_code=None, stdout="dry run\n", stderr="")
        return SearchResult(
            candidate_id=candidate_id,
            config_path=config_path,
            command=command,
            log_path=log_path,
            status="planned",
        )

    try:
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
    except OSError as exc:
        write_command_log(log_path, command=command, exit_code=None, stdout="", stderr=str(exc))
        return SearchResult(
            candidate_id=candidate_id,
            config_path=config_path,
            command=command,
            log_path=log_path,
            status="failed",
            error=str(exc),
        )

    write_command_log(
        log_path,
        command=command,
        exit_code=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
    )
    if completed.returncode != 0:
        return SearchResult(
            candidate_id=candidate_id,
            config_path=config_path,
            command=command,
            log_path=log_path,
            status="failed",
            exit_code=completed.returncode,
            error=completed.stderr.strip(),
        )

    try:
        metrics = parse_search_metrics(completed.stdout)
    except ScriptError as exc:
        return SearchResult(
            candidate_id=candidate_id,
            config_path=config_path,
            command=command,
            log_path=log_path,
            status="failed",
            exit_code=completed.returncode,
            error=str(exc),
        )

    return SearchResult(
        candidate_id=candidate_id,
        config_path=config_path,
        command=command,
        log_path=log_path,
        status="passed",
        metrics=metrics,
        exit_code=completed.returncode,
        notes="metrics unavailable" if metrics is None else "",
    )


def run_match(
    *,
    candidate: Candidate,
    command: list[str],
    summary_cmd: list[str],
    log_path: Path,
    summary_log_path: Path,
    jsonl_path: Path,
    summary_path: Path,
    config: ValidationConfig,
) -> MatchResult:
    if config.dry_run:
        write_command_log(log_path, command=command, exit_code=None, stdout="dry run\n", stderr="")
        write_command_log(
            summary_log_path,
            command=summary_cmd,
            exit_code=None,
            stdout="dry run\n",
            stderr="",
        )
        return MatchResult(
            candidate_id=candidate.candidate_id,
            config_path=candidate.config_path,
            command=command,
            summary_command=summary_cmd,
            log_path=log_path,
            summary_log_path=summary_log_path,
            jsonl_path=jsonl_path,
            summary_path=summary_path,
            status="planned",
        )

    jsonl_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
    except OSError as exc:
        write_command_log(log_path, command=command, exit_code=None, stdout="", stderr=str(exc))
        return MatchResult(
            candidate_id=candidate.candidate_id,
            config_path=candidate.config_path,
            command=command,
            summary_command=summary_cmd,
            log_path=log_path,
            summary_log_path=summary_log_path,
            jsonl_path=jsonl_path,
            summary_path=summary_path,
            status="failed",
            error=str(exc),
        )

    write_command_log(
        log_path,
        command=command,
        exit_code=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
    )
    if completed.returncode != 0:
        return MatchResult(
            candidate_id=candidate.candidate_id,
            config_path=candidate.config_path,
            command=command,
            summary_command=summary_cmd,
            log_path=log_path,
            summary_log_path=summary_log_path,
            jsonl_path=jsonl_path,
            summary_path=summary_path,
            status="failed",
            exit_code=completed.returncode,
            error=completed.stderr.strip(),
        )

    try:
        summary_completed = subprocess.run(
            summary_cmd,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        write_command_log(
            summary_log_path,
            command=summary_cmd,
            exit_code=None,
            stdout="",
            stderr=str(exc),
        )
        return MatchResult(
            candidate_id=candidate.candidate_id,
            config_path=candidate.config_path,
            command=command,
            summary_command=summary_cmd,
            log_path=log_path,
            summary_log_path=summary_log_path,
            jsonl_path=jsonl_path,
            summary_path=summary_path,
            status="failed",
            exit_code=completed.returncode,
            error=str(exc),
        )

    write_command_log(
        summary_log_path,
        command=summary_cmd,
        exit_code=summary_completed.returncode,
        stdout=summary_completed.stdout,
        stderr=summary_completed.stderr,
    )
    if summary_completed.returncode != 0:
        return MatchResult(
            candidate_id=candidate.candidate_id,
            config_path=candidate.config_path,
            command=command,
            summary_command=summary_cmd,
            log_path=log_path,
            summary_log_path=summary_log_path,
            jsonl_path=jsonl_path,
            summary_path=summary_path,
            status="failed",
            exit_code=completed.returncode,
            summary_exit_code=summary_completed.returncode,
            error=summary_completed.stderr.strip(),
        )

    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(summary_completed.stdout, encoding="utf-8")
    return MatchResult(
        candidate_id=candidate.candidate_id,
        config_path=candidate.config_path,
        command=command,
        summary_command=summary_cmd,
        log_path=log_path,
        summary_log_path=summary_log_path,
        jsonl_path=jsonl_path,
        summary_path=summary_path,
        status="passed",
        summary_text=summary_completed.stdout,
        exit_code=completed.returncode,
        summary_exit_code=summary_completed.returncode,
    )


def with_search_notes(
    base_result: SearchResult,
    candidate_results: list[SearchResult],
) -> tuple[SearchResult, list[SearchResult]]:
    if base_result.status != "passed" or base_result.metrics is None:
        return base_result, candidate_results

    annotated: list[SearchResult] = []
    for result in candidate_results:
        if result.status != "passed" or result.metrics is None:
            annotated.append(result)
            continue

        notes: list[str] = []
        if result.metrics.result_checksum != base_result.metrics.result_checksum:
            notes.append("result checksum differs from base; semantic-change evidence")
        else:
            notes.append("result checksum matches base")
        if result.metrics.work_checksum != base_result.metrics.work_checksum:
            notes.append("work checksum differs from base")
        else:
            notes.append("work checksum matches base")
        annotated.append(replace(result, notes="; ".join(notes)))

    return replace(base_result, notes="base search reference"), annotated


def md_cell(value: object) -> str:
    text = str(value)
    return text.replace("|", "\\|")


def heldout_objective(candidate: Candidate) -> str:
    return candidate.heldout_row.objective if candidate.heldout_row is not None else "n/a"


def heldout_delta(candidate: Candidate) -> str:
    return candidate.heldout_row.delta_vs_base if candidate.heldout_row is not None else "n/a"


def search_cells(result: SearchResult | None) -> tuple[str, str, str, str, str, str]:
    if result is None:
        return ("not_run", "n/a", "n/a", "n/a", "n/a", "n/a")
    metrics = result.metrics
    return (
        result.status,
        metrics.result_checksum if metrics is not None else "n/a",
        metrics.work_checksum if metrics is not None else "n/a",
        metrics.nodes if metrics is not None else "n/a",
        metrics.elapsed_ms if metrics is not None else "n/a",
        result.notes or result.error or "",
    )


def match_summary_line(result: MatchResult | None) -> str:
    if result is None:
        return "not_run"
    if result.summary_text:
        values: dict[str, str] = {}
        for line in result.summary_text.splitlines():
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            values[key.strip()] = value.strip()
        if "games" in values and "A wins" in values and "B wins" in values:
            return (
                f"games={values['games']}; A wins={values['A wins']}; "
                f"B wins={values['B wins']}; draws={values.get('draws', 'n/a')}"
            )
        for line in result.summary_text.splitlines():
            if line.strip():
                return line.strip()
    return result.notes or result.error or result.status


def render_candidate_selection(
    *,
    all_candidates: list[Candidate],
    selected: list[Candidate],
    config: ValidationConfig,
    heldout: HeldoutSummary | None,
) -> list[str]:
    lines = [
        "## Candidate Selection",
        "",
        f"- candidates_discovered: `{len(all_candidates)}`",
        f"- candidates_selected: `{len(selected)}`",
        f"- heldout_ranking: `{'available' if heldout is not None else 'unavailable'}`",
        "",
        "| Selected | Candidate | Held-Out Objective | Held-Out Delta vs Base | Note | Config |",
        "|---|---|---:|---:|---|---|",
    ]
    for candidate in all_candidates:
        lines.append(
            f"| {'yes' if candidate.selected else 'no'} | `{candidate.candidate_id}` | "
            f"{heldout_objective(candidate)} | {heldout_delta(candidate)} | "
            f"{md_cell(candidate.heldout_note)} | `{candidate.config_path}` |"
        )
    if heldout is not None and heldout.missing_config_rows:
        lines.extend(["", "Held-out summary rows without config paths:"])
        for row in heldout.missing_config_rows:
            lines.append(f"- candidate `{row.candidate}` objective `{row.objective}`")
    if len(all_candidates) > config.top:
        lines.append("")
        lines.append(f"Selected top `{config.top}` candidates for requested validation modes.")
    lines.append("")
    return lines


def render_search_results(
    *,
    config: ValidationConfig,
    base_search: SearchResult | None,
    candidate_searches: list[SearchResult],
) -> list[str]:
    lines = ["## Search Bench Results", ""]
    if not config.run_search_bench:
        lines.extend(["Search bench was not requested.", ""])
        return lines

    lines.extend(
        [
            "| Candidate | Status | Result Checksum | Work Checksum | Nodes | Elapsed ms | Notes | Log |",
            "|---|---|---|---|---:|---:|---|---|",
        ]
    )
    for result in [base_search, *candidate_searches]:
        if result is None:
            continue
        status, result_checksum, work_checksum, nodes, elapsed_ms, notes = search_cells(result)
        lines.append(
            f"| `{result.candidate_id}` | {status} | {md_cell(result_checksum)} | "
            f"{md_cell(work_checksum)} | {nodes} | {elapsed_ms} | {md_cell(notes)} | "
            f"`{result.log_path}` |"
        )
    lines.append("")
    return lines


def render_match_results(
    *,
    config: ValidationConfig,
    match_results: list[MatchResult],
) -> list[str]:
    lines = ["## Match Smoke Results", ""]
    if not config.run_match_smoke:
        lines.extend(["Match smoke was not requested.", ""])
        return lines

    lines.extend(
        [
            "| Candidate | Status | Summary | Notes | JSONL | Log |",
            "|---|---|---|---|---|---|",
        ]
    )
    for result in match_results:
        lines.append(
            f"| `{result.candidate_id}` | {result.status} | {md_cell(match_summary_line(result))} | "
            f"{md_cell(result.notes or result.error)} | `{result.jsonl_path}` | `{result.log_path}` |"
        )
    if not match_results:
        lines.append("| - | not_run | n/a | n/a | n/a | n/a |")
    lines.append("")
    return lines


def render_comparison_notes(
    *,
    base_search: SearchResult | None,
    candidate_searches: list[SearchResult],
    match_results: list[MatchResult],
) -> list[str]:
    lines = ["## Comparison Notes", ""]
    notes: list[str] = []
    if base_search is not None and base_search.status == "failed":
        notes.append(f"Base search bench failed; inspect `{base_search.log_path}`.")
    for result in candidate_searches:
        if result.status == "failed":
            notes.append(f"`{result.candidate_id}` search bench failed; inspect `{result.log_path}`.")
        elif "semantic-change evidence" in result.notes:
            notes.append(f"`{result.candidate_id}` changed search result checksums vs base.")
    for result in match_results:
        if result.status == "failed":
            notes.append(f"`{result.candidate_id}` match smoke failed; inspect `{result.log_path}`.")
    if not notes:
        notes.append("No failed checks or parsed checksum differences were recorded.")
    notes.append("Candidates still need broader validation before any promotion claim.")
    lines.extend(f"- {note}" for note in notes)
    lines.append("")
    return lines


def render_commands(
    *,
    base_search: SearchResult | None,
    candidate_searches: list[SearchResult],
    match_results: list[MatchResult],
) -> list[str]:
    lines = ["## Commands", ""]
    for result in [base_search, *candidate_searches]:
        if result is None:
            continue
        lines.extend(
            [
                f"### search: {result.candidate_id}",
                "",
                f"- status: `{result.status}`",
                f"- log: `{result.log_path}`",
            ]
        )
        if result.exit_code is not None:
            lines.append(f"- exit_code: `{result.exit_code}`")
        lines.extend(["", "```sh", quote_command(result.command), "```", ""])

    for result in match_results:
        lines.extend(
            [
                f"### match: {result.candidate_id}",
                "",
                f"- status: `{result.status}`",
                f"- log: `{result.log_path}`",
                f"- jsonl: `{result.jsonl_path}`",
                "",
                "```sh",
                quote_command(result.command),
                "```",
                "",
                "```sh",
                quote_command(result.summary_command),
                "```",
                "",
            ]
        )
    return lines


def render_failures(
    *,
    base_search: SearchResult | None,
    candidate_searches: list[SearchResult],
    match_results: list[MatchResult],
) -> list[str]:
    failures: list[str] = []
    for result in [base_search, *candidate_searches]:
        if result is not None and result.status == "failed":
            failures.append(
                f"- `{result.candidate_id}` search failed; log `{result.log_path}`; "
                f"error: {result.error or 'see log'}"
            )
    for result in match_results:
        if result.status == "failed":
            failures.append(
                f"- `{result.candidate_id}` match failed; log `{result.log_path}`; "
                f"error: {result.error or 'see log'}"
            )
    if not failures:
        return []
    return ["## Failures", "", *failures, ""]


def render_report(
    *,
    config: ValidationConfig,
    metadata: Metadata,
    all_candidates: list[Candidate],
    selected: list[Candidate],
    heldout: HeldoutSummary | None,
    base_search: SearchResult | None,
    candidate_searches: list[SearchResult],
    match_results: list[MatchResult],
) -> str:
    lines = [
        "# Eval Config Search/Match Validation Report",
        "",
        "No strength claim. This is search/match smoke validation for experimental `.eval` configs.",
        "",
        "## Metadata",
        "",
        f"- timestamp: `{metadata.timestamp}`",
        f"- git_sha: `{metadata.git_sha}`",
        f"- command: `{quote_command(config.invocation) if config.invocation else 'unknown'}`",
        f"- base_config_path: `{config.base_config}`",
        f"- base_config_sha256: `{metadata.base_config_sha256}`",
        f"- candidate_source: `{config.candidate_source}`",
        f"- heldout_summary_path: `{config.heldout_summary if config.heldout_summary else 'n/a'}`",
        f"- build_dir: `{config.build_dir}`",
        f"- search_bench_path: `{config.search_bench}`",
        f"- match_runner_path: `{config.match_runner}`",
        f"- match_summary_path: `{config.match_summary}`",
        f"- depths: `{config.depths}`",
        f"- games: `{config.games}`",
        f"- seed: `{config.seed}`",
        f"- exact_endgame_threshold: `{config.exact_endgame_threshold}`",
        "",
        *render_candidate_selection(
            all_candidates=all_candidates,
            selected=selected,
            config=config,
            heldout=heldout,
        ),
        *render_search_results(
            config=config,
            base_search=base_search,
            candidate_searches=candidate_searches,
        ),
        *render_match_results(config=config, match_results=match_results),
        *render_comparison_notes(
            base_search=base_search,
            candidate_searches=candidate_searches,
            match_results=match_results,
        ),
        *render_commands(
            base_search=base_search,
            candidate_searches=candidate_searches,
            match_results=match_results,
        ),
        *render_failures(
            base_search=base_search,
            candidate_searches=candidate_searches,
            match_results=match_results,
        ),
        "## Caveats",
        "",
        "- Search bench evidence is not Elo or a strength claim.",
        "- Match smoke game counts are too small for strength claims.",
        "- `--exact-endgame-threshold 0` is the default for midgame/eval search evidence unless overridden.",
        "- Candidate configs are local experiment artifacts under `runs/`.",
        "- This report is not a default-promotion recommendation.",
        "- Follow-up validation should use broader search bench, match runner or base/head comparison, and external sanity when appropriate.",
    ]
    return "\n".join(lines) + "\n"


def write_summary_tsv(
    path: Path,
    *,
    base_config: Path,
    all_candidates: list[Candidate],
    selected: list[Candidate],
    base_search: SearchResult | None,
    candidate_searches: list[SearchResult],
    match_results: list[MatchResult],
) -> None:
    search_by_id = {result.candidate_id: result for result in candidate_searches}
    match_by_id = {result.candidate_id: result for result in match_results}
    selected_ids = {candidate.candidate_id for candidate in selected}
    rows = [
        "candidate\tselected\theldout_objective\theldout_delta\tsearch_status\tmatch_status\tsearch_log\tmatch_jsonl\tmatch_summary\tconfig"
    ]
    rows.append(
        "\t".join(
            [
                "base",
                "yes",
                "n/a",
                "n/a",
                base_search.status if base_search is not None else "disabled",
                "n/a",
                str(base_search.log_path) if base_search is not None else "",
                "",
                "",
                str(base_search.config_path) if base_search is not None else str(base_config),
            ]
        )
    )

    for candidate in all_candidates:
        search = search_by_id.get(candidate.candidate_id)
        match = match_by_id.get(candidate.candidate_id)
        selected_text = "yes" if candidate.candidate_id in selected_ids else "no"
        search_status = search.status if search is not None else ("disabled" if selected_text == "yes" else "not_run")
        match_status = match.status if match is not None else ("disabled" if selected_text == "yes" else "not_run")
        rows.append(
            "\t".join(
                [
                    candidate.candidate_id,
                    selected_text,
                    heldout_objective(candidate),
                    heldout_delta(candidate),
                    search_status,
                    match_status,
                    str(search.log_path) if search is not None else "",
                    str(match.jsonl_path) if match is not None else "",
                    str(match.summary_path) if match is not None else "",
                    str(candidate.config_path),
                ]
            )
        )
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def run_validation(config: ValidationConfig) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    (config.out_dir / "logs").mkdir(parents=True, exist_ok=True)
    (config.out_dir / "reports").mkdir(parents=True, exist_ok=True)
    (config.out_dir / "matches").mkdir(parents=True, exist_ok=True)

    metadata = collect_metadata(config)
    heldout = parse_heldout_summary(config.heldout_summary) if config.heldout_summary else None
    all_candidates, selected = select_candidates(config, heldout)

    base_search: SearchResult | None = None
    candidate_searches: list[SearchResult] = []
    if config.run_search_bench:
        base_command = search_command(config, eval_config=config.base_config)
        base_search = run_search(
            candidate_id="base",
            config_path=config.base_config,
            command=base_command,
            log_path=config.out_dir / "logs" / "search-base.log",
            config=config,
        )

        for candidate in selected:
            command = search_command(config, eval_config=candidate.config_path)
            candidate_searches.append(
                run_search(
                    candidate_id=candidate.candidate_id,
                    config_path=candidate.config_path,
                    command=command,
                    log_path=config.out_dir / "logs" / f"search-{candidate.slug}.log",
                    config=config,
                )
            )
        if base_search is not None:
            base_search, candidate_searches = with_search_notes(base_search, candidate_searches)

    match_results: list[MatchResult] = []
    if config.run_match_smoke:
        for candidate in selected:
            jsonl_path = config.out_dir / "matches" / f"{candidate.slug}-vs-base.jsonl"
            summary_path = config.out_dir / "reports" / f"{candidate.slug}-match-summary.txt"
            command = match_command(config, candidate=candidate, jsonl_path=jsonl_path)
            summary_cmd = summary_command(config, jsonl_path=jsonl_path)
            match_results.append(
                run_match(
                    candidate=candidate,
                    command=command,
                    summary_cmd=summary_cmd,
                    log_path=config.out_dir / "logs" / f"match-{candidate.slug}.log",
                    summary_log_path=config.out_dir
                    / "logs"
                    / f"match-summary-{candidate.slug}.log",
                    jsonl_path=jsonl_path,
                    summary_path=summary_path,
                    config=config,
                )
            )

    report = render_report(
        config=config,
        metadata=metadata,
        all_candidates=all_candidates,
        selected=selected,
        heldout=heldout,
        base_search=base_search,
        candidate_searches=candidate_searches,
        match_results=match_results,
    )
    (config.out_dir / "search_match_validation_report.md").write_text(report, encoding="utf-8")
    write_summary_tsv(
        config.out_dir / "summary.tsv",
        base_config=config.base_config,
        all_candidates=all_candidates,
        selected=selected,
        base_search=base_search,
        candidate_searches=candidate_searches,
        match_results=match_results,
    )

    checked_results: list[SearchResult | MatchResult] = [
        *([base_search] if base_search is not None else []),
        *candidate_searches,
        *match_results,
    ]
    failed = any(result.status == "failed" for result in checked_results)
    if failed and not config.allow_failures:
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [
            Path(sys.argv[0]).name,
            *(argv if argv is not None else sys.argv[1:]),
        ]
        config = config_from_args(args, invocation=invocation)
        return run_validation(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
