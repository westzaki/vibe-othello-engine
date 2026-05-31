#!/usr/bin/env python3
"""Collect reproducible evidence for engine, search, and evaluator PRs.

This script records commands and outputs from existing C++ tools. It does not
implement Othello rules and does not make strength claims.
"""

from __future__ import annotations

import argparse
import datetime as dt
import platform
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol

import match_summary
from common import (
    ScriptError,
    parse_csv_paths,
    parse_csv_values,
    quote_command,
    slugify,
    write_report_section,
)


PROFILES = ("smoke", "pr", "eval", "full")
DEFAULT_SEED = 20260531
DEFAULT_EVAL_DEPTHS = "5"
DEFAULT_EVAL_GAMES = 4
EXACT_MIDGAME_THRESHOLD = 0


@dataclass(frozen=True)
class EvidenceConfig:
    profile: str
    source_dir: Path
    build_dir: Path
    out_dir: Path
    build_type: str
    skip_configure: bool
    skip_build: bool
    allow_failures: bool
    dry_run: bool
    search_bench: Path
    match_runner: Path
    openings: Path | None
    seed: int
    small_depths: str
    small_games: int
    eval_presets: list[str]
    eval_configs: list[Path]
    reference_preset: str
    reference_config: Path | None
    invocation: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class Metadata:
    generated_at: str
    git_ref: str
    git_sha: str
    git_dirty: bool
    git_diff_stat: str
    build_dir: Path
    build_type: str
    cmake_version: str
    compiler: str
    platform: str
    cpu: str
    invocation: list[str]


@dataclass(frozen=True)
class Step:
    name: str
    group: str
    command: list[str] = field(default_factory=list)
    cwd: Path = Path(".")
    log_path: Path | None = None
    required: bool = True
    skipped_reason: str | None = None
    note: str = ""


@dataclass(frozen=True)
class StepResult:
    name: str
    group: str
    command: list[str]
    cwd: Path
    log_path: Path | None
    required: bool
    status: str
    exit_code: int | None = None
    elapsed_seconds: float | None = None
    skipped_reason: str | None = None
    note: str = ""


@dataclass(frozen=True)
class MatchEvidence:
    jsonl_path: Path
    error_games: int | None = None
    summary_error: str | None = None


class StepExecutor(Protocol):
    def __call__(self, step: Step, *, dry_run: bool) -> StepResult:
        ...


def default_out_dir(source_dir: Path, profile: str) -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%d-%H%M%S")
    return source_dir / "runs" / "evidence" / f"{profile}-{timestamp}"


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
        description="Collect reproducible evidence for Othello engine PRs."
    )
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--source-dir", default=".", help="repository source directory")
    parser.add_argument("--build-dir", default="build", help="CMake build directory")
    parser.add_argument("--out", help="output directory; defaults under runs/evidence/")
    parser.add_argument("--build-type", default="Release", help="CMake build type")
    parser.add_argument("--skip-configure", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="write the report but return success even when required steps fail",
    )
    parser.add_argument("--dry-run", action="store_true", help="write a planned report only")
    parser.add_argument("--search-bench", help="path to othello_search_bench")
    parser.add_argument("--match-runner", help="path to othello_match_runner")
    parser.add_argument("--openings", help="opening suite for match/eval profiles")
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--small-depths", default=DEFAULT_EVAL_DEPTHS)
    parser.add_argument("--small-games", type=_positive_int, default=DEFAULT_EVAL_GAMES)
    parser.add_argument("--eval-presets", default="", help="comma-separated evaluator presets")
    parser.add_argument("--eval-configs", default="", help="comma-separated .eval config paths")
    parser.add_argument("--reference-preset", default="default")
    parser.add_argument("--reference-config", help="reference .eval config path")
    return parser.parse_args(argv)


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> EvidenceConfig:
    source_dir = Path(args.source_dir)
    build_dir = Path(args.build_dir)
    out_dir = Path(args.out) if args.out else default_out_dir(source_dir, args.profile)
    eval_presets = parse_csv_values(args.eval_presets) if args.eval_presets.strip() else []
    eval_configs = parse_csv_paths(args.eval_configs) if args.eval_configs.strip() else []
    return EvidenceConfig(
        profile=args.profile,
        source_dir=source_dir,
        build_dir=build_dir,
        out_dir=out_dir,
        build_type=args.build_type,
        skip_configure=args.skip_configure,
        skip_build=args.skip_build,
        allow_failures=args.allow_failures,
        dry_run=args.dry_run,
        search_bench=Path(args.search_bench) if args.search_bench else build_dir / "othello_search_bench",
        match_runner=Path(args.match_runner) if args.match_runner else build_dir / "othello_match_runner",
        openings=Path(args.openings) if args.openings else source_dir / "data" / "openings" / "smoke_openings.txt",
        seed=args.seed,
        small_depths=args.small_depths,
        small_games=args.small_games,
        eval_presets=eval_presets,
        eval_configs=eval_configs,
        reference_preset=args.reference_preset,
        reference_config=Path(args.reference_config) if args.reference_config else None,
        invocation=invocation or [],
    )


