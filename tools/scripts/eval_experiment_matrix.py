#!/usr/bin/env python3
"""Run a small local evaluator-preset experiment matrix."""

from __future__ import annotations

import argparse
import datetime as dt
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import match_summary
from common import ScriptError, quote_command


@dataclass(frozen=True)
class GitMetadata:
    ref: str
    sha: str


@dataclass(frozen=True)
class EvalExperimentConfig:
    presets: list[str]
    depths: list[int]
    games: int
    openings: Path
    seed: int
    build_dir: Path
    out_dir: Path
    search_bench: Path
    match_runner: Path
    exact_endgame_threshold: int
    positions: str = "smoke"
    by_position: bool = False
    allow_errors: bool = False


@dataclass(frozen=True)
class SearchBenchRun:
    preset: str
    command: list[str]
    exit_code: int = 0
    output: str = ""


@dataclass(frozen=True)
class MatchRun:
    preset: str
    depth: int
    output_path: Path
    command: list[str]
    exit_code: int = 0
    summary: match_summary.Summary | None = None
    summary_text: str = ""
    error: str | None = None


def parse_csv_names(value: str) -> list[str]:
    names = [part.strip() for part in value.split(",")]
    if not names or any(not name for name in names):
        raise ScriptError(f"invalid preset list: {value}")
    return names


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


def slugify(value: str) -> str:
    slug = "".join(char if char.isalnum() or char in ("-", "_") else "-" for char in value)
    return slug.strip("-") or "preset"


