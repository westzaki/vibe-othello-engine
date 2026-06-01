#!/usr/bin/env python3
"""Build a balanced board9 position pool with the C++ position sampler."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from common import ScriptError, quote_command


SCHEMA_MANIFEST = "balanced_position_pool_manifest.v1"
DEFAULT_OUTPUT = Path("runs") / "position-pools" / "balanced-board9" / "positions.txt"
DEFAULT_SAMPLER = Path("build") / "othello_position_sampler"
REPO_ROOT = Path(__file__).resolve().parents[2]
WORK_DIR_NAME = ".balanced-position-pool-work"
MAX_BUCKET_ROUNDS = 16
MAX_SAFE_SAMPLER_SEED = (1 << 63) - 1

DEFAULT_300K_BUCKETS: tuple[tuple[int, int], ...] = (
    (52, 4000),
    (51, 4000),
    (48, 5000),
    (47, 5000),
    (44, 7000),
    (43, 7000),
    (40, 10000),
    (39, 10000),
    (36, 14000),
    (35, 14000),
    (32, 17000),
    (31, 17000),
    (28, 18000),
    (27, 18000),
    (24, 18000),
    (23, 18000),
    (20, 16000),
    (19, 16000),
    (16, 14000),
    (15, 14000),
    (14, 11000),
    (13, 11000),
    (12, 9000),
    (11, 9000),
    (10, 4500),
    (9, 4500),
    (8, 2500),
    (7, 2500),
)

SMOKE_BUCKETS: tuple[tuple[int, int], ...] = (
    (52, 50),
    (48, 75),
    (44, 100),
    (40, 125),
    (36, 150),
    (32, 150),
    (28, 150),
    (24, 125),
    (20, 100),
    (16, 75),
    (12, 60),
    (8, 40),
)

NAMED_BUCKET_SPECS = {
    "default-300k": DEFAULT_300K_BUCKETS,
    "smoke": SMOKE_BUCKETS,
}


@dataclass(frozen=True)
class Bucket:
    empties: int
    count: int


@dataclass(frozen=True)
class BoardRecord:
    board_text: str
    empties: int
    bucket_empties: int
    generated_index: int

    @property
    def position_id(self) -> str:
        digest = hashlib.sha256(self.board_text.encode("utf-8")).hexdigest()
        return f"sha256:{digest}"


@dataclass(frozen=True)
class DuplicateRecord:
    position_id: str
    first_bucket: int
    first_generated_index: int
    duplicate_bucket: int
    duplicate_generated_index: int


@dataclass(frozen=True)
class BucketSummary:
    empties: int
    requested: int
    generated_unique: int
    duplicate_candidates: int
    sampler_invocations: int
    sampler_commands: list[list[str]]


@dataclass(frozen=True)
class PoolConfig:
    output_path: Path
    buckets: tuple[Bucket, ...]
    seed: int
    sampler: Path
    max_attempts_per_bucket: int | None
    dry_run: bool
    invocation: list[str] = field(default_factory=list)


Runner = Callable[..., subprocess.CompletedProcess[str]]


def parse_non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a non-negative integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be a non-negative integer")
    return parsed


def parse_positive_int(value: str) -> int:
    parsed = parse_non_negative_int(value)
    if parsed == 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return parsed


def parse_bucket_spec(value: str) -> tuple[Bucket, ...]:
    named = NAMED_BUCKET_SPECS.get(value.strip())
    if named is not None:
        return tuple(Bucket(empties, count) for empties, count in named)

    tokens = [token for token in value.replace(",", " ").split() if token]
    if not tokens:
        raise ScriptError("--bucket-spec must not be empty")

    buckets: list[Bucket] = []
    seen: set[int] = set()
    for token in tokens:
        empties_text, separator, count_text = token.partition(":")
        if not separator:
            raise ScriptError(f"invalid bucket spec segment: {token}")
        try:
            empties = int(empties_text)
            count = int(count_text)
        except ValueError as exc:
            raise ScriptError(f"invalid bucket spec segment: {token}") from exc
        if empties < 0 or empties > 64:
            raise ScriptError(f"bucket empty-count must be in [0, 64]: {empties}")
        if count <= 0:
            raise ScriptError(f"bucket count must be positive: {token}")
        if empties in seen:
            raise ScriptError(f"duplicate empty-count in bucket spec: {empties}")
        seen.add(empties)
        buckets.append(Bucket(empties=empties, count=count))

    return tuple(buckets)


def bucket_spec_text(buckets: Sequence[Bucket]) -> str:
    return ",".join(f"{bucket.empties}:{bucket.count}" for bucket in buckets)


def resolve_output_path(output: str | None, dataset_root: str | None) -> Path:
    if output is None:
        return DEFAULT_OUTPUT.resolve(strict=False)

    path = Path(output).expanduser()
    if dataset_root is not None and not path.is_absolute():
        path = Path(dataset_root).expanduser() / path
    return path.resolve(strict=False)


def reject_source_controlled_output(path: Path) -> None:
    data_dir = (REPO_ROOT / "data").resolve(strict=False)
    try:
        path.resolve(strict=False).relative_to(data_dir)
    except ValueError:
        return
    raise ScriptError("balanced position pool output must not be under source-controlled data/")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a balanced board9 position pool with othello_position_sampler."
    )
    parser.add_argument(
        "--output",
        help=f"board9 positions output path (default: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--dataset-root",
        help="resolve a relative --output below this external dataset root",
    )
    parser.add_argument(
        "--bucket-spec",
        default="smoke",
        help="named spec (smoke, default-300k) or comma/space-separated EMPTY:COUNT pairs",
    )
    parser.add_argument("--seed", type=parse_non_negative_int, required=True)
    parser.add_argument(
        "--sampler",
        default=str(DEFAULT_SAMPLER),
        help="path to othello_position_sampler",
    )
    parser.add_argument(
        "--max-attempts-per-bucket",
        type=parse_positive_int,
        help="sampler --max-attempts value for each bucket request",
    )
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> PoolConfig:
    output_path = resolve_output_path(args.output, args.dataset_root)
    reject_source_controlled_output(output_path)
    return PoolConfig(
        output_path=output_path,
        buckets=parse_bucket_spec(args.bucket_spec),
        seed=args.seed,
        sampler=Path(args.sampler),
        max_attempts_per_bucket=args.max_attempts_per_bucket,
        dry_run=args.dry_run,
        invocation=invocation or [],
    )


def normalize_board9_text(board_text: str) -> str:
    lines = [
        line.strip()
        for line in board_text.splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]
    if len(lines) != 9:
        raise ScriptError("board9 text must contain 8 board rows plus side line")
    for row in lines[:8]:
        if len(row) != 8 or any(char not in ".BW" for char in row):
            raise ScriptError("board9 rows must be 8 characters from '.', 'B', and 'W'")
    if lines[8] not in {"side=B", "side=W"}:
        raise ScriptError("board9 side line must be side=B or side=W")
    return "\n".join(lines)


def parse_board9_positions(text: str, *, bucket_empties: int, source_name: str) -> list[BoardRecord]:
    records: list[BoardRecord] = []
    block: list[str] = []
    block_start_line = 0

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if not block:
            block_start_line = line_number
        block.append(line)
        if len(block) < 9:
            continue

        board_text = normalize_board9_text("\n".join(block))
        records.append(
            BoardRecord(
                board_text=board_text,
                empties=board_text.count("."),
                bucket_empties=bucket_empties,
                generated_index=len(records),
            )
        )
        block = []

    if block:
        raise ScriptError(
            f"{source_name}: incomplete board block starting at line {block_start_line}: "
            "expected 9 non-comment lines"
        )
    return records


def derived_seed(seed: int, bucket: Bucket, bucket_index: int, round_index: int) -> int:
    material = f"{seed}:{bucket.empties}:{bucket.count}:{bucket_index}:{round_index}"
    raw_seed = int(hashlib.sha256(material.encode("utf-8")).hexdigest()[:16], 16)
    return (raw_seed % MAX_SAFE_SAMPLER_SEED) + 1


def sampler_command(
    config: PoolConfig,
    *,
    bucket: Bucket,
    output_path: Path,
    count: int,
    seed: int,
) -> list[str]:
    command = [
        str(config.sampler),
        "--output",
        str(output_path),
        "--count",
        str(count),
        "--target-empties",
        str(bucket.empties),
        "--seed",
        str(seed),
        "--unique",
        "true",
        "--allow-terminal",
        "false",
    ]
    if config.max_attempts_per_bucket is not None:
        command.extend(["--max-attempts", str(config.max_attempts_per_bucket)])
    return command


def run_sampler(command: Sequence[str], runner: Runner) -> None:
    completed = runner(
        list(command),
        check=False,
        capture_output=True,
        text=True,
    )
    if int(completed.returncode) == 0:
        return
    stderr = completed.stderr.strip()
    stdout = completed.stdout.strip()
    detail = stderr or stdout or "no output"
    raise ScriptError(f"position sampler failed with exit code {completed.returncode}: {detail}")


def redact_token(token: str) -> str:
    path = Path(token)
    if path.is_absolute():
        return f"<absolute-path:{path.name}>"
    return token


def redact_command(command: Sequence[str]) -> list[str]:
    return [redact_token(part) for part in command]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as input_file:
        for chunk in iter(lambda: input_file.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def collect_git_sha() -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return "unknown"
    return completed.stdout.strip() or "unknown"


def write_positions(path: Path, records: Sequence[BoardRecord]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    body = "\n\n".join(record.board_text for record in records)
    path.write_text(body + ("\n" if body else ""), encoding="utf-8")


def write_commands(path: Path, config: PoolConfig) -> None:
    command = quote_command(redact_command(config.invocation)) if config.invocation else "unknown"
    lines = [
        "#!/usr/bin/env sh",
        "# Recreate or adapt this balanced position-pool build.",
        "# Local absolute paths are redacted.",
        command,
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")
    path.chmod(0o755)


def write_qc_outputs(
    qc_dir: Path,
    bucket_summaries: Sequence[BucketSummary],
    duplicates: Sequence[DuplicateRecord],
) -> None:
    qc_dir.mkdir(parents=True, exist_ok=True)

    phase_lines = [
        "empties\trequested_unique\tgenerated_unique\tduplicate_candidates\tsampler_invocations"
    ]
    for summary in bucket_summaries:
        phase_lines.append(
            "\t".join(
                [
                    str(summary.empties),
                    str(summary.requested),
                    str(summary.generated_unique),
                    str(summary.duplicate_candidates),
                    str(summary.sampler_invocations),
                ]
            )
        )
    (qc_dir / "phase_distribution.tsv").write_text(
        "\n".join(phase_lines) + "\n",
        encoding="utf-8",
    )

    duplicate_lines = [
        "position_id\tfirst_bucket\tfirst_generated_index\tduplicate_bucket\tduplicate_generated_index"
    ]
    for duplicate in duplicates:
        duplicate_lines.append(
            "\t".join(
                [
                    duplicate.position_id,
                    str(duplicate.first_bucket),
                    str(duplicate.first_generated_index),
                    str(duplicate.duplicate_bucket),
                    str(duplicate.duplicate_generated_index),
                ]
            )
        )
    (qc_dir / "duplicate_report.tsv").write_text(
        "\n".join(duplicate_lines) + "\n",
        encoding="utf-8",
    )


def write_manifest(
    path: Path,
    *,
    config: PoolConfig,
    status: str,
    bucket_summaries: Sequence[BucketSummary],
    duplicates: Sequence[DuplicateRecord],
    artifact_paths: Sequence[Path],
) -> None:
    total_requested = sum(summary.requested for summary in bucket_summaries)
    total_generated = sum(summary.generated_unique for summary in bucket_summaries)
    total_duplicates = sum(summary.duplicate_candidates for summary in bucket_summaries)
    manifest: dict[str, Any] = {
        "schema": SCHEMA_MANIFEST,
        "status": status,
        "repo_git_sha": collect_git_sha(),
        "command_line": quote_command(redact_command(config.invocation)) if config.invocation else "unknown",
        "output_path": str(config.output_path),
        "bucket_spec": bucket_spec_text(config.buckets),
        "seed": config.seed,
        "sampler": redact_token(str(config.sampler)),
        "max_attempts_per_bucket": config.max_attempts_per_bucket,
        "dry_run": config.dry_run,
        "counts": {
            "requested_unique_positions": total_requested,
            "generated_unique_positions": total_generated,
            "duplicate_candidates": total_duplicates,
            "duplicate_report_rows": len(duplicates),
            "bucket_count": len(bucket_summaries),
        },
        "buckets": [
            {
                "empties": summary.empties,
                "requested": summary.requested,
                "generated_unique": summary.generated_unique,
                "duplicate_candidates": summary.duplicate_candidates,
                "sampler_invocations": summary.sampler_invocations,
                "sampler_commands": [redact_command(command) for command in summary.sampler_commands],
            }
            for summary in bucket_summaries
        ],
        "per_file_sha256": {
            str(artifact.relative_to(config.output_path.parent)): sha256_file(artifact)
            for artifact in artifact_paths
            if artifact.is_file()
        },
    }
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def build_bucket(
    config: PoolConfig,
    *,
    bucket: Bucket,
    bucket_index: int,
    work_dir: Path,
    seen: dict[str, BoardRecord],
    runner: Runner,
) -> tuple[list[BoardRecord], list[DuplicateRecord], BucketSummary]:
    accepted: list[BoardRecord] = []
    duplicates: list[DuplicateRecord] = []
    commands: list[list[str]] = []
    round_index = 0

    while len(accepted) < bucket.count:
        if round_index >= MAX_BUCKET_ROUNDS:
            raise ScriptError(
                f"unable to collect {bucket.count} unique positions for empties={bucket.empties}; "
                f"collected={len(accepted)} duplicates={len(duplicates)}"
            )

        remaining = bucket.count - len(accepted)
        sample_path = work_dir / f"bucket-{bucket.empties:02d}-round-{round_index:02d}.txt"
        command = sampler_command(
            config,
            bucket=bucket,
            output_path=sample_path,
            count=remaining,
            seed=derived_seed(config.seed, bucket, bucket_index, round_index),
        )
        commands.append(command)

        if config.dry_run:
            break

        run_sampler(command, runner)
        try:
            text = sample_path.read_text(encoding="utf-8")
        except OSError as exc:
            raise ScriptError(f"failed to read sampler output {sample_path}: {exc}") from exc

        candidates = parse_board9_positions(
            text,
            bucket_empties=bucket.empties,
            source_name=str(sample_path),
        )
        if not candidates:
            raise ScriptError(f"sampler produced no positions for empties={bucket.empties}")

        for candidate in candidates:
            if candidate.empties != bucket.empties:
                raise ScriptError(
                    f"sampler output has empties={candidate.empties}, expected {bucket.empties}"
                )
            position_id = candidate.position_id
            first = seen.get(position_id)
            if first is not None:
                duplicates.append(
                    DuplicateRecord(
                        position_id=position_id,
                        first_bucket=first.bucket_empties,
                        first_generated_index=first.generated_index,
                        duplicate_bucket=candidate.bucket_empties,
                        duplicate_generated_index=candidate.generated_index,
                    )
                )
                continue
            seen[position_id] = candidate
            accepted.append(candidate)
            if len(accepted) == bucket.count:
                break

        round_index += 1

    summary = BucketSummary(
        empties=bucket.empties,
        requested=bucket.count,
        generated_unique=len(accepted),
        duplicate_candidates=len(duplicates),
        sampler_invocations=len(commands),
        sampler_commands=commands,
    )
    return accepted, duplicates, summary


def run_pool(config: PoolConfig, *, runner: Runner = subprocess.run) -> int:
    out_dir = config.output_path.parent
    qc_dir = out_dir / "qc"
    manifest_path = out_dir / "manifest.json"
    commands_path = out_dir / "commands.sh"
    work_dir = out_dir / WORK_DIR_NAME
    out_dir.mkdir(parents=True, exist_ok=True)
    work_dir.mkdir(parents=True, exist_ok=True)

    seen: dict[str, BoardRecord] = {}
    records: list[BoardRecord] = []
    duplicates: list[DuplicateRecord] = []
    bucket_summaries: list[BucketSummary] = []
    status = "dry-run" if config.dry_run else "completed"

    try:
        for bucket_index, bucket in enumerate(config.buckets):
            bucket_records, bucket_duplicates, summary = build_bucket(
                config,
                bucket=bucket,
                bucket_index=bucket_index,
                work_dir=work_dir,
                seen=seen,
                runner=runner,
            )
            records.extend(bucket_records)
            duplicates.extend(bucket_duplicates)
            bucket_summaries.append(summary)

        write_positions(config.output_path, records)
        write_qc_outputs(qc_dir, bucket_summaries, duplicates)
        write_commands(commands_path, config)
        artifact_paths = [
            config.output_path,
            qc_dir / "phase_distribution.tsv",
            qc_dir / "duplicate_report.tsv",
            commands_path,
        ]
        write_manifest(
            manifest_path,
            config=config,
            status=status,
            bucket_summaries=bucket_summaries,
            duplicates=duplicates,
            artifact_paths=artifact_paths,
        )
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)

    if not config.dry_run:
        requested = sum(bucket.count for bucket in config.buckets)
        if len(records) != requested:
            raise ScriptError(f"generated {len(records)} unique positions; expected {requested}")
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        invocation = [Path(sys.argv[0]).name, *(argv if argv is not None else sys.argv[1:])]
        config = config_from_args(args, invocation=invocation)
        return run_pool(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