def _git_output(source_dir: Path, args: list[str]) -> str:
    completed = subprocess.run(
        ["git", "-C", str(source_dir), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if completed.returncode != 0:
        return "unknown"
    return completed.stdout.strip() or "unknown"


def _command_output(args: list[str]) -> str:
    completed = subprocess.run(
        args,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if completed.returncode != 0:
        return "unknown"
    return completed.stdout.strip() or "unknown"


def _cache_value(cache_path: Path, key: str) -> str | None:
    try:
        lines = cache_path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None
    prefix = f"{key}:"
    for line in lines:
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1] or None
    return None


def _compiler_info(build_dir: Path) -> str:
    cache = build_dir / "CMakeCache.txt"
    compiler = _cache_value(cache, "CMAKE_CXX_COMPILER")
    if compiler is None:
        return "unknown"
    version = _cache_value(cache, "CMAKE_CXX_COMPILER_VERSION")
    return f"{compiler} {version}" if version else compiler


def _cpu_model() -> str:
    if platform.system() == "Darwin":
        value = _command_output(["sysctl", "-n", "machdep.cpu.brand_string"])
        if value != "unknown":
            return value
    cpuinfo = Path("/proc/cpuinfo")
    try:
        for line in cpuinfo.read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name") and ":" in line:
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def collect_metadata(config: EvidenceConfig) -> Metadata:
    status = _git_output(config.source_dir, ["status", "--short"])
    diff_parts: list[str] = []
    if status != "unknown" and status:
        diff_parts.extend(("git status --short:", status))
    unstaged_stat = _git_output(config.source_dir, ["diff", "--stat"])
    if unstaged_stat != "unknown" and unstaged_stat:
        diff_parts.extend(("", "git diff --stat:", unstaged_stat))
    staged_stat = _git_output(config.source_dir, ["diff", "--cached", "--stat"])
    if staged_stat != "unknown" and staged_stat:
        diff_parts.extend(("", "git diff --cached --stat:", staged_stat))
    diff_stat = "\n".join(diff_parts) if diff_parts else "-"
    cmake_version = _command_output(["cmake", "--version"]).splitlines()[0]
    return Metadata(
        generated_at=dt.datetime.now(dt.UTC).isoformat(timespec="seconds"),
        git_ref=_git_output(config.source_dir, ["rev-parse", "--abbrev-ref", "HEAD"]),
        git_sha=_git_output(config.source_dir, ["rev-parse", "HEAD"]),
        git_dirty=status != "unknown" and bool(status),
        git_diff_stat=diff_stat,
        build_dir=config.build_dir,
        build_type=config.build_type,
        cmake_version=cmake_version,
        compiler=_compiler_info(config.build_dir),
        platform=platform.platform(),
        cpu=_cpu_model(),
        invocation=config.invocation,
    )


def log_path(config: EvidenceConfig, name: str) -> Path:
    return config.out_dir / "logs" / f"{name}.log"


def executable_exists(path: Path, source_dir: Path) -> bool:
    candidate = path if path.is_absolute() else source_dir / path
    return candidate.exists()


def opening_exists(path: Path | None, source_dir: Path) -> bool:
    if path is None:
        return False
    candidate = path if path.is_absolute() else source_dir / path
    return candidate.exists()


def configure_step(config: EvidenceConfig) -> Step:
    if config.skip_configure:
        return Step(
            name="configure",
            group="Build",
            cwd=config.source_dir,
            required=False,
            skipped_reason="--skip-configure was provided",
        )
    return Step(
        name="configure",
        group="Build",
        command=[
            "cmake",
            "-S",
            str(config.source_dir),
            "-B",
            str(config.build_dir),
            f"-DCMAKE_BUILD_TYPE={config.build_type}",
        ],
        cwd=config.source_dir,
        log_path=log_path(config, "configure"),
    )


def build_step(config: EvidenceConfig) -> Step:
    if config.skip_build:
        return Step(
            name="build",
            group="Build",
            cwd=config.source_dir,
            required=False,
            skipped_reason="--skip-build was provided",
        )
    return Step(
        name="build",
        group="Build",
        command=["cmake", "--build", str(config.build_dir)],
        cwd=config.source_dir,
        log_path=log_path(config, "build"),
    )


def ctest_step(config: EvidenceConfig) -> Step:
    return Step(
        name="ctest",
        group="Correctness Gate",
        command=["ctest", "--test-dir", str(config.build_dir), "--output-on-failure"],
        cwd=config.source_dir,
        log_path=log_path(config, "ctest"),
    )


def base_search_command(search_bench: Path, depths: str = "1,2,3,4,5") -> list[str]:
    return [
        str(search_bench),
        "--mode",
        "both",
        "--depths",
        depths,
        "--positions",
        "smoke",
        "--repetitions",
        "1",
        "--exact-endgame-threshold",
        str(EXACT_MIDGAME_THRESHOLD),
    ]


def search_smoke_step(config: EvidenceConfig) -> Step:
    return Step(
        name="search-smoke",
        group="Search / Evaluation Evidence",
        command=base_search_command(config.search_bench),
        cwd=config.source_dir,
        log_path=log_path(config, "search-smoke"),
        note="exact_endgame_threshold=0 isolates midgame/eval search from exact root solving",
    )


def rule_core_step(config: EvidenceConfig) -> Step:
    executable = config.build_dir / "othello_rule_core_bench"
    if (config.dry_run or config.skip_build) and not executable_exists(executable, config.source_dir):
        return Step(
            name="rule-core-bench",
            group="Search / Evaluation Evidence",
            cwd=config.source_dir,
            required=False,
            skipped_reason=f"optional executable not found: {executable}",
        )
    return Step(
        name="rule-core-bench",
        group="Search / Evaluation Evidence",
        command=[str(executable)],
        cwd=config.source_dir,
        log_path=log_path(config, "rule-core-bench"),
    )


def endgame_step(config: EvidenceConfig) -> Step:
    executable = config.build_dir / "othello_endgame_bench"
    if (config.dry_run or config.skip_build) and not executable_exists(executable, config.source_dir):
        return Step(
            name="endgame-bench-smoke",
            group="Search / Evaluation Evidence",
            cwd=config.source_dir,
            required=False,
            skipped_reason=f"optional executable not found: {executable}",
        )
    return Step(
        name="endgame-bench-smoke",
        group="Search / Evaluation Evidence",
        command=[str(executable), "--positions", "smoke"],
        cwd=config.source_dir,
        log_path=log_path(config, "endgame-bench-smoke"),
    )


def match_steps(config: EvidenceConfig) -> list[Step]:
    if not opening_exists(config.openings, config.source_dir):
        return [
            Step(
                name="match-self-play",
                group="Match Evidence",
                cwd=config.source_dir,
                required=False,
                skipped_reason=f"opening suite not found: {config.openings}",
            )
        ]
    if (config.dry_run or config.skip_build) and not executable_exists(
        config.match_runner, config.source_dir
    ):
        return [
            Step(
                name="match-self-play",
                group="Match Evidence",
                cwd=config.source_dir,
                required=False,
                skipped_reason=f"optional executable not found: {config.match_runner}",
            )
        ]
    output = config.out_dir / "matches" / "self-play-smoke.jsonl"
    summary_script = config.source_dir / "tools" / "scripts" / "match_summary.py"
    return [
        Step(
            name="match-self-play",
            group="Match Evidence",
            command=[
                str(config.match_runner),
                "--black",
                "search:depth=4,tt=on,pvs=on,exact=off",
                "--white",
                "search:depth=4,tt=on,pvs=on,exact=off",
                "--games",
                "4",
                "--swap-sides",
                "true",
                "--seed",
                str(config.seed),
                "--openings",
                str(config.openings),
                "--output",
                str(output),
                "--quiet",
            ],
            cwd=config.source_dir,
            log_path=log_path(config, "match-self-play"),
        ),
        Step(
            name="match-summary",
            group="Match Evidence",
            command=[sys.executable, str(summary_script), "--input", str(output), "--by-opening"],
            cwd=config.source_dir,
            log_path=log_path(config, "match-summary"),
        ),
    ]


@dataclass(frozen=True)
class EvalTarget:
    kind: str
    value: str
    label: str


def eval_targets(config: EvidenceConfig) -> list[EvalTarget]:
    targets = [EvalTarget(kind="preset", value=preset, label=preset) for preset in config.eval_presets]
    targets.extend(
        EvalTarget(kind="config", value=str(path), label=path.stem or str(path))
        for path in config.eval_configs
    )
    return targets


def eval_search_step(config: EvidenceConfig, target: EvalTarget, index: int) -> Step:
    option = "--eval-config" if target.kind == "config" else "--eval-preset"
    command = [
        str(config.search_bench),
        "--mode",
        "iterative",
        "--depths",
        config.small_depths,
        "--positions",
        "smoke",
        "--repetitions",
        "1",
        "--tt",
        "on",
        "--pvs",
        "on",
        "--aspiration",
        "on",
        option,
        target.value,
        "--exact-endgame-threshold",
        str(EXACT_MIDGAME_THRESHOLD),
    ]
    return Step(
        name=f"eval-search-{index}-{slugify(target.label)}",
        group="Search / Evaluation Evidence",
        command=command,
        cwd=config.source_dir,
        log_path=log_path(config, f"eval-search-{index}-{slugify(target.label)}"),
        note="exact_endgame_threshold=0 isolates midgame/eval search from exact root solving",
    )


def eval_matrix_step(config: EvidenceConfig, targets: list[EvalTarget]) -> Step:
    if not targets:
        return Step(
            name="eval-experiment-matrix",
            group="Search / Evaluation Evidence",
            cwd=config.source_dir,
            required=False,
            skipped_reason="no eval presets or configs were provided",
        )
    if not opening_exists(config.openings, config.source_dir):
        return Step(
            name="eval-experiment-matrix",
            group="Search / Evaluation Evidence",
            cwd=config.source_dir,
            required=False,
            skipped_reason=f"opening suite not found: {config.openings}",
        )
    script = config.source_dir / "tools" / "scripts" / "eval_experiment_matrix.py"
    command = [
        sys.executable,
        str(script),
        "--small-depths",
        config.small_depths,
        "--small-games",
        str(config.small_games),
        "--openings",
        str(config.openings),
        "--seed",
        str(config.seed),
        "--build-dir",
        str(config.build_dir),
        "--search-bench",
        str(config.search_bench),
        "--match-runner",
        str(config.match_runner),
        "--out",
        str(config.out_dir / "eval-matrix"),
        "--positions",
        "smoke",
        "--exact-endgame-threshold",
        str(EXACT_MIDGAME_THRESHOLD),
    ]
    if config.eval_presets:
        command.extend(["--presets", ",".join(config.eval_presets)])
    if config.eval_configs:
        command.extend(["--configs", ",".join(str(path) for path in config.eval_configs)])
    if config.reference_config is not None:
        command.extend(["--reference-config", str(config.reference_config)])
    else:
        command.extend(["--reference-preset", config.reference_preset])
    return Step(
        name="eval-experiment-matrix",
        group="Search / Evaluation Evidence",
        command=command,
        cwd=config.source_dir,
        log_path=log_path(config, "eval-experiment-matrix"),
        note="matrix report is a triage artifact, not a default-promotion recommendation",
    )


def full_profile_planned_step(config: EvidenceConfig) -> Step:
    return Step(
        name="analyze-position-suite",
        group="Search / Evaluation Evidence",
        cwd=config.source_dir,
        required=False,
        skipped_reason="planned full-profile check; no cheap committed analyze-position suite yet",
    )


def build_steps(config: EvidenceConfig) -> list[Step]:
    steps = [configure_step(config), build_step(config), ctest_step(config)]
    if config.profile in ("smoke", "pr", "full"):
        steps.append(search_smoke_step(config))
    if config.profile in ("pr", "full"):
        steps.append(rule_core_step(config))
        steps.extend(match_steps(config))
    if config.profile == "eval":
        targets = eval_targets(config)
        if targets:
            for index, target in enumerate(targets, start=1):
                steps.append(eval_search_step(config, target, index))
        else:
            steps.append(search_smoke_step(config))
        steps.append(eval_matrix_step(config, targets))
    if config.profile == "full":
        steps.append(endgame_step(config))
        steps.append(full_profile_planned_step(config))
    return steps


def execute_step(step: Step, *, dry_run: bool) -> StepResult:
    if step.skipped_reason is not None:
        return StepResult(
            name=step.name,
            group=step.group,
            command=step.command,
            cwd=step.cwd,
            log_path=step.log_path,
            required=step.required,
            status="skipped",
            skipped_reason=step.skipped_reason,
            note=step.note,
        )

    if dry_run:
        return StepResult(
            name=step.name,
            group=step.group,
            command=step.command,
            cwd=step.cwd,
            log_path=step.log_path,
            required=step.required,
            status="planned",
            note=step.note,
        )

    if step.log_path is not None:
        step.log_path.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    try:
        completed = subprocess.run(
            step.command,
            cwd=step.cwd,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        output = completed.stdout
        exit_code = int(completed.returncode)
    except OSError as exc:
        output = f"failed to execute command: {exc}\n"
        exit_code = 127
    elapsed = time.monotonic() - start
    if step.log_path is not None:
        step.log_path.write_text(output, encoding="utf-8")
    return StepResult(
        name=step.name,
        group=step.group,
        command=step.command,
        cwd=step.cwd,
        log_path=step.log_path,
        required=step.required,
        status="passed" if exit_code == 0 else "failed",
        exit_code=exit_code,
        elapsed_seconds=elapsed,
        note=step.note,
    )


def required_failures(results: list[StepResult]) -> list[StepResult]:
    return [result for result in results if result.required and result.status == "failed"]


def report_status(config: EvidenceConfig, results: list[StepResult]) -> str:
    if config.dry_run:
        return "dry run"
    if required_failures(results):
        return "completed with failures"
    return "completed"


def relative_to_out(config: EvidenceConfig, path: Path | None) -> str:
    if path is None:
        return "-"
    try:
        return str(path.relative_to(config.out_dir))
    except ValueError:
        return str(path)


def command_block(command: list[str]) -> str:
    return f"```sh\n{quote_command(command)}\n```"


def result_line(config: EvidenceConfig, result: StepResult) -> str:
    if result.status == "skipped":
        return f"- `{result.name}`: skipped - {result.skipped_reason}"
    if result.status == "planned":
        return f"- `{result.name}`: planned; log `{relative_to_out(config, result.log_path)}`"
    detail = f"exit {result.exit_code}"
    if result.elapsed_seconds is not None:
        detail += f", {result.elapsed_seconds:.2f}s"
    return f"- `{result.name}`: {result.status} ({detail}); log `{relative_to_out(config, result.log_path)}`"


def collect_match_evidence(config: EvidenceConfig, results: list[StepResult]) -> list[MatchEvidence]:
    if config.dry_run:
        return []
    evidence: list[MatchEvidence] = []
    jsonl = config.out_dir / "matches" / "self-play-smoke.jsonl"
    if not jsonl.exists():
        return evidence
    try:
        summary = match_summary.summarize(match_summary.load_records(jsonl))
        evidence.append(MatchEvidence(jsonl_path=jsonl, error_games=summary.error_games))
    except ScriptError as exc:
        evidence.append(MatchEvidence(jsonl_path=jsonl, summary_error=str(exc)))
    return evidence


def render_metadata(metadata: Metadata) -> str:
    dirty = "yes" if metadata.git_dirty else "no"
    lines = [
        f"- Generated at: `{metadata.generated_at}`",
        f"- Git ref: `{metadata.git_ref}`",
        f"- Git SHA: `{metadata.git_sha}`",
        f"- Working tree dirty: `{dirty}`",
        f"- Build dir: `{metadata.build_dir}`",
        f"- Build type: `{metadata.build_type}`",
        f"- CMake: `{metadata.cmake_version}`",
        f"- Compiler: `{metadata.compiler}`",
        f"- Platform: `{metadata.platform}`",
        f"- CPU: `{metadata.cpu}`",
        f"- Invocation: `{quote_command(metadata.invocation) if metadata.invocation else '-'}`",
    ]
    if metadata.git_dirty:
        lines.extend(("", "Diff stat:", "", "```text", metadata.git_diff_stat, "```"))
    return "\n".join(lines)


def render_commands(results: list[StepResult]) -> str:
    lines: list[str] = []
    for result in results:
        if result.status == "skipped":
            lines.append(f"### {result.name}")
            lines.append("")
            lines.append(f"Skipped: {result.skipped_reason}")
            lines.append("")
            continue
        lines.append(f"### {result.name}")
        lines.append("")
        lines.append(command_block(result.command))
        lines.append("")
    return "\n".join(lines).rstrip()


def render_group(config: EvidenceConfig, results: list[StepResult], group: str) -> str:
    matching = [result for result in results if result.group == group]
    if not matching:
        return "- none"
    return "\n".join(result_line(config, result) for result in matching)


def render_search_notes(config: EvidenceConfig, results: list[StepResult]) -> str:
    lines = [
        render_group(config, results, "Search / Evaluation Evidence"),
        "",
        f"- exact_endgame_threshold used for midgame/eval search evidence: `{EXACT_MIDGAME_THRESHOLD}`",
        "- note: exact threshold 0 means this isolates midgame/eval search from exact root solving",
    ]
    if config.profile == "eval":
        lines.append("- No default promotion is recommended from this report alone.")
    return "\n".join(lines)


def render_match_notes(config: EvidenceConfig, results: list[StepResult], matches: list[MatchEvidence]) -> str:
    lines = [render_group(config, results, "Match Evidence")]
    if not matches:
        lines.append("- error games count: unavailable")
        return "\n".join(lines)
    for evidence in matches:
        lines.append(f"- match JSONL: `{relative_to_out(config, evidence.jsonl_path)}`")
        if evidence.error_games is not None:
            lines.append(f"- error games count: `{evidence.error_games}`")
        if evidence.summary_error is not None:
            lines.append(f"- summary error: `{evidence.summary_error}`")
    return "\n".join(lines)


def render_report(
    config: EvidenceConfig,
    metadata: Metadata,
    results: list[StepResult],
    matches: list[MatchEvidence],
) -> str:
    status = report_status(config, results)
    lines = [
        "# Evidence Report",
        "",
        f"Status: {status}",
        "",
        "No strength claim. This report collects evidence for review only.",
        "",
    ]
    write_report_section(lines, "Metadata", render_metadata(metadata))
    write_report_section(
        lines,
        "Profile",
        [
            f"- Profile: `{config.profile}`",
            f"- Output directory: `{config.out_dir}`",
        ],
    )
    write_report_section(lines, "Commands", render_commands(results))
    write_report_section(lines, "Correctness Gate", render_group(config, results, "Correctness Gate"))
    write_report_section(lines, "Search / Evaluation Evidence", render_search_notes(config, results))
    write_report_section(lines, "Match Evidence", render_match_notes(config, results, matches))
    write_report_section(
        lines,
        "Semantic Change Notes",
        [
            "- No baseline comparison is available in this report.",
            "- Treat best move, score, PV, or checksum changes as semantic-change evidence.",
            "- If only timing or node counts change with stable checksums, treat it as speed evidence.",
        ],
    )
    write_report_section(
        lines,
        "Caveats",
        [
            "- No Elo claim.",
            "- No default promotion recommendation from this report alone.",
            "- Raw logs are under runs/ and should not be committed.",
        ],
    )
    write_report_section(
        lines,
        "Suggested Next Checks",
        [
            "- Base/head external-process comparison if behavior changed.",
            "- NTest sanity before default promotion.",
            "- Exact-label validation for evaluator promotion when available.",
        ],
    )
    return "\n".join(lines)


def write_report(config: EvidenceConfig, report: str) -> Path:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    path = config.out_dir / "report.md"
    path.write_text(report, encoding="utf-8")
    return path


def run_evidence(config: EvidenceConfig, *, executor: StepExecutor = execute_step) -> int:
    config.out_dir.mkdir(parents=True, exist_ok=True)
    metadata = collect_metadata(config)
    results = [executor(step, dry_run=config.dry_run) for step in build_steps(config)]
    matches = collect_match_evidence(config, results)
    report = render_report(config, metadata, results, matches)
    report_path = write_report(config, report)
    print(f"report: {report_path}", flush=True)
    if required_failures(results) and not config.allow_failures:
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_evidence(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