def detect_git_metadata(repo: Path) -> GitMetadata:
    def git_output(args: list[str]) -> str:
        completed = subprocess.run(
            ["git", "-C", str(repo), *args],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        if completed.returncode != 0:
            return "unknown"
        text = completed.stdout.strip()
        return text if text else "unknown"

    return GitMetadata(
        ref=git_output(["rev-parse", "--abbrev-ref", "HEAD"]),
        sha=git_output(["rev-parse", "HEAD"]),
    )


def detect_build_type(build_dir: Path) -> str:
    cache = build_dir / "CMakeCache.txt"
    try:
        for line in cache.read_text(encoding="utf-8").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                return line.split("=", 1)[1] or "unknown"
    except OSError:
        return "unknown"
    return "unknown"


def build_search_bench_command(config: EvalExperimentConfig, preset: str) -> list[str]:
    command = [
        str(config.search_bench),
        "--mode",
        "iterative",
        "--depths",
        ",".join(str(depth) for depth in config.depths),
        "--positions",
        config.positions,
        "--repetitions",
        "1",
        "--tt",
        "on",
        "--pvs",
        "on",
        "--aspiration",
        "on",
        "--eval-preset",
        preset,
        "--exact-endgame-threshold",
        str(config.exact_endgame_threshold),
    ]
    if config.by_position:
        command.append("--by-position")
    return command


def build_match_command(config: EvalExperimentConfig, preset: str, depth: int) -> MatchRun:
    output_path = config.out_dir / "matches" / f"{slugify(preset)}-depth-{depth}.jsonl"
    command = [
        str(config.match_runner),
        "--black",
        f"search:depth={depth},tt=on,pvs=on,exact=off,eval={preset}",
        "--white",
        f"search:depth={depth},tt=on,pvs=on,exact=off,eval=default",
        "--games",
        str(config.games),
        "--swap-sides",
        "true",
        "--seed",
        str(config.seed),
        "--openings",
        str(config.openings),
        "--output",
        str(output_path),
        "--quiet",
    ]
    return MatchRun(preset=preset, depth=depth, output_path=output_path, command=command)


def run_capture(command: list[str]) -> tuple[int, str]:
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return int(completed.returncode), completed.stdout


def summarize_match(run: MatchRun) -> MatchRun:
    records = match_summary.load_records(run.output_path)
    summary = match_summary.summarize(records)
    summary_text = match_summary.format_summary(run.output_path, summary, by_opening=True)
    summary_path = run.output_path.with_suffix(".summary.txt")
    summary_path.write_text(summary_text, encoding="utf-8")
    return MatchRun(
        preset=run.preset,
        depth=run.depth,
        output_path=run.output_path,
        command=run.command,
        exit_code=run.exit_code,
        summary=summary,
        summary_text=summary_text,
    )


def matrix_has_failures(
    search_runs: list[SearchBenchRun],
    match_runs: list[MatchRun],
    *,
    allow_errors: bool,
) -> bool:
    if any(run.exit_code != 0 for run in search_runs):
        return True
    for run in match_runs:
        if run.exit_code != 0 or run.summary is None:
            return True
        if run.summary.error_games > 0 and not allow_errors:
            return True
    return False


def render_report(
    config: EvalExperimentConfig,
    search_runs: list[SearchBenchRun],
    match_runs: list[MatchRun],
    *,
    dry_run: bool,
) -> str:
    git = detect_git_metadata(Path.cwd())
    timestamp = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    failed = [run for run in search_runs if run.exit_code != 0]
    failed.extend(run for run in match_runs if run.exit_code != 0 or run.summary is None)
    error_game_runs = [
        run
        for run in match_runs
        if run.summary is not None and run.summary.error_games > 0
    ]

    lines = [
        "# Evaluation Experiment Matrix",
        "",
        "Status: dry run." if dry_run else "Status: local evaluator preset experiment.",
        "",
        "This report checks experiment wiring only. It is not a strength claim.",
        "",
        "## Metadata",
        "",
        f"- Generated at: `{timestamp}`",
        f"- Git ref: `{git.ref}`",
        f"- Git SHA: `{git.sha}`",
        f"- Build dir: `{config.build_dir}`",
        f"- Build type: `{detect_build_type(config.build_dir)}`",
        f"- Presets: `{','.join(config.presets)}`",
        f"- Depths: `{','.join(str(depth) for depth in config.depths)}`",
        f"- Games: `{config.games}`",
        f"- Seed: `{config.seed}`",
        f"- Openings: `{config.openings}`",
        f"- Exact endgame threshold: `{config.exact_endgame_threshold}`",
        f"- Search positions: `{config.positions}`",
        f"- Search by-position: `{'on' if config.by_position else 'off'}`",
        f"- Allow error games: `{'true' if config.allow_errors else 'false'}`",
        "",
        "## Commands",
        "",
    ]

    for run in search_runs:
        lines.extend(("```sh", quote_command(run.command), "```", ""))
    for run in match_runs:
        lines.extend(("```sh", quote_command(run.command), "```", ""))

    lines.extend(("## Search Bench Summary", ""))
    for run in search_runs:
        lines.append(f"### {run.preset}")
        lines.append("")
        if dry_run:
            lines.append("Not executed in dry-run mode.")
        elif run.exit_code != 0:
            lines.append(f"Command failed with exit code {run.exit_code}.")
        else:
            lines.extend(("```text", run.output.rstrip(), "```"))
        lines.append("")

    lines.extend(("## Match Summary", ""))
    if not match_runs:
        lines.extend(("No candidate-vs-default match was scheduled.", ""))
    for run in match_runs:
        lines.append(f"### {run.preset} depth {run.depth}")
        lines.append("")
        lines.append(f"- JSONL: `{run.output_path}`")
        if dry_run:
            lines.append("- Not executed in dry-run mode.")
        elif run.summary is None:
            lines.append(f"- Error: {run.error or 'summary unavailable'}")
        else:
            lines.append(f"- Error games: `{run.summary.error_games}`")
            if run.summary.error_games > 0 and not config.allow_errors:
                lines.append("- Failure: match summary contains error games.")
            lines.extend(("", "```text", run.summary_text.rstrip(), "```"))
        lines.append("")

    lines.extend(
        (
            "## Notes",
            "",
            "- Positive: evaluator presets can be selected without changing the default path.",
            "- Negative: small smoke runs only prove wiring and reproducibility plumbing.",
        )
    )
    if failed and not dry_run:
        lines.append(f"- Failures: {len(failed)} command(s) need inspection.")
    if error_game_runs:
        total_error_games = sum(run.summary.error_games for run in error_game_runs if run.summary)
        if config.allow_errors:
            lines.append(f"- Error games allowed: {total_error_games} error game(s) were reported.")
        else:
            lines.append(f"- Failure: {total_error_games} error game(s) were reported.")
    return "\n".join(lines) + "\n"


def run_matrix(config: EvalExperimentConfig, *, dry_run: bool) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    (config.out_dir / "matches").mkdir(parents=True, exist_ok=True)

    search_runs: list[SearchBenchRun] = []
    for preset in config.presets:
        command = build_search_bench_command(config, preset)
        print(f"search bench {preset}: {quote_command(command)}", flush=True)
        if dry_run:
            search_runs.append(SearchBenchRun(preset=preset, command=command))
            continue
        exit_code, output = run_capture(command)
        search_runs.append(
            SearchBenchRun(preset=preset, command=command, exit_code=exit_code, output=output)
        )

    match_runs: list[MatchRun] = []
    for preset in config.presets:
        if preset == "default":
            continue
        for depth in config.depths:
            run = build_match_command(config, preset, depth)
            print(f"match {preset} depth {depth}: {quote_command(run.command)}", flush=True)
            if dry_run:
                match_runs.append(run)
                continue
            run.output_path.parent.mkdir(parents=True, exist_ok=True)
            exit_code, output = run_capture(run.command)
            if exit_code != 0:
                match_runs.append(
                    MatchRun(
                        preset=run.preset,
                        depth=run.depth,
                        output_path=run.output_path,
                        command=run.command,
                        exit_code=exit_code,
                        error=output.strip() or f"runner exited with {exit_code}",
                    )
                )
                continue
            try:
                match_runs.append(summarize_match(run))
            except ScriptError as exc:
                match_runs.append(
                    MatchRun(
                        preset=run.preset,
                        depth=run.depth,
                        output_path=run.output_path,
                        command=run.command,
                        exit_code=0,
                        error=str(exc),
                    )
                )

    report_path = config.out_dir / "report.md"
    report_path.write_text(render_report(config, search_runs, match_runs, dry_run=dry_run),
                           encoding="utf-8")
    print(f"report: {report_path}", flush=True)

    if dry_run:
        return 0
    if matrix_has_failures(search_runs, match_runs, allow_errors=config.allow_errors):
        return 1
    return 0


def _positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected integer, got: {value}") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError(f"expected positive integer, got: {value}")
    return parsed


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run evaluator preset search and match smoke experiments."
    )
    parser.add_argument("--presets", required=True, help="comma-separated evaluator preset names")
    parser.add_argument("--depths", required=True, help="comma-separated positive depths")
    parser.add_argument("--games", required=True, type=_positive_int, help="games per match")
    parser.add_argument("--openings", required=True, help="opening suite text file")
    parser.add_argument("--seed", required=True, type=int, help="base random seed")
    parser.add_argument("--build-dir", required=True, help="build directory")
    parser.add_argument("--out", required=True, help="output directory, normally under runs/")
    parser.add_argument(
        "--exact-endgame-threshold",
        default=0,
        type=int,
        help="search bench exact endgame threshold; default disables exact root solving",
    )
    parser.add_argument(
        "--positions",
        default="smoke",
        choices=("smoke", "suite"),
        help="search bench position set (default: smoke)",
    )
    parser.add_argument(
        "--by-position",
        action="store_true",
        help="include per-position search bench rows and summaries",
    )
    parser.add_argument(
        "--allow-errors",
        action="store_true",
        help="return success even if match summaries contain error games",
    )
    parser.add_argument(
        "--search-bench",
        help="path to othello_search_bench; defaults to build dir",
    )
    parser.add_argument(
        "--match-runner",
        help="path to othello_match_runner; defaults to build dir",
    )
    parser.add_argument(
        "--smoke-run",
        action="store_true",
        help="limit to the first two presets, first depth, and at most six games",
    )
    parser.add_argument("--dry-run", action="store_true", help="write commands/report only")
    return parser.parse_args(argv)


def config_from_args(args: argparse.Namespace) -> EvalExperimentConfig:
    build_dir = Path(args.build_dir)
    presets = parse_csv_names(args.presets)
    depths = parse_depth_list(args.depths)
    games = args.games
    if args.smoke_run:
        presets = presets[:2]
        depths = depths[:1]
        games = min(games, 6)
    return EvalExperimentConfig(
        presets=presets,
        depths=depths,
        games=games,
        openings=Path(args.openings),
        seed=args.seed,
        build_dir=build_dir,
        out_dir=Path(args.out),
        search_bench=Path(args.search_bench) if args.search_bench else build_dir / "othello_search_bench",
        match_runner=Path(args.match_runner) if args.match_runner else build_dir / "othello_match_runner",
        exact_endgame_threshold=args.exact_endgame_threshold,
        positions=args.positions,
        by_position=args.by_position,
        allow_errors=args.allow_errors,
    )


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        config = config_from_args(args)
        return run_matrix(config, dry_run=args.dry_run)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
