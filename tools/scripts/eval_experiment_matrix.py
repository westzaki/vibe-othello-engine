#!/usr/bin/env python3
"""Run staged evaluator-preset experiment matrices.

This script orchestrates existing C++ binaries and summarizes their output. It
does not implement Othello rules or make strength claims.
"""

from __future__ import annotations

import argparse
import datetime as dt
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import match_summary
from common import (
    ScriptError,
    parse_bool,
    parse_csv_paths as common_parse_csv_paths,
    parse_csv_values,
    quote_command,
    slugify,
    write_report_section,
)


@dataclass(frozen=True)
class GitMetadata:
    ref: str
    sha: str


@dataclass(frozen=True)
class EvalTarget:
    kind: str
    value: str
    label: str

    @property
    def slug(self) -> str:
        return slugify(self.label, fallback="preset")

    @property
    def search_option(self) -> str:
        return "--eval-config" if self.kind == "config" else "--eval-preset"

    @property
    def player_option(self) -> str:
        return "eval_config" if self.kind == "config" else "eval"


@dataclass(frozen=True)
class EvalExperimentConfig:
    presets: list[str]
    reference_preset: str
    small_depths: list[int]
    extended_depths: list[int]
    small_games: int
    extended_games: int
    promote_top: int
    max_node_ratio: float
    min_avg_diff: float
    require_nonnegative_diff: bool
    openings: Path
    seed: int
    build_dir: Path
    out_dir: Path
    search_bench: Path
    match_runner: Path
    exact_endgame_threshold: int
    configs: list[EvalTarget] = field(default_factory=list)
    reference_config: EvalTarget | None = None
    positions: str = "smoke"
    by_position: bool = False
    allow_errors: bool = False
    run_ntest_sanity: bool = False
    ntest_engine: str | None = None
    engines: Path | None = None
    ntest_depth: int | None = None
    ntest_games: int | None = None
    ntest_openings: Path | None = None

    @property
    def preset_targets(self) -> list[EvalTarget]:
        return [EvalTarget(kind="preset", value=preset, label=preset) for preset in self.presets]

    @property
    def eval_targets(self) -> list[EvalTarget]:
        return [*self.preset_targets, *self.configs]

    @property
    def reference_target(self) -> EvalTarget:
        if self.reference_config is not None:
            return self.reference_config
        return EvalTarget(kind="preset", value=self.reference_preset, label=self.reference_preset)

    @property
    def candidate_targets(self) -> list[EvalTarget]:
        reference = self.reference_target
        return [
            target
            for target in self.eval_targets
            if not (target.kind == reference.kind and target.value == reference.value)
        ]

    @property
    def candidate_presets(self) -> list[str]:
        return [target.label for target in self.candidate_targets]

    @property
    def search_depths(self) -> list[int]:
        depths: list[int] = []
        for depth in [*self.small_depths, *self.extended_depths]:
            if depth not in depths:
                depths.append(depth)
        return depths


@dataclass(frozen=True)
class SearchBenchRun:
    preset: str
    command: list[str]
    output_path: Path | None = None
    exit_code: int = 0
    output: str = ""


@dataclass(frozen=True)
class MatchRun:
    preset: str
    stage: str
    depth: int
    games: int
    reference_preset: str
    output_path: Path
    command: list[str]
    exit_code: int = 0
    summary: match_summary.Summary | None = None
    summary_text: str = ""
    error: str | None = None


@dataclass(frozen=True)
class NTestRun:
    preset: str
    depth: int
    output_path: Path
    command: list[str]
    exit_code: int = 0
    summary: match_summary.Summary | None = None
    summary_text: str = ""
    error: str | None = None


@dataclass(frozen=True)
class CandidateDecision:
    preset: str
    status: str
    promoted_to_extended: bool
    reasons: list[str]
    games: int = 0
    wins: int = 0
    losses: int = 0
    draws: int = 0
    error_games: int = 0
    avg_diff: float = 0.0
    node_ratio: float | None = None
    time_ratio: float | None = None
    depth_avg_diffs: dict[int, float] = field(default_factory=dict)


def parse_csv_names(value: str) -> list[str]:
    return parse_csv_values(value, error_label="preset list")


def parse_csv_paths(value: str) -> list[Path]:
    return common_parse_csv_paths(value, error_label="config list")


def eval_config_label(path: Path) -> str:
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            body = line.split("#", 1)[0].strip()
            if not body or "=" not in body:
                continue
            key, value = (part.strip() for part in body.split("=", 1))
            if key == "name" and value:
                return value
    except OSError:
        pass
    return path.stem or str(path)


def config_target(path: Path) -> EvalTarget:
    return EvalTarget(kind="config", value=str(path), label=eval_config_label(path))


