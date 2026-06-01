#!/usr/bin/env python3
"""Run a local NTest teacher-label smoke and estimate full-run feasibility."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import shutil
import subprocess
import sys
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import balanced_position_pool as position_pool
from common import ScriptError, quote_command
import external_teacher_label_workflow as teacher_workflow


DEFAULT_COUNT = 1200
DEFAULT_SEED = 20260601
DEFAULT_TARGET_LABELS = 300000
DEFAULT_OVERNIGHT_SECONDS = 12 * 60 * 60
DEFAULT_TIMEOUT_MS = 60000
DEFAULT_JOBS = 1
DEFAULT_POSITION_LOG_MODE = "failures"
DEFAULT_TEACHER_ENGINE_LIFECYCLE = "per-request"
DEFAULT_SAMPLER = Path("build") / "othello_position_sampler"
DEFAULT_LEGAL_VALIDATOR = Path("build") / "othello_validate_move"
REPO_ROOT = Path(__file__).resolve().parents[2]
RUN_ROOT = Path("runs") / "ntest-teacher-smoke"
PYTHON = "python3"

ACTION_OK = "OK for overnight"
ACTION_REDUCE_DEPTH = "reduce depth"
ACTION_INCREASE_TIMEOUT = "increase timeout"
ACTION_REDUCE_JOBS = "reduce jobs"
ACTION_INSPECT_FAILURES = "inspect failures"

SMOKE_EMPTY_COUNTS = tuple(bucket[0] for bucket in position_pool.SMOKE_BUCKETS)


@dataclass(frozen=True)
class SmokeConfig:
    dataset_root: Path
    dataset_id: str
    ntest_cmd: list[str]
    ntest_workdir: Path | None
    depth: int
    timeout_ms: int
    jobs: int
    position_log_mode: str
    teacher_engine_lifecycle: str
    sampler: Path
    legal_validator: Path
    seed: int
    bucket_spec: str
    dry_run: bool
    run_dir: Path
    invocation: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class SmokeResult:
    requested_labels: int
    ok_labels: int
    failed_labels: int
    timed_out_labels: int
    illegal_teacher_moves: int
    elapsed_seconds: float
    labels_per_second: float
    estimated_300k_seconds: float | None
    recommended_action: str
    teacher_exit_code: int
    position_pool_status: str


Runner = Callable[..., subprocess.CompletedProcess[str]]
Clock = Callable[[], float]


def parse_positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a positive integer") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def parse_non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a non-negative integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be a non-negative integer")
    return parsed


def _split_ntest_cmd(values: list[str]) -> tuple[list[str], list[str]]:
    if "--ntest-cmd" not in values:
        raise ScriptError("--ntest-cmd is required and must be followed by '--' and a command")
    index = values.index("--ntest-cmd")
    helper_args = values[:index]
    ntest_args = values[index + 1 :]
    if not ntest_args or ntest_args[0] != "--" or len(ntest_args) == 1:
        raise ScriptError("--ntest-cmd must be followed by '--' and a command")
    return helper_args, ntest_args[1:]


def default_run_dir() -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    return RUN_ROOT / timestamp


def bucket_spec_for_count(count: int) -> str:
    if count <= 0:
        raise ScriptError("--count must be a positive integer")
    base, remainder = divmod(count, len(SMOKE_EMPTY_COUNTS))
    buckets: list[position_pool.Bucket] = []
    for index, empties in enumerate(SMOKE_EMPTY_COUNTS):
        bucket_count = base + (1 if index < remainder else 0)
        if bucket_count > 0:
            buckets.append(position_pool.Bucket(empties=empties, count=bucket_count))
    return position_pool.bucket_spec_text(buckets)


def normalize_bucket_spec(value: str) -> str:
    return position_pool.bucket_spec_text(position_pool.parse_bucket_spec(value))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    values = list(sys.argv[1:] if argv is None else argv)
    helper_args, ntest_cmd = _split_ntest_cmd(values)

    parser = argparse.ArgumentParser(
        description="Run a local NTest teacher-label smoke and estimate full-run feasibility."
    )
    parser.add_argument("--dataset-root", required=True)
    parser.add_argument("--dataset-id", required=True)
    parser.add_argument("--ntest-workdir")
    parser.add_argument("--depth", type=parse_positive_int, required=True)
    parser.add_argument("--timeout-ms", type=parse_positive_int, default=DEFAULT_TIMEOUT_MS)
    parser.add_argument("--jobs", type=parse_positive_int, default=DEFAULT_JOBS)
    parser.add_argument(
        "--position-log-mode",
        choices=teacher_workflow.POSITION_LOG_MODES,
        default=DEFAULT_POSITION_LOG_MODE,
    )
    parser.add_argument(
        "--teacher-engine-lifecycle",
        choices=teacher_workflow.ENGINE_LIFECYCLES,
        default=DEFAULT_TEACHER_ENGINE_LIFECYCLE,
    )
    parser.add_argument("--sampler", default=str(DEFAULT_SAMPLER))
    parser.add_argument("--legal-validator", default=str(DEFAULT_LEGAL_VALIDATOR))
    parser.add_argument("--seed", type=parse_non_negative_int, default=DEFAULT_SEED)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--count", type=parse_positive_int)
    group.add_argument("--bucket-spec")
    parser.add_argument("--dry-run", action="store_true")

    args = parser.parse_args(helper_args)
    args.ntest_cmd = ntest_cmd
    return args


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> SmokeConfig:
    raw_bucket_spec = args.bucket_spec or bucket_spec_for_count(args.count or DEFAULT_COUNT)
    bucket_spec = normalize_bucket_spec(raw_bucket_spec)
    return SmokeConfig(
        dataset_root=Path(args.dataset_root).expanduser().resolve(strict=False),
        dataset_id=args.dataset_id,
        ntest_cmd=list(args.ntest_cmd),
        ntest_workdir=Path(args.ntest_workdir).expanduser().resolve(strict=False)
        if args.ntest_workdir
        else None,
        depth=args.depth,
        timeout_ms=args.timeout_ms,
        jobs=args.jobs,
        position_log_mode=args.position_log_mode,
        teacher_engine_lifecycle=args.teacher_engine_lifecycle,
        sampler=Path(args.sampler),
        legal_validator=Path(args.legal_validator),
        seed=args.seed,
        bucket_spec=bucket_spec,
        dry_run=args.dry_run,
        run_dir=default_run_dir(),
        invocation=invocation or [],
    )


def position_pool_relative_path(config: SmokeConfig) -> Path:
    return Path("teacher") / config.dataset_id / "source" / "smoke-positions.txt"


def position_pool_path(config: SmokeConfig) -> Path:
    return (config.dataset_root / position_pool_relative_path(config)).resolve(strict=False)


def dataset_output_relative_path(config: SmokeConfig) -> Path:
    return Path("teacher") / config.dataset_id


def dataset_output_dir(config: SmokeConfig) -> Path:
    return (config.dataset_root / dataset_output_relative_path(config)).resolve(strict=False)


def teacher_name(config: SmokeConfig) -> str:
    return f"ntest-depth{config.depth}-smoke"


def teacher_label_root(config: SmokeConfig) -> Path:
    return dataset_output_dir(config) / "labels" / teacher_name(config)


def build_pool_command(config: SmokeConfig) -> list[str]:
    command = [
        PYTHON,
        str(Path("tools") / "scripts" / "balanced_position_pool.py"),
        "--dataset-root",
        str(config.dataset_root),
        "--output",
        str(position_pool_relative_path(config)),
        "--bucket-spec",
        config.bucket_spec,
        "--seed",
        str(config.seed),
        "--sampler",
        str(config.sampler),
    ]
    if config.dry_run:
        command.append("--dry-run")
    return command


def build_teacher_command(config: SmokeConfig) -> list[str]:
    command = [
        PYTHON,
        str(Path("tools") / "scripts" / "teacher_dataset_build.py"),
        "--dataset-id",
        config.dataset_id,
        "--dataset-root",
        str(config.dataset_root),
        "--out",
        str(dataset_output_relative_path(config)),
        "--positions",
        str(position_pool_path(config)),
        "--split-seed",
        str(config.seed),
        "--shard-size",
        str(max(1, min(1000, smoke_position_count(config)))),
        "--teacher-adapter",
        "ntest",
        "--teacher-protocol",
        "nboard",
        "--teacher-depth",
        str(config.depth),
        "--teacher-timeout-ms",
        str(config.timeout_ms),
        "--label-jobs",
        str(config.jobs),
        "--position-log-mode",
        config.position_log_mode,
        "--teacher-engine-lifecycle",
        config.teacher_engine_lifecycle,
        "--teacher-engine-name",
        teacher_name(config),
        "--legal-validator",
        str(config.legal_validator),
        "--allow-failures",
    ]
    if config.ntest_workdir is not None:
        command.extend(["--teacher-workdir", str(config.ntest_workdir)])
    if config.dry_run:
        command.append("--dry-run")
    command.extend(["--teacher-engine-cmd", "--", *config.ntest_cmd])
    return command


def smoke_position_count(config: SmokeConfig) -> int:
    return sum(bucket.count for bucket in position_pool.parse_bucket_spec(config.bucket_spec))


def redact_token(token: str) -> str:
    key, separator, value = token.partition("=")
    if separator and Path(value).is_absolute():
        return f"{key}=<absolute-path:{Path(value).name}>"
    path = Path(token)
    if path.is_absolute():
        return f"<absolute-path:{path.name}>"
    return token


def redact_command(command: Sequence[str]) -> list[str]:
    return [redact_token(part) for part in command]


def run_command(command: Sequence[str], *, runner: Runner, log_path: Path) -> subprocess.CompletedProcess[str]:
    completed = runner(
        list(command),
        check=False,
        capture_output=True,
        text=True,
        cwd=REPO_ROOT,
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        f"command: {quote_command(redact_command(command))}\n"
        f"exit_code: {completed.returncode}\n\n"
        f"stdout:\n{completed.stdout}\n"
        f"stderr:\n{completed.stderr}\n",
        encoding="utf-8",
    )
    return completed


def pool_manifest_matches(config: SmokeConfig) -> bool:
    path = position_pool_path(config)
    manifest_path = path.parent / "manifest.json"
    if not path.is_file() or not manifest_path.is_file():
        return False
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    counts = manifest.get("counts")
    if not isinstance(counts, dict):
        return False
    return (
        manifest.get("bucket_spec") == config.bucket_spec
        and manifest.get("seed") == config.seed
        and counts.get("generated_unique_positions") == smoke_position_count(config)
    )


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as input_file:
        for line in input_file:
            if line.strip():
                value = json.loads(line)
                if isinstance(value, dict):
                    rows.append(value)
    return rows


def read_label_rows(config: SmokeConfig) -> list[dict[str, Any]]:
    shard_dir = teacher_label_root(config) / "shards"
    rows: list[dict[str, Any]] = []
    if not shard_dir.is_dir():
        return rows
    for path in sorted(shard_dir.glob("labels-*.jsonl")):
        rows.extend(read_jsonl(path))
    return rows


def labels_per_second(requested_labels: int, elapsed_seconds: float) -> float:
    if requested_labels <= 0 or elapsed_seconds <= 0:
        return 0.0
    return requested_labels / elapsed_seconds


def estimate_seconds_for_labels(labels: int, rate: float) -> float | None:
    if rate <= 0:
        return None
    return labels / rate


def recommend_action(
    *,
    requested_labels: int,
    failed_labels: int,
    timed_out_labels: int,
    illegal_teacher_moves: int,
    estimated_300k_seconds: float | None,
    jobs: int,
    teacher_exit_code: int,
) -> str:
    if teacher_exit_code != 0:
        return ACTION_INSPECT_FAILURES
    if requested_labels <= 0 or estimated_300k_seconds is None:
        return ACTION_INSPECT_FAILURES
    if illegal_teacher_moves > 0:
        return ACTION_INSPECT_FAILURES

    timeout_rate = timed_out_labels / requested_labels
    if timeout_rate >= 0.20 and jobs > 1:
        return ACTION_REDUCE_JOBS
    if timed_out_labels > 0:
        return ACTION_INCREASE_TIMEOUT
    if failed_labels > 0:
        return ACTION_INSPECT_FAILURES
    if estimated_300k_seconds > DEFAULT_OVERNIGHT_SECONDS:
        return ACTION_REDUCE_DEPTH
    return ACTION_OK


def format_duration(seconds: float | None) -> str:
    if seconds is None:
        return "unavailable"
    total = int(round(seconds))
    hours, remainder = divmod(total, 3600)
    minutes, secs = divmod(remainder, 60)
    if hours:
        return f"{hours}h {minutes}m {secs}s"
    if minutes:
        return f"{minutes}m {secs}s"
    return f"{secs}s"


def summarize_smoke(
    config: SmokeConfig,
    *,
    rows: Sequence[dict[str, Any]],
    elapsed_seconds: float,
    teacher_exit_code: int,
    position_pool_status: str,
) -> SmokeResult:
    requested = len(rows)
    ok = sum(1 for row in rows if row.get("status") == "ok")
    failed = sum(1 for row in rows if row.get("status") == "failed")
    timed_out = sum(1 for row in rows if row.get("timed_out") is True)
    illegal = sum(1 for row in rows if row.get("legal_move_valid") is False)
    rate = labels_per_second(requested, elapsed_seconds)
    estimate = estimate_seconds_for_labels(DEFAULT_TARGET_LABELS, rate)
    action = recommend_action(
        requested_labels=requested,
        failed_labels=failed,
        timed_out_labels=timed_out,
        illegal_teacher_moves=illegal,
        estimated_300k_seconds=estimate,
        jobs=config.jobs,
        teacher_exit_code=teacher_exit_code,
    )
    return SmokeResult(
        requested_labels=requested,
        ok_labels=ok,
        failed_labels=failed,
        timed_out_labels=timed_out,
        illegal_teacher_moves=illegal,
        elapsed_seconds=elapsed_seconds,
        labels_per_second=rate,
        estimated_300k_seconds=estimate,
        recommended_action=action,
        teacher_exit_code=teacher_exit_code,
        position_pool_status=position_pool_status,
    )


def write_commands(config: SmokeConfig, *, pool_command: Sequence[str], teacher_command: Sequence[str]) -> None:
    config.run_dir.mkdir(parents=True, exist_ok=True)
    full_dataset_id = f"ntest-depth{config.depth}-300k"
    full_positions = Path("teacher") / full_dataset_id / "source" / "positions.txt"
    full_output = Path("teacher") / full_dataset_id
    full_pool = list(pool_command)
    full_pool[full_pool.index("--output") + 1] = str(full_positions)
    full_pool[full_pool.index("--bucket-spec") + 1] = "default-300k"
    full_teacher = list(teacher_command)
    full_teacher[full_teacher.index("--dataset-id") + 1] = full_dataset_id
    full_teacher[full_teacher.index("--out") + 1] = str(full_output)
    full_teacher[full_teacher.index("--positions") + 1] = str(
        (config.dataset_root / full_positions).resolve(strict=False)
    )
    lines = [
        "#!/usr/bin/env sh",
        "# Re-run this smoke or adapt it for a full 300K run.",
        "# Local absolute paths are redacted.",
        "",
        "# Smoke helper command:",
        quote_command(redact_command(config.invocation)) if config.invocation else "unknown",
        "",
        "# Smoke position-pool command:",
        quote_command(redact_command(pool_command)),
        "",
        "# Smoke teacher dataset command:",
        quote_command(redact_command(teacher_command)),
        "",
        "# Full 300K position-pool adaptation:",
        quote_command(redact_command(full_pool)),
        "",
        "# Full 300K teacher dataset adaptation:",
        "# Change --dataset-id if your full-run dataset should use a different name.",
        quote_command(redact_command(full_teacher)),
        "",
        "# Keep --teacher-depth explicit, and choose --label-jobs only after smoke results.",
        "",
    ]
    path = config.run_dir / "commands.sh"
    path.write_text("\n".join(lines), encoding="utf-8")
    path.chmod(0o755)


def write_report(
    config: SmokeConfig,
    result: SmokeResult,
    *,
    pool_command: Sequence[str],
    teacher_command: Sequence[str],
) -> None:
    config.run_dir.mkdir(parents=True, exist_ok=True)
    lines = [
        "# NTest Teacher Smoke Report",
        "",
        "No strength claim. This smoke only checks whether a full overnight teacher-label run is operationally safe.",
        "",
        "## Summary",
        "",
        f"- dataset_id: `{config.dataset_id}`",
        f"- depth: `{config.depth}`",
        f"- timeout_ms: `{config.timeout_ms}`",
        f"- jobs: `{config.jobs}`",
        f"- position_log_mode: `{config.position_log_mode}`",
        f"- teacher_engine_lifecycle: `{config.teacher_engine_lifecycle}`",
        f"- position_pool_status: `{result.position_pool_status}`",
        f"- teacher_exit_code: `{result.teacher_exit_code}`",
        f"- recommended_action: `{result.recommended_action}`",
        "",
        "## Throughput",
        "",
        f"- requested_labels: `{result.requested_labels}`",
        f"- ok_labels: `{result.ok_labels}`",
        f"- failed_labels: `{result.failed_labels}`",
        f"- timed_out_labels: `{result.timed_out_labels}`",
        f"- illegal_teacher_moves: `{result.illegal_teacher_moves}`",
        f"- elapsed_wall_time: `{format_duration(result.elapsed_seconds)}`",
        f"- labels_per_second: `{result.labels_per_second:.4f}`",
        f"- estimated_time_for_300k: `{format_duration(result.estimated_300k_seconds)}`",
        "",
        "## Artifacts",
        "",
        f"- positions: `{redact_token(str(position_pool_path(config)))}`",
        f"- dataset_output: `{redact_token(str(dataset_output_dir(config)))}`",
        f"- commands: `{redact_token(str(config.run_dir / 'commands.sh'))}`",
        "",
        "## Commands",
        "",
        "```sh",
        quote_command(redact_command(pool_command)),
        quote_command(redact_command(teacher_command)),
        "```",
        "",
        "## Notes",
        "",
        "- Generated smoke outputs are local artifacts and should not be committed.",
        "- This helper does not vendor NTest and does not change the C++ engine core.",
        "- If the recommendation is not `OK for overnight`, inspect label failures and adjust depth, timeout, or jobs before a 300K run.",
        "",
    ]
    (config.run_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")


def run_smoke(
    config: SmokeConfig,
    *,
    runner: Runner = subprocess.run,
    clock: Clock = time.monotonic,
) -> int:
    config.run_dir.mkdir(parents=True, exist_ok=True)
    pool_command = build_pool_command(config)
    teacher_command = build_teacher_command(config)

    if config.dry_run:
        result = summarize_smoke(
            config,
            rows=[],
            elapsed_seconds=0.0,
            teacher_exit_code=0,
            position_pool_status="planned",
        )
        write_commands(config, pool_command=pool_command, teacher_command=teacher_command)
        write_report(config, result, pool_command=pool_command, teacher_command=teacher_command)
        print(f"report: {config.run_dir / 'report.md'}")
        return 0

    position_pool_status = "reused" if pool_manifest_matches(config) else "generated"
    if position_pool_status == "generated":
        completed = run_command(
            pool_command,
            runner=runner,
            log_path=config.run_dir / "logs" / "position-pool.log",
        )
        if completed.returncode != 0:
            raise ScriptError(f"balanced position pool failed with exit code {completed.returncode}")

    label_root = teacher_label_root(config)
    if label_root.is_symlink() or label_root.is_file():
        raise ScriptError(f"refusing to clear non-directory teacher label root: {label_root}")
    if label_root.is_dir():
        shutil.rmtree(label_root)

    start = clock()
    teacher_completed = run_command(
        teacher_command,
        runner=runner,
        log_path=config.run_dir / "logs" / "teacher-dataset-build.log",
    )
    elapsed = clock() - start
    rows = read_label_rows(config)
    result = summarize_smoke(
        config,
        rows=rows,
        elapsed_seconds=elapsed,
        teacher_exit_code=int(teacher_completed.returncode),
        position_pool_status=position_pool_status,
    )
    write_commands(config, pool_command=pool_command, teacher_command=teacher_command)
    write_report(config, result, pool_command=pool_command, teacher_command=teacher_command)
    print(f"report: {config.run_dir / 'report.md'}")
    return int(teacher_completed.returncode)


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_smoke(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