def eval_target(value: EvalTarget | str) -> EvalTarget:
    if isinstance(value, EvalTarget):
        return value
    return EvalTarget(kind="preset", value=value, label=value)


def target_display(target: EvalTarget) -> str:
    return f"{target.kind}:{target.value} ({target.label})"


def same_target(left: EvalTarget, right: EvalTarget) -> bool:
    return left.kind == right.kind and left.value == right.value


def validate_unique_eval_targets(eval_targets: list[EvalTarget], reference: EvalTarget) -> None:
    seen_labels: dict[str, EvalTarget] = {}
    seen_slugs: dict[str, EvalTarget] = {}
    for target in eval_targets:
        previous = seen_labels.get(target.label)
        if previous is not None:
            raise ScriptError(
                "duplicate evaluator label: "
                f"{target.label} ({target_display(previous)} and {target_display(target)})"
            )
        seen_labels[target.label] = target

        previous_slug = seen_slugs.get(target.slug)
        if previous_slug is not None:
            raise ScriptError(
                "duplicate evaluator slug: "
                f"{target.slug} ({target_display(previous_slug)} and {target_display(target)})"
            )
        seen_slugs[target.slug] = target

    for target in eval_targets:
        if same_target(target, reference):
            continue
        if target.label == reference.label:
            raise ScriptError(
                "duplicate evaluator label: "
                f"{reference.label} ({target_display(target)} and {target_display(reference)})"
            )
        if target.slug == reference.slug:
            raise ScriptError(
                "duplicate evaluator slug: "
                f"{reference.slug} ({target_display(target)} and {target_display(reference)})"
            )


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


def build_search_bench_command(config: EvalExperimentConfig, preset: EvalTarget | str) -> list[str]:
    target = eval_target(preset)
    command = [
        str(config.search_bench),
        "--mode",
        "iterative",
        "--depths",
        ",".join(str(depth) for depth in config.search_depths),
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
        target.search_option,
        target.value,
        "--exact-endgame-threshold",
        str(config.exact_endgame_threshold),
    ]
    if config.by_position:
        command.append("--by-position")
    return command


def search_output_path(config: EvalExperimentConfig, preset: EvalTarget | str) -> Path:
    return config.out_dir / "search" / f"{eval_target(preset).slug}.txt"


def build_match_command(
    config: EvalExperimentConfig,
    preset: EvalTarget | str,
    depth: int,
    *,
    stage: str = "small",
    games: int | None = None,
) -> MatchRun:
    target = eval_target(preset)
    reference = config.reference_target
    match_games = config.small_games if games is None else games
    output_path = (
        config.out_dir
        / "matches"
        / stage
        / f"{target.slug}-vs-{reference.slug}-depth-{depth}.jsonl"
    )
    command = [
        str(config.match_runner),
        "--black",
        f"search:depth={depth},tt=on,pvs=on,exact=off,{target.player_option}={target.value}",
        "--white",
        f"search:depth={depth},tt=on,pvs=on,exact=off,{reference.player_option}={reference.value}",
        "--games",
        str(match_games),
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
    return MatchRun(
        preset=target.label,
        stage=stage,
        depth=depth,
        games=match_games,
        reference_preset=reference.label,
        output_path=output_path,
        command=command,
    )


def build_ntest_command(config: EvalExperimentConfig, preset: EvalTarget | str) -> NTestRun:
    target = eval_target(preset)
    if config.ntest_engine is None:
        raise ScriptError("ntest engine is not configured")
    if config.engines is None:
        raise ScriptError("engine config path is not configured")
    depth = config.ntest_depth or (config.extended_depths[-1] if config.extended_depths else config.small_depths[-1])
    games = config.ntest_games or config.small_games
    openings = config.ntest_openings or config.openings
    output_path = (
        config.out_dir
        / "ntest"
        / f"{target.slug}-vs-{slugify(config.ntest_engine, fallback='preset')}-depth-{depth}.jsonl"
    )
    command = [
        str(config.match_runner),
        "--black",
        f"search:depth={depth},tt=on,pvs=on,exact=off,{target.player_option}={target.value}",
        "--white",
        f"external:{config.ntest_engine}",
        "--games",
        str(games),
        "--swap-sides",
        "true",
        "--seed",
        str(config.seed),
        "--openings",
        str(openings),
        "--engines",
        str(config.engines),
        "--output",
        str(output_path),
        "--quiet",
    ]
    return NTestRun(preset=target.label, depth=depth, output_path=output_path, command=command)


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
        stage=run.stage,
        depth=run.depth,
        games=run.games,
        reference_preset=run.reference_preset,
        output_path=run.output_path,
        command=run.command,
        exit_code=run.exit_code,
        summary=summary,
        summary_text=summary_text,
    )


def summarize_ntest(run: NTestRun) -> NTestRun:
    records = match_summary.load_records(run.output_path)
    summary = match_summary.summarize(records)
    summary_text = match_summary.format_summary(run.output_path, summary, by_opening=True)
    summary_path = run.output_path.with_suffix(".summary.txt")
    summary_path.write_text(summary_text, encoding="utf-8")
    return NTestRun(
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


def _optional_ratio(summary: match_summary.Summary, numerator: str, denominator: str) -> float | None:
    numerator_avg = summary.optional_average(numerator)
    denominator_avg = summary.optional_average(denominator)
    if numerator_avg is None or denominator_avg in (None, 0.0):
        return None
    return numerator_avg / denominator_avg


def _aggregate_optional_ratio(
    runs: list[MatchRun], numerator: str, denominator: str
) -> float | None:
    numerator_total = 0.0
    denominator_total = 0.0
    seen = False
    for run in runs:
        summary = run.summary
        if summary is None:
            continue
        numerator_count = summary.optional_counts.get(numerator, 0)
        denominator_count = summary.optional_counts.get(denominator, 0)
        if numerator_count == 0 or denominator_count == 0:
            continue
        numerator_total += summary.optional_totals[numerator]
        denominator_total += summary.optional_totals[denominator]
        seen = True
    if not seen or denominator_total == 0.0:
        return None
    return numerator_total / denominator_total


def decide_candidate(
    preset: str,
    small_runs: list[MatchRun],
    search_runs: list[SearchBenchRun],
    config: EvalExperimentConfig,
) -> CandidateDecision:
    reasons: list[str] = []
    if any(run.preset == preset and run.exit_code != 0 for run in search_runs):
        return CandidateDecision(
            preset=preset,
            status="failed",
            promoted_to_extended=False,
            reasons=["search bench command failed"],
        )

    if any(run.exit_code != 0 or run.summary is None for run in small_runs):
        return CandidateDecision(
            preset=preset,
            status="failed",
            promoted_to_extended=False,
            reasons=["small match command failed or summary unavailable"],
        )

    games = 0
    valid_games = 0
    wins = 0
    losses = 0
    draws = 0
    error_games = 0
    total_diff = 0
    depth_avg_diffs: dict[int, float] = {}
    for run in small_runs:
        summary = run.summary
        if summary is None:
            continue
        games += summary.games
        valid_games += summary.valid_games
        wins += summary.player_a_wins
        losses += summary.player_b_wins
        draws += summary.draws
        error_games += summary.error_games
        total_diff += summary.total_diff
        depth_avg_diffs[run.depth] = summary.average_diff

    avg_diff = total_diff / valid_games if valid_games else 0.0
    node_ratio = _aggregate_optional_ratio(small_runs, "nodes_player_a", "nodes_player_b")
    time_ratio = _aggregate_optional_ratio(small_runs, "time_ms_player_a", "time_ms_player_b")

    if error_games > 0:
        return CandidateDecision(
            preset=preset,
            status="failed",
            promoted_to_extended=False,
            reasons=[f"{error_games} error game(s)"],
            games=games,
            wins=wins,
            losses=losses,
            draws=draws,
            error_games=error_games,
            avg_diff=avg_diff,
            node_ratio=node_ratio,
            time_ratio=time_ratio,
            depth_avg_diffs=depth_avg_diffs,
        )

    if avg_diff < 0 and losses > wins:
        status = "rejected"
        reasons.append("negative average diff and more losses than wins")
    elif avg_diff >= config.min_avg_diff and wins > losses:
        status = "promoted_to_extended"
        reasons.append("wins exceed losses and average diff meets threshold")
    elif avg_diff > 0 and losses > wins:
        status = "needs_retune"
        reasons.append("positive average diff but losses exceed wins")
    else:
        status = "needs_retune"
        reasons.append("small match result is mixed or below promotion threshold")

    if config.require_nonnegative_diff and any(value < 0 for value in depth_avg_diffs.values()):
        status = "needs_retune" if status == "promoted_to_extended" else status
        reasons.append("at least one small depth has negative average diff")

    if node_ratio is not None and node_ratio > config.max_node_ratio:
        status = "needs_retune" if status == "promoted_to_extended" else status
        reasons.append(f"needs_speed_check: node ratio {node_ratio:.3f} exceeds {config.max_node_ratio:.3f}")

    return CandidateDecision(
        preset=preset,
        status=status,
        promoted_to_extended=status == "promoted_to_extended",
        reasons=reasons,
        games=games,
        wins=wins,
        losses=losses,
        draws=draws,
        error_games=error_games,
        avg_diff=avg_diff,
        node_ratio=node_ratio,
        time_ratio=time_ratio,
        depth_avg_diffs=depth_avg_diffs,
    )


def rank_promoted_candidates(
    decisions: list[CandidateDecision], promote_top: int
) -> list[CandidateDecision]:
    promoted = [decision for decision in decisions if decision.promoted_to_extended]
    promoted.sort(
        key=lambda decision: (
            decision.avg_diff,
            decision.wins - decision.losses,
            -(decision.node_ratio or 1.0),
        ),
        reverse=True,
    )
    if promote_top <= 0:
        return []
    return promoted[:promote_top]


def decide_promotions(
    candidates: list[str],
    small_runs: list[MatchRun],
    search_runs: list[SearchBenchRun],
    config: EvalExperimentConfig,
) -> list[CandidateDecision]:
    decisions = [
        decide_candidate(
            preset,
            [run for run in small_runs if run.preset == preset],
            search_runs,
            config,
        )
        for preset in candidates
    ]
    selected = {decision.preset for decision in rank_promoted_candidates(decisions, config.promote_top)}
    adjusted: list[CandidateDecision] = []
    for decision in decisions:
        if decision.promoted_to_extended and decision.preset not in selected:
            adjusted.append(
                CandidateDecision(
                    preset=decision.preset,
                    status="needs_retune",
                    promoted_to_extended=False,
                    reasons=[*decision.reasons, "not selected by promote-top limit"],
                    games=decision.games,
                    wins=decision.wins,
                    losses=decision.losses,
                    draws=decision.draws,
                    error_games=decision.error_games,
                    avg_diff=decision.avg_diff,
                    node_ratio=decision.node_ratio,
                    time_ratio=decision.time_ratio,
                    depth_avg_diffs=decision.depth_avg_diffs,
                )
            )
        else:
            adjusted.append(decision)
    return adjusted


def ntest_skipped_reason(config: EvalExperimentConfig) -> str | None:
    if not config.run_ntest_sanity:
        return "NTest sanity skipped: not requested"
    if config.ntest_engine is None or config.engines is None:
        return "NTest sanity skipped: config unavailable"
    return None


def select_ntest_targets(
    config: EvalExperimentConfig, decisions: list[CandidateDecision]
) -> list[EvalTarget]:
    targets: list[EvalTarget] = [config.reference_target]
    targets_by_label = {target.label: target for target in config.candidate_targets}
    promoted = [decision.preset for decision in decisions if decision.promoted_to_extended]
    fallback = config.candidate_presets[: config.promote_top] if config.promote_top > 0 else []
    for label in promoted or fallback:
        target = targets_by_label.get(label)
        if target is not None and target not in targets:
            targets.append(target)
    return targets


def _fmt_ratio(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.3f}"


def _fmt_float(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}"


def _md(value: object) -> str:
    return str(value).replace("|", "\\|")


def render_match_table(runs: list[MatchRun]) -> str:
    lines = [
        "| preset | stage | depth | games | wins | losses | draws | avg diff | errors | node ratio | time ratio | JSONL |",
        "| :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |",
    ]
    if not runs:
        lines.append("| - | - | 0 | 0 | 0 | 0 | 0 | n/a | 0 | n/a | n/a | - |")
        return "\n".join(lines)

    for run in runs:
        summary = run.summary
        if summary is None:
            lines.append(
                f"| `{run.preset}` | {run.stage} | {run.depth} | {run.games} | 0 | 0 | 0 | n/a | n/a | n/a | n/a | {_md(run.output_path)} |"
            )
            continue
        lines.append(
            "| "
            + " | ".join(
                (
                    f"`{run.preset}`",
                    run.stage,
                    str(run.depth),
                    str(summary.games),
                    str(summary.player_a_wins),
                    str(summary.player_b_wins),
                    str(summary.draws),
                    f"{summary.average_diff:.2f}",
                    str(summary.error_games),
                    _fmt_ratio(_optional_ratio(summary, "nodes_player_a", "nodes_player_b")),
                    _fmt_ratio(_optional_ratio(summary, "time_ms_player_a", "time_ms_player_b")),
                    _md(run.output_path),
                )
            )
            + " |"
        )
    return "\n".join(lines)


def render_promotion_table(decisions: list[CandidateDecision]) -> str:
    lines = [
        "| preset | status | promoted | games | W-L-D | avg diff | errors | node ratio | time ratio | reasons |",
        "| :--- | :--- | :---: | ---: | :--- | ---: | ---: | ---: | ---: | :--- |",
    ]
    if not decisions:
        lines.append("| - | not_run | no | 0 | 0-0-0 | 0.00 | 0 | n/a | n/a | no candidates |")
        return "\n".join(lines)

    for decision in decisions:
        lines.append(
            "| "
            + " | ".join(
                (
                    f"`{decision.preset}`",
                    decision.status,
                    "yes" if decision.promoted_to_extended else "no",
                    str(decision.games),
                    f"{decision.wins}-{decision.losses}-{decision.draws}",
                    f"{decision.avg_diff:.2f}",
                    str(decision.error_games),
                    _fmt_ratio(decision.node_ratio),
                    _fmt_ratio(decision.time_ratio),
                    _md("; ".join(decision.reasons) or "-"),
                )
            )
            + " |"
        )
    return "\n".join(lines)


def render_ntest_table(ntest_runs: list[NTestRun]) -> str:
    lines = [
        "| preset | depth | games | wins | losses | draws | avg diff | errors | error | JSONL |",
        "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- | :--- |",
    ]
    if not ntest_runs:
        lines.append("| - | 0 | 0 | 0 | 0 | 0 | n/a | 0 | - | - |")
        return "\n".join(lines)
    for run in ntest_runs:
        summary = run.summary
        if summary is None:
            error = run.error or "summary unavailable"
            lines.append(
                f"| `{run.preset}` | {run.depth} | 0 | 0 | 0 | 0 | n/a | n/a | "
                f"{_md(error)} | {_md(run.output_path)} |"
            )
            continue
        lines.append(
            f"| `{run.preset}` | {run.depth} | {summary.games} | {summary.player_a_wins} | "
            f"{summary.player_b_wins} | {summary.draws} | {summary.average_diff:.2f} | "
            f"{summary.error_games} | {_md(run.error or '-')} | {_md(run.output_path)} |"
        )
    return "\n".join(lines)


def ntest_issue_runs(ntest_runs: list[NTestRun]) -> list[NTestRun]:
    return [
        run
        for run in ntest_runs
        if run.exit_code != 0 or run.summary is None or run.error is not None
    ]


def render_search_table(search_runs: list[SearchBenchRun], *, dry_run: bool) -> str:
    lines = [
        "| preset | status | output |",
        "| :--- | :--- | :--- |",
    ]
    if not search_runs:
        lines.append("| - | not_run | - |")
        return "\n".join(lines)

    for run in search_runs:
        status = "dry-run" if dry_run else f"exit {run.exit_code}"
        output = _md(run.output_path) if run.output_path is not None else "-"
        lines.append(f"| `{run.preset}` | {status} | {output} |")
    return "\n".join(lines)


def render_report(
    config: EvalExperimentConfig,
    search_runs: list[SearchBenchRun],
    small_runs: list[MatchRun],
    extended_runs: list[MatchRun],
    decisions: list[CandidateDecision],
    ntest_runs: list[NTestRun],
    ntest_skip_reason: str | None,
    *,
    dry_run: bool,
) -> str:
    git = detect_git_metadata(Path.cwd())
    timestamp = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    failed = [run for run in search_runs if run.exit_code != 0]
    failed.extend(run for run in [*small_runs, *extended_runs] if run.exit_code != 0 or run.summary is None)
    error_game_runs = [
        run
        for run in [*small_runs, *extended_runs]
        if run.summary is not None and run.summary.error_games > 0
    ]

    candidate_labels = [target.label for target in config.eval_targets]
    config_labels = [target.label for target in config.configs]
    rejected = [decision.preset for decision in decisions if decision.status == "rejected"]
    mixed = [decision.preset for decision in decisions if decision.status == "needs_retune"]
    speed = [
        decision.preset
        for decision in decisions
        if any("needs_speed_check" in reason for reason in decision.reasons)
    ]
    promoted = [decision.preset for decision in decisions if decision.promoted_to_extended]
    candidate_worth_base_head = promoted[:1]
    candidate_worth_ntest = promoted[: config.promote_top] if config.run_ntest_sanity else []

    lines = [
        "# Evaluation Experiment Matrix",
        "",
        "Status: dry run." if dry_run else "Status: local evaluator preset experiment.",
        "",
        "No strength claim. This report supports reproducible comparison and triage only.",
        "",
        "## Metadata",
        "",
        f"- Generated at: `{timestamp}`",
        f"- Git ref: `{git.ref}`",
        f"- Git SHA: `{git.sha}`",
        f"- Build dir: `{config.build_dir}`",
        f"- Build type: `{detect_build_type(config.build_dir)}`",
        f"- Candidate presets: `{','.join(config.presets)}`",
        f"- Reference preset: `{config.reference_preset}`",
        f"- Candidate configs: `{','.join(config_labels) if config_labels else 'none'}`",
        f"- Reference config: `{config.reference_config.label if config.reference_config else 'none'}`",
        f"- Candidate evaluators: `{','.join(candidate_labels)}`",
        f"- Reference evaluator: `{config.reference_target.label}`",
        f"- Search depths: `{','.join(str(depth) for depth in config.search_depths)}`",
        f"- Small depths: `{','.join(str(depth) for depth in config.small_depths)}`",
        f"- Extended depths: `{','.join(str(depth) for depth in config.extended_depths) or 'none'}`",
        f"- Small games: `{config.small_games}`",
        f"- Extended games: `{config.extended_games}`",
        f"- Promote top: `{config.promote_top}`",
        f"- Max node ratio: `{config.max_node_ratio}`",
        f"- Min average diff: `{config.min_avg_diff}`",
        f"- Require nonnegative diff: `{'true' if config.require_nonnegative_diff else 'false'}`",
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
    for run in [*small_runs, *extended_runs]:
        lines.extend(("```sh", quote_command(run.command), "```", ""))
    for run in ntest_runs:
        lines.extend(("```sh", quote_command(run.command), "```", ""))

    write_report_section(lines, "Search Screening Summary", render_search_table(search_runs, dry_run=dry_run))
    write_report_section(lines, "Small Match Summary", render_match_table(small_runs))
    write_report_section(lines, "Extended Match Summary", render_match_table(extended_runs))
    write_report_section(lines, "Promotion Table", render_promotion_table(decisions))
    write_report_section(
        lines,
        "Rejected Candidates",
        ", ".join(f"`{preset}`" for preset in rejected) if rejected else "None.",
    )
    write_report_section(
        lines,
        "Mixed Candidates",
        ", ".join(f"`{preset}`" for preset in mixed) if mixed else "None.",
    )
    write_report_section(
        lines,
        "Speed Concerns",
        ", ".join(f"`{preset}`" for preset in speed) if speed else "None.",
    )
    lines.extend(("## Optional NTest Sanity", ""))
    if ntest_skip_reason is not None:
        lines.append(ntest_skip_reason)
    else:
        lines.append(render_ntest_table(ntest_runs))
        if not dry_run:
            ntest_issues = ntest_issue_runs(ntest_runs)
            if ntest_issues:
                lines.append("")
                lines.append(
                    f"NTest sanity issue found: {len(ntest_issues)} run(s) "
                    "failed or had unavailable summaries."
                )
            else:
                lines.append("")
                lines.append("NTest sanity attempted: no issue found.")
    lines.append("")

    lines.extend(
        (
            "## Final Recommendation",
            "",
            "- No strength claim.",
            f"- Top candidates for next sweep: `{','.join(promoted) if promoted else 'none'}`",
            f"- Candidate worth base/head check: `{','.join(candidate_worth_base_head) if candidate_worth_base_head else 'none'}`",
            f"- Candidate worth NTest sanity: `{','.join(candidate_worth_ntest) if candidate_worth_ntest else 'none'}`",
            "- Do not recommend default promotion from this report alone.",
        )
    )
    if failed and not dry_run:
        lines.append(f"- Infrastructure issue found: {len(failed)} command(s) failed.")
    elif error_game_runs and not config.allow_errors:
        total_error_games = sum(run.summary.error_games for run in error_game_runs if run.summary)
        lines.append(f"- Infrastructure issue found: {total_error_games} error game(s) were reported.")
    else:
        lines.append("- Infrastructure issue found: none in this run.")

    return "\n".join(lines) + "\n"


def _execute_match_run(run: MatchRun) -> MatchRun:
    run.output_path.parent.mkdir(parents=True, exist_ok=True)
    exit_code, output = run_capture(run.command)
    if exit_code != 0:
        return MatchRun(
            preset=run.preset,
            stage=run.stage,
            depth=run.depth,
            games=run.games,
            reference_preset=run.reference_preset,
            output_path=run.output_path,
            command=run.command,
            exit_code=exit_code,
            error=output.strip() or f"runner exited with {exit_code}",
        )
    try:
        return summarize_match(run)
    except ScriptError as exc:
        return MatchRun(
            preset=run.preset,
            stage=run.stage,
            depth=run.depth,
            games=run.games,
            reference_preset=run.reference_preset,
            output_path=run.output_path,
            command=run.command,
            exit_code=0,
            error=str(exc),
        )


def _execute_ntest_run(run: NTestRun) -> NTestRun:
    run.output_path.parent.mkdir(parents=True, exist_ok=True)
    exit_code, output = run_capture(run.command)
    if exit_code != 0:
        return NTestRun(
            preset=run.preset,
            depth=run.depth,
            output_path=run.output_path,
            command=run.command,
            exit_code=exit_code,
            error=output.strip() or f"runner exited with {exit_code}",
        )
    try:
        return summarize_ntest(run)
    except ScriptError as exc:
        return NTestRun(
            preset=run.preset,
            depth=run.depth,
            output_path=run.output_path,
            command=run.command,
            exit_code=0,
            error=str(exc),
        )


def run_matrix(config: EvalExperimentConfig, *, dry_run: bool) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    (config.out_dir / "search").mkdir(parents=True, exist_ok=True)
    (config.out_dir / "matches" / "small").mkdir(parents=True, exist_ok=True)
    (config.out_dir / "matches" / "extended").mkdir(parents=True, exist_ok=True)

    search_runs: list[SearchBenchRun] = []
    for target in config.eval_targets:
        command = build_search_bench_command(config, target)
        output_path = search_output_path(config, target)
        print(f"search bench {target.label}: {quote_command(command)}", flush=True)
        if dry_run:
            search_runs.append(
                SearchBenchRun(preset=target.label, command=command, output_path=output_path)
            )
            continue
        exit_code, output = run_capture(command)
        output_path.write_text(output, encoding="utf-8")
        search_runs.append(
            SearchBenchRun(
                preset=target.label,
                command=command,
                output_path=output_path,
                exit_code=exit_code,
                output=output,
            )
    )

    small_runs: list[MatchRun] = []
    for target in config.candidate_targets:
        for depth in config.small_depths:
            run = build_match_command(
                config, target, depth, stage="small", games=config.small_games
            )
            print(
                f"small match {target.label} depth {depth}: {quote_command(run.command)}",
                flush=True,
            )
            small_runs.append(run if dry_run else _execute_match_run(run))

    if dry_run:
        selected_for_extended = config.candidate_presets[: config.promote_top]
        decisions = [
            CandidateDecision(
                preset=preset,
                status="not_run",
                promoted_to_extended=preset in selected_for_extended,
                reasons=["dry run"],
            )
            for preset in config.candidate_presets
        ]
    else:
        decisions = decide_promotions(config.candidate_presets, small_runs, search_runs, config)
        selected_for_extended = [decision.preset for decision in decisions if decision.promoted_to_extended]

    extended_runs: list[MatchRun] = []
    if config.extended_depths and config.promote_top > 0:
        targets_by_label = {target.label: target for target in config.candidate_targets}
        for preset in selected_for_extended:
            target = targets_by_label[preset]
            for depth in config.extended_depths:
                run = build_match_command(
                    config, target, depth, stage="extended", games=config.extended_games
                )
                print(
                    f"extended match {preset} depth {depth}: {quote_command(run.command)}",
                    flush=True,
                )
                extended_runs.append(run if dry_run else _execute_match_run(run))

    ntest_runs: list[NTestRun] = []
    skip_reason = ntest_skipped_reason(config)
    if skip_reason is None:
        for target in select_ntest_targets(config, decisions):
            run = build_ntest_command(config, target)
            print(f"ntest sanity {target.label}: {quote_command(run.command)}", flush=True)
            ntest_runs.append(run if dry_run else _execute_ntest_run(run))

    report_path = config.out_dir / "report.md"
    report_path.write_text(
        render_report(
            config,
            search_runs,
            small_runs,
            extended_runs,
            decisions,
            ntest_runs,
            skip_reason,
            dry_run=dry_run,
        ),
        encoding="utf-8",
    )
    print(f"report: {report_path}", flush=True)

    if dry_run:
        return 0
    all_match_runs = [*small_runs, *extended_runs]
    if matrix_has_failures(search_runs, all_match_runs, allow_errors=config.allow_errors):
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


def _positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected float, got: {value}") from exc
    if parsed <= 0.0:
        raise argparse.ArgumentTypeError(f"expected positive float, got: {value}")
    return parsed


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run staged evaluator preset search and match experiments."
    )
    parser.add_argument("--presets", default="", help="comma-separated evaluator preset names")
    parser.add_argument("--configs", default="", help="comma-separated .eval config paths")
    parser.add_argument(
        "--reference-preset",
        default="default",
        help="opponent preset for candidate matches (default: default)",
    )
    parser.add_argument("--reference-config", help="opponent .eval config path")
    parser.add_argument(
        "--depths",
        help="backward-compatible alias for --small-depths",
    )
    parser.add_argument(
        "--games",
        type=_positive_int,
        help="backward-compatible alias for --small-games",
    )
    parser.add_argument("--small-depths", help="comma-separated small-match depths")
    parser.add_argument("--extended-depths", default="", help="comma-separated extended depths")
    parser.add_argument("--small-games", type=_positive_int, help="games per small match")
    parser.add_argument("--extended-games", type=_positive_int, help="games per extended match")
    parser.add_argument("--promote-top", default=0, type=int, help="max candidates promoted to extended")
    parser.add_argument(
        "--max-node-ratio",
        default=1.15,
        type=_positive_float,
        help="node ratio threshold for speed concerns",
    )
    parser.add_argument(
        "--min-avg-diff",
        default=0.0,
        type=float,
        help="minimum aggregate average disc diff for promotion",
    )
    parser.add_argument(
        "--require-nonnegative-diff",
        default="false",
        help="true/false; require every small depth to have nonnegative avg diff",
    )
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
        choices=("smoke", "suite", "evaluation"),
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
    parser.add_argument("--run-ntest-sanity", action="store_true", help="schedule optional NTest sanity")
    parser.add_argument("--ntest-engine", help="external engine name in --engines config")
    parser.add_argument("--engines", help="match runner engines config path")
    parser.add_argument("--ntest-depth", type=_positive_int, help="NTest sanity search depth")
    parser.add_argument("--ntest-games", type=_positive_int, help="NTest sanity games")
    parser.add_argument("--ntest-openings", help="NTest sanity opening suite")
    parser.add_argument(
        "--smoke-run",
        action="store_true",
        help="limit to the first two presets, first small depth, and at most six small games",
    )
    parser.add_argument("--dry-run", action="store_true", help="write commands/report only")
    return parser.parse_args(argv)


def _parse_optional_depths(value: str | None) -> list[int]:
    if value is None or value.strip() == "":
        return []
    return parse_depth_list(value)


def config_from_args(args: argparse.Namespace) -> EvalExperimentConfig:
    build_dir = Path(args.build_dir)
    presets = parse_csv_names(args.presets) if args.presets.strip() else []
    config_paths = parse_csv_paths(args.configs) if args.configs.strip() else []
    configs = [config_target(path) for path in config_paths]
    reference_config = config_target(Path(args.reference_config)) if args.reference_config else None
    if not presets and not configs:
        raise ScriptError("at least one evaluator is required; pass --presets or --configs")
    small_depths = _parse_optional_depths(args.small_depths) or _parse_optional_depths(args.depths)
    if not small_depths:
        raise ScriptError("small depths are required; pass --small-depths or --depths")
    small_games = args.small_games if args.small_games is not None else args.games
    if small_games is None:
        raise ScriptError("small games are required; pass --small-games or --games")
    extended_depths = _parse_optional_depths(args.extended_depths)
    extended_games = args.extended_games if args.extended_games is not None else small_games
    promote_top = args.promote_top
    if promote_top < 0:
        raise ScriptError(f"promote-top must be nonnegative: {promote_top}")
    if args.smoke_run:
        presets = presets[:2]
        configs = configs[: max(0, 2 - len(presets))]
        small_depths = small_depths[:1]
        extended_depths = extended_depths[:1]
        small_games = min(small_games, 6)
        extended_games = min(extended_games, 6)
        promote_top = min(promote_top, 1)
    reference_target = (
        reference_config
        if reference_config is not None
        else EvalTarget(kind="preset", value=args.reference_preset, label=args.reference_preset)
    )
    validate_unique_eval_targets(
        [*(EvalTarget(kind="preset", value=preset, label=preset) for preset in presets), *configs],
        reference_target,
    )
    return EvalExperimentConfig(
        presets=presets,
        reference_preset=args.reference_preset,
        small_depths=small_depths,
        extended_depths=extended_depths,
        small_games=small_games,
        extended_games=extended_games,
        promote_top=promote_top,
        max_node_ratio=args.max_node_ratio,
        min_avg_diff=args.min_avg_diff,
        require_nonnegative_diff=parse_bool(args.require_nonnegative_diff),
        openings=Path(args.openings),
        seed=args.seed,
        build_dir=build_dir,
        out_dir=Path(args.out),
        search_bench=Path(args.search_bench) if args.search_bench else build_dir / "othello_search_bench",
        match_runner=Path(args.match_runner) if args.match_runner else build_dir / "othello_match_runner",
        exact_endgame_threshold=args.exact_endgame_threshold,
        configs=configs,
        reference_config=reference_config,
        positions=args.positions,
        by_position=args.by_position,
        allow_errors=args.allow_errors,
        run_ntest_sanity=args.run_ntest_sanity,
        ntest_engine=args.ntest_engine,
        engines=Path(args.engines) if args.engines else None,
        ntest_depth=args.ntest_depth,
        ntest_games=args.ntest_games,
        ntest_openings=Path(args.ntest_openings) if args.ntest_openings else None,
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
