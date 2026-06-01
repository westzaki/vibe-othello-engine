#!/usr/bin/env python3
"""Generate local teacher-label JSONL with an external Othello engine."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import subprocess
import sys
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from common import ScriptError, quote_command
from dataset_paths import resolve_path_reference
from external_engines.common import EngineMoveResult, ExternalEngineError
from external_engines.ntest import NTestConfig, request_best_move as request_ntest_best_move
from external_engines.one_shot import request_best_move as request_one_shot_best_move


SCHEMA_NAME = "teacher_label.v1"
DEFAULT_TIMEOUT_MS = 1000
DEFAULT_NTEST_DEPTH = 26
DEFAULT_LEGAL_VALIDATOR = Path("build") / "othello_validate_move"
POSITION_LOG_MODES = ("all", "failures", "none")

_BOARD_ROW_PATTERN = re.compile(r"^[.BW]{8}$")
_MOVE_PATTERN = re.compile(r"^[a-h][1-8]$", re.IGNORECASE)
_KNOWN_METADATA_KEYS = {"name", "phase", "tags", "note"}


@dataclass(frozen=True)
class InputPosition:
    position_index: int
    source_line: int
    input_format: str
    metadata: dict[str, str]
    board_text: str | None
    external_input_text: str

    @property
    def name(self) -> str | None:
        value = self.metadata.get("name")
        return value if value else None


@dataclass(frozen=True)
class WorkflowConfig:
    positions_path: Path
    out_dir: Path
    input_format: str
    limit: int | None
    engine_name: str
    adapter: str
    protocol: str
    depth: int | None
    timeout_ms: int
    workdir: str | None
    env: dict[str, str]
    engine_command: list[str]
    legal_validator: Path | None
    dry_run: bool
    allow_failures: bool
    jobs: int = 1
    position_log_mode: str = "all"
    invocation: list[str] = field(default_factory=list)
    report_workdir: str | None = None
    report_engine_command: list[str] | None = None

    @property
    def labels_path(self) -> Path:
        return self.out_dir / "labels.jsonl"

    @property
    def report_path(self) -> Path:
        return self.out_dir / "workflow.md"

    @property
    def logs_dir(self) -> Path:
        return self.out_dir / "logs"


@dataclass(frozen=True)
class Metadata:
    generated_at: str
    git_sha: str


@dataclass(frozen=True)
class RunSummary:
    total_input_positions: int
    requested: int
    ok: int
    failed: int
    skipped: int
    timed_out: int
    invalid_move_token: int
    illegal_move: int
    legal_validation_failed: int
    limit_skipped: int


@dataclass(frozen=True)
class LegalValidationResult:
    valid: bool | None
    source: str
    legal_moves: list[str]
    error: str | None = None
    command: list[str] = field(default_factory=list)
    exit_code: int | None = None
    stdout: str = ""
    stderr: str = ""


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


def default_out_dir() -> Path:
    timestamp = dt.datetime.now(dt.UTC).strftime("%Y%m%dT%H%M%SZ")
    return Path("runs") / "teacher-labels" / timestamp


def _parse_env(values: list[str]) -> dict[str, str]:
    environment: dict[str, str] = {}
    for value in values:
        key, separator, env_value = value.partition("=")
        if not separator or key == "":
            raise ScriptError(f"--env must be KEY=VALUE, got: {value}")
        environment[key] = env_value
    return environment


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    values = list(sys.argv[1:] if argv is None else argv)
    if "--engine-cmd" in values:
        engine_index = values.index("--engine-cmd")
        adapter_args = values[:engine_index]
        engine_args = values[engine_index + 1 :]
    else:
        adapter_args = values
        engine_args = []

    parser = argparse.ArgumentParser(
        description="Generate external-engine teacher labels as local JSONL.",
        usage=(
            "%(prog)s --positions PATH [--out DIR] [--input-format auto|board9|raw|nboard-game] "
            "[--limit N] [--timeout-ms N] [--workdir PATH] [--env KEY=VALUE] "
            "[--jobs N] [--position-log-mode all|failures|none] "
            "[--adapter one-shot|ntest] [--protocol nboard|one-shot] [--depth N] "
            "[--engine-name NAME] [--legal-validator PATH] --engine-cmd -- COMMAND [ARGS...]"
        ),
    )
    parser.add_argument("--positions", required=True, help="input positions file")
    parser.add_argument(
        "--dataset-root",
        help="shared dataset root for dataset: references; overrides VIBE_OTHELLO_DATASET_ROOT",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="workflow output directory (default: runs/teacher-labels/<timestamp>)",
    )
    parser.add_argument(
        "--input-format",
        choices=("auto", "board9", "raw", "nboard-game"),
        default="auto",
        help="position input parser; auto uses board9 when the file looks like a 9-line board file",
    )
    parser.add_argument("--limit", type=parse_non_negative_int, help="maximum positions to request")
    parser.add_argument("--timeout-ms", type=parse_positive_int, default=DEFAULT_TIMEOUT_MS)
    parser.add_argument(
        "--jobs",
        type=parse_positive_int,
        default=1,
        help="maximum concurrent external-engine requests (default: 1)",
    )
    parser.add_argument(
        "--position-log-mode",
        choices=POSITION_LOG_MODES,
        default="all",
        help="per-position log policy: all, failures, or none (default: all)",
    )
    parser.add_argument("--workdir", help="working directory for the engine process")
    parser.add_argument(
        "--adapter",
        choices=("one-shot", "ntest"),
        default="one-shot",
        help="external engine adapter to use",
    )
    parser.add_argument(
        "--protocol",
        choices=("nboard", "one-shot"),
        help="adapter protocol; defaults to one-shot for --adapter one-shot and nboard for --adapter ntest",
    )
    parser.add_argument("--depth", type=parse_positive_int, help="NTest NBoard search depth")
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="environment override for the engine process; repeatable",
    )
    parser.add_argument("--engine-name", help="human label for the teacher engine")
    parser.add_argument(
        "--legal-validator",
        default=str(DEFAULT_LEGAL_VALIDATOR),
        help="C++ move validator path for board9 inputs (default: build/othello_validate_move)",
    )
    parser.add_argument("--dry-run", action="store_true", help="write planned JSONL/report/logs only")
    parser.add_argument(
        "--allow-failures",
        action="store_true",
        help="return zero while recording failed engine requests",
    )

    args = parser.parse_args(adapter_args)
    if "--engine-cmd" not in values:
        raise ScriptError("--engine-cmd is required")
    if not engine_args or engine_args[0] != "--" or len(engine_args) == 1:
        raise ScriptError("--engine-cmd must be followed by '--' and a command")

    if args.adapter == "one-shot":
        if args.protocol not in (None, "one-shot"):
            raise ScriptError("--adapter one-shot only allows --protocol one-shot")
        if args.depth is not None:
            raise ScriptError("--depth is only valid with --adapter ntest --protocol nboard")
        args.protocol = "one-shot"
    else:
        args.protocol = args.protocol or "nboard"
        if args.protocol == "one-shot" and args.depth is not None:
            raise ScriptError("--depth is only valid with --adapter ntest --protocol nboard")
        if args.protocol == "nboard":
            args.depth = DEFAULT_NTEST_DEPTH if args.depth is None else args.depth

    args.engine_command = engine_args[1:]
    args.env_overrides = _parse_env(args.env)
    return args


def config_from_args(args: argparse.Namespace, invocation: list[str] | None = None) -> WorkflowConfig:
    engine_name = args.engine_name
    if not engine_name:
        engine_name = Path(args.engine_command[0]).name if args.engine_command else "unknown"

    return WorkflowConfig(
        positions_path=resolve_path_reference(args.positions, explicit_root=args.dataset_root),
        out_dir=Path(args.out) if args.out else default_out_dir(),
        input_format=args.input_format,
        limit=args.limit,
        engine_name=engine_name,
        adapter=args.adapter,
        protocol=args.protocol,
        depth=args.depth,
        timeout_ms=args.timeout_ms,
        workdir=args.workdir,
        env=args.env_overrides,
        engine_command=list(args.engine_command),
        legal_validator=Path(args.legal_validator) if args.legal_validator else None,
        dry_run=args.dry_run,
        allow_failures=args.allow_failures,
        jobs=args.jobs,
        position_log_mode=args.position_log_mode,
        invocation=invocation or [],
    )


def collect_metadata() -> Metadata:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    git_sha = completed.stdout.strip() if completed.returncode == 0 else "unknown"
    return Metadata(
        generated_at=dt.datetime.now(dt.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
        git_sha=git_sha or "unknown",
    )


def _metadata_from_comment(line: str, *, source_name: str, line_number: int) -> tuple[str, str] | None:
    stripped = line.strip()
    if not stripped.startswith("#"):
        return None
    body = stripped[1:].strip()
    key, separator, value = body.partition(":")
    if not separator:
        return None
    key = key.strip()
    if key not in _KNOWN_METADATA_KEYS:
        return None
    value = value.strip()
    if key == "name" and not value:
        raise ScriptError(f"{source_name}: empty # name: metadata at line {line_number}")
    return key, value


def _apply_metadata_line(
    metadata: dict[str, str], line: str, *, source_name: str, line_number: int
) -> None:
    parsed = _metadata_from_comment(line, source_name=source_name, line_number=line_number)
    if parsed is None:
        return
    key, value = parsed
    metadata[key] = value


def _ignored_line(line: str) -> bool:
    stripped = line.strip()
    return not stripped or stripped.startswith("#")


def _data_lines(text: str) -> list[str]:
    lines: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        lines.append(line.rstrip("\r"))
    return lines


def _validate_board9(block: list[str], *, source_name: str, start_line: int) -> str:
    if len(block) != 9:
        raise ScriptError(
            f"{source_name}: incomplete board block starting at line {start_line}: "
            "expected 9 non-comment lines"
        )
    for offset, row in enumerate(block[:8]):
        if _BOARD_ROW_PATTERN.fullmatch(row) is None:
            raise ScriptError(
                f"{source_name}: invalid board row at line {start_line + offset}: "
                "expected 8 characters from '.', 'B', and 'W'"
            )
    if block[8] not in {"side=B", "side=W"}:
        raise ScriptError(
            f"{source_name}: invalid side line at line {start_line + 8}: expected side=B or side=W"
        )
    return "\n".join(block)


def _check_duplicate_name(
    metadata: dict[str, str],
    seen_names: dict[str, int],
    *,
    source_name: str,
    start_line: int,
) -> None:
    name = metadata.get("name")
    if not name:
        return
    if name in seen_names:
        raise ScriptError(
            f"{source_name}: duplicate # name: {name!r} at line {start_line}; "
            f"first used at line {seen_names[name]}"
        )
    seen_names[name] = start_line


def parse_board9_positions(text: str, *, source_name: str) -> list[InputPosition]:
    positions: list[InputPosition] = []
    block: list[str] = []
    metadata: dict[str, str] = {}
    seen_names: dict[str, int] = {}
    block_start_line = 0

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.rstrip("\r")
        if _ignored_line(line):
            if not block:
                _apply_metadata_line(metadata, line, source_name=source_name, line_number=line_number)
            continue

        if not block:
            block_start_line = line_number
        block.append(line)

        if len(block) != 9:
            continue

        board_text = _validate_board9(block, source_name=source_name, start_line=block_start_line)
        _check_duplicate_name(
            metadata,
            seen_names,
            source_name=source_name,
            start_line=block_start_line,
        )
        positions.append(
            InputPosition(
                position_index=len(positions),
                source_line=block_start_line,
                input_format="board9",
                metadata=dict(metadata),
                board_text=board_text,
                external_input_text=board_text,
            )
        )
        block = []
        metadata = {}

    if block:
        raise ScriptError(
            f"{source_name}: incomplete board block starting at line {block_start_line}: "
            "expected 9 non-comment lines"
        )
    if metadata:
        keys = ", ".join(sorted(metadata))
        raise ScriptError(f"{source_name}: metadata ({keys}) is not followed by a position")
    if not positions:
        raise ScriptError(f"{source_name}: no positions found")
    return positions


def parse_raw_positions(text: str, *, source_name: str, input_format: str) -> list[InputPosition]:
    positions: list[InputPosition] = []
    raw_block: list[str] = []
    metadata: dict[str, str] = {}
    seen_names: dict[str, int] = {}
    block_start_line = 0

    def finish_block() -> None:
        nonlocal raw_block, metadata, block_start_line
        if not raw_block:
            return
        external_input = "\n".join(raw_block).strip()
        if not external_input:
            raw_block = []
            return
        _check_duplicate_name(
            metadata,
            seen_names,
            source_name=source_name,
            start_line=block_start_line,
        )
        positions.append(
            InputPosition(
                position_index=len(positions),
                source_line=block_start_line,
                input_format=input_format,
                metadata=dict(metadata),
                board_text=None,
                external_input_text=external_input,
            )
        )
        raw_block = []
        metadata = {}
        block_start_line = 0

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.rstrip("\r")
        if not line.strip():
            finish_block()
            continue
        if not raw_block and line.strip().startswith("#"):
            _apply_metadata_line(metadata, line, source_name=source_name, line_number=line_number)
            continue
        if not raw_block:
            block_start_line = line_number
        raw_block.append(line)

    finish_block()
    if metadata:
        keys = ", ".join(sorted(metadata))
        raise ScriptError(f"{source_name}: metadata ({keys}) is not followed by a position")
    if not positions:
        raise ScriptError(f"{source_name}: no positions found")
    return positions


def parse_positions(path: Path, input_format: str) -> list[InputPosition]:
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ScriptError(f"failed to read positions file: {path}: {exc}") from exc

    source_name = str(path)
    if input_format == "board9":
        return parse_board9_positions(text, source_name=source_name)
    if input_format in {"raw", "nboard-game"}:
        return parse_raw_positions(text, source_name=source_name, input_format=input_format)

    data_lines = _data_lines(text)
    first_data = data_lines[0] if data_lines else None
    looks_like_board9 = (
        first_data is not None and _BOARD_ROW_PATTERN.fullmatch(first_data) is not None
    ) or any(line in {"side=B", "side=W"} for line in data_lines)
    if looks_like_board9:
        return parse_board9_positions(text, source_name=source_name)
    return parse_raw_positions(text, source_name=source_name, input_format="raw")


def board9_to_ntest_nboard_game(board_text: str) -> str:
    lines = board_text.splitlines()
    if len(lines) != 9:
        raise ScriptError("board9 conversion requires 8 board rows plus side line")
    board_chars: list[str] = []
    for row in lines[:8]:
        if _BOARD_ROW_PATTERN.fullmatch(row) is None:
            raise ScriptError("board9 conversion received an invalid board row")
        for char in row:
            if char == "B":
                board_chars.append("*")
            elif char == "W":
                board_chars.append("O")
            else:
                board_chars.append("-")
    side_line = lines[8]
    if side_line == "side=B":
        side = "*"
    elif side_line == "side=W":
        side = "O"
    else:
        raise ScriptError("board9 conversion received an invalid side line")
    return f"(;GM[Othello]PC[NBoard]PB[teacher]PW[teacher]RE[?]TY[8]BO[8 {''.join(board_chars)} {side}];)"


def adapter_input_for_position(config: WorkflowConfig, position: InputPosition) -> tuple[str, str]:
    if (
        position.input_format == "board9"
        and config.adapter == "ntest"
        and config.protocol == "nboard"
        and position.board_text is not None
    ):
        return board9_to_ntest_nboard_game(position.board_text), "nboard-game"
    return position.external_input_text, position.input_format


def ntest_nboard_move_to_board9(move: str | None) -> str | None:
    if move is None or move == "pass":
        return move
    if _MOVE_PATTERN.fullmatch(move) is None:
        return move
    return f"{move[0].lower()}{9 - int(move[1])}"


def normalize_adapter_move_for_position(
    config: WorkflowConfig,
    position: InputPosition,
    external_input_format: str,
    move: str | None,
) -> str | None:
    if (
        position.board_text is not None
        and position.input_format == "board9"
        and config.adapter == "ntest"
        and config.protocol == "nboard"
        and external_input_format == "nboard-game"
    ):
        return ntest_nboard_move_to_board9(move)
    return move


def request_engine_move(config: WorkflowConfig, input_text: str) -> EngineMoveResult:
    if config.adapter == "ntest":
        return request_ntest_best_move(
            NTestConfig(
                command=config.engine_command,
                timeout_ms=config.timeout_ms,
                workdir=config.workdir,
                env=config.env,
                profile=config.protocol,
                depth=config.depth or DEFAULT_NTEST_DEPTH,
            ),
            input_text,
        )
    return request_one_shot_best_move(
        config.engine_command,
        board_text=input_text,
        timeout_ms=config.timeout_ms,
        workdir=config.workdir,
        env=config.env,
    )


def _parse_validator_output(text: str) -> tuple[bool | None, list[str], str | None]:
    valid: bool | None = None
    legal_moves: list[str] = []
    error: str | None = None
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            continue
        if key == "legal_move_valid":
            if value == "true":
                valid = True
            elif value == "false":
                valid = False
        elif key == "legal_moves":
            legal_moves = [part for part in value.split() if part]
        elif key == "error" and value != "-":
            error = value
    return valid, legal_moves, error


def validate_legal_move(
    config: WorkflowConfig,
    position: InputPosition,
    move: str | None,
) -> LegalValidationResult:
    if move is None:
        return LegalValidationResult(valid=None, source="not-run", legal_moves=[])
    if position.board_text is None:
        return LegalValidationResult(
            valid=None,
            source="unavailable",
            legal_moves=[],
        )
    if config.legal_validator is None:
        return LegalValidationResult(
            valid=None,
            source="unavailable",
            legal_moves=[],
            error="legal validator is not configured",
        )

    command = [str(config.legal_validator), "--stdin", "--move", move]
    try:
        completed = subprocess.run(
            command,
            input=position.board_text,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        return LegalValidationResult(
            valid=None,
            source=str(config.legal_validator),
            legal_moves=[],
            error=f"failed to run legal validator: {exc}",
            command=command,
        )

    valid, legal_moves, output_error = _parse_validator_output(completed.stdout)
    if valid is None:
        return LegalValidationResult(
            valid=None,
            source=str(config.legal_validator),
            legal_moves=legal_moves,
            error="legal validator produced no parseable result",
            command=command,
            exit_code=completed.returncode,
            stdout=completed.stdout,
            stderr=completed.stderr,
        )
    return LegalValidationResult(
        valid=valid,
        source=str(config.legal_validator),
        legal_moves=legal_moves,
        error=output_error,
        command=command,
        exit_code=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
    )


def _move_token_valid(result: EngineMoveResult) -> bool | None:
    if result.move is not None:
        return True
    if result.timed_out or result.exit_code != 0:
        return None
    return False


def _result_error(result: EngineMoveResult) -> str | None:
    if result.error:
        return result.error
    if result.timed_out:
        return "engine timed out"
    if result.exit_code != 0:
        return "engine exited non-zero"
    if result.move is None:
        return "engine produced no valid move"
    return None


def _row_error(result: EngineMoveResult, validation: LegalValidationResult) -> str | None:
    result_error = _result_error(result)
    if result_error is not None:
        return result_error
    if validation.valid is False:
        return validation.error or "illegal move according to rule-core validator"
    if validation.valid is None and validation.error is not None:
        return validation.error
    return None


def _row_status(result: EngineMoveResult, validation: LegalValidationResult) -> str:
    if result.move is None or result.exit_code != 0 or result.timed_out:
        return "failed"
    if validation.valid is False:
        return "failed"
    if validation.valid is None and validation.error is not None:
        return "failed"
    return "ok"


def should_write_position_log(config: WorkflowConfig, status: str) -> bool:
    if config.position_log_mode == "all":
        return True
    if config.position_log_mode == "failures":
        return status == "failed"
    return False


def position_log_path(config: WorkflowConfig, position: InputPosition, status: str) -> Path | None:
    if not should_write_position_log(config, status):
        return None
    return config.logs_dir / f"position-{position.position_index:06d}.log"


def write_position_log(
    path: Path,
    *,
    config: WorkflowConfig,
    position: InputPosition,
    external_input_text: str,
    external_input_format: str,
    status: str,
    result: EngineMoveResult | None = None,
    normalized_move: str | None = None,
    validation: LegalValidationResult | None = None,
    error: str | None = None,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"schema: {SCHEMA_NAME}",
        f"position_index: {position.position_index}",
        f"position_name: {position.name or '-'}",
        f"source_line: {position.source_line}",
        f"status: {status}",
        f"engine_name: {config.engine_name}",
        f"adapter: {config.adapter}",
        f"protocol: {config.protocol}",
        f"depth: {config.depth if config.depth is not None else 'n/a'}",
        f"timeout_ms: {config.timeout_ms}",
        f"command: {quote_command(config.engine_command)}",
        f"input_format: {position.input_format}",
        f"external_input_format: {external_input_format}",
    ]
    if config.workdir is not None:
        lines.append(f"workdir: {config.workdir}")
    if config.env:
        env_text = ", ".join(f"{key}=<set>" for key in sorted(config.env))
        lines.append(f"env: {env_text}")
    if error:
        lines.append(f"error: {error}")
    if result is not None:
        lines.extend(
            [
                f"engine_move: {result.move if result.move is not None else '-'}",
                f"move: {normalized_move if normalized_move is not None else result.move if result.move is not None else '-'}",
                f"elapsed_ms: {result.elapsed_ms:.2f}",
                f"exit_code: {result.exit_code}",
                f"timed_out: {'true' if result.timed_out else 'false'}",
                f"result_error: {_result_error(result) or '-'}",
            ]
        )
    if validation is not None:
        lines.extend(
            [
                f"legal_move_valid: {validation.valid if validation.valid is not None else 'null'}",
                f"legal_validation_source: {validation.source}",
                f"legal_validation_exit_code: {validation.exit_code if validation.exit_code is not None else 'n/a'}",
                f"legal_validation_error: {validation.error or '-'}",
                f"legal_moves: {' '.join(validation.legal_moves) if validation.legal_moves else '-'}",
            ]
        )
        if validation.command:
            lines.append(f"legal_validation_command: {quote_command(validation.command)}")
    lines.extend(
        [
            "",
            "stdin:",
            external_input_text,
            "",
            "stdout:",
            result.raw_output if result is not None else "",
            "",
            "stderr:",
            result.raw_error if result is not None else "",
            "",
        ]
    )
    if validation is not None:
        lines.extend(
            [
                "legal_validation_stdout:",
                validation.stdout,
                "",
                "legal_validation_stderr:",
                validation.stderr,
                "",
            ]
        )
    path.write_text("\n".join(lines), encoding="utf-8")


def dry_run_row(
    config: WorkflowConfig,
    metadata: Metadata,
    position: InputPosition,
    external_input_text: str,
    external_input_format: str,
) -> dict[str, Any]:
    status = "skipped"
    validation = LegalValidationResult(
        valid=None,
        source=str(config.legal_validator) if config.legal_validator is not None else "unavailable",
        legal_moves=[],
        error="dry-run",
    )
    log_path = position_log_path(config, position, status)
    if log_path is not None:
        write_position_log(
            log_path,
            config=config,
            position=position,
            external_input_text=external_input_text,
            external_input_format=external_input_format,
            status=status,
            validation=validation,
            error="dry-run",
        )
    return base_row(
        config,
        metadata,
        position,
        log_path,
        external_input_text,
        external_input_format,
        status=status,
        move=None,
        move_token_valid=None,
        validation=validation,
        elapsed_ms=None,
        exit_code=None,
        timed_out=False,
        error="dry-run",
    )


def base_row(
    config: WorkflowConfig,
    metadata: Metadata,
    position: InputPosition,
    log_path: Path | None,
    external_input_text: str,
    external_input_format: str,
    *,
    status: str,
    move: str | None,
    move_token_valid: bool | None,
    validation: LegalValidationResult,
    engine_move: str | None = None,
    elapsed_ms: float | None,
    exit_code: int | None,
    timed_out: bool,
    error: str | None,
    result: EngineMoveResult | None = None,
) -> dict[str, Any]:
    row: dict[str, Any] = {
        "schema": SCHEMA_NAME,
        "position_index": position.position_index,
        "input_format": position.input_format,
        "external_input_format": external_input_format,
        "engine_name": config.engine_name,
        "adapter": config.adapter,
        "protocol": config.protocol,
        "depth": config.depth,
        "timeout_ms": config.timeout_ms,
        "status": status,
        "move": move,
        "engine_move": engine_move,
        "move_token_valid": move_token_valid,
        "legal_move_valid": validation.valid,
        "legal_validation_source": validation.source,
        "legal_validation_error": validation.error,
        "legal_moves": validation.legal_moves,
        "elapsed_ms": elapsed_ms,
        "exit_code": exit_code,
        "timed_out": timed_out,
        "error": error,
        "log_path": str(log_path) if log_path is not None else None,
        "generated_at": metadata.generated_at,
        "git_sha": metadata.git_sha,
    }
    if log_path is None and status != "ok":
        row.update(
            {
                "engine_stdout": result.raw_output if result is not None else "",
                "engine_stderr": result.raw_error if result is not None else "",
                "legal_validation_command": validation.command,
                "legal_validation_exit_code": validation.exit_code,
                "legal_validation_stdout": validation.stdout,
                "legal_validation_stderr": validation.stderr,
            }
        )
    if position.name is not None:
        row["position_name"] = position.name
    if position.metadata:
        row["position_metadata"] = position.metadata
    if position.board_text is not None:
        row["board_text"] = position.board_text
    row["external_input_text"] = external_input_text
    return row


def run_position(
    config: WorkflowConfig,
    metadata: Metadata,
    position: InputPosition,
) -> dict[str, Any]:
    external_input_text, external_input_format = adapter_input_for_position(config, position)

    if config.dry_run:
        return dry_run_row(
            config,
            metadata,
            position,
            external_input_text,
            external_input_format,
        )

    try:
        result = request_engine_move(config, external_input_text)
    except ExternalEngineError as exc:
        error = str(exc)
        status = "failed"
        validation = LegalValidationResult(valid=None, source="not-run", legal_moves=[])
        log_path = position_log_path(config, position, status)
        if log_path is not None:
            write_position_log(
                log_path,
                config=config,
                position=position,
                external_input_text=external_input_text,
                external_input_format=external_input_format,
                status=status,
                validation=validation,
                error=error,
            )
        return base_row(
            config,
            metadata,
            position,
            log_path,
            external_input_text,
            external_input_format,
            status=status,
            move=None,
            move_token_valid=None,
            validation=validation,
            elapsed_ms=None,
            exit_code=None,
            timed_out=False,
            error=error,
        )

    normalized_move = normalize_adapter_move_for_position(
        config,
        position,
        external_input_format,
        result.move,
    )
    validation = validate_legal_move(config, position, normalized_move)
    status = _row_status(result, validation)
    error = _row_error(result, validation)
    log_path = position_log_path(config, position, status)
    if log_path is not None:
        write_position_log(
            log_path,
            config=config,
            position=position,
            external_input_text=external_input_text,
            external_input_format=external_input_format,
            status=status,
            result=result,
            normalized_move=normalized_move,
            validation=validation,
            error=error,
        )
    return base_row(
        config,
        metadata,
        position,
        log_path,
        external_input_text,
        external_input_format,
        status=status,
        move=normalized_move,
        move_token_valid=_move_token_valid(result),
        validation=validation,
        engine_move=result.move if result.move != normalized_move else None,
        elapsed_ms=result.elapsed_ms,
        exit_code=result.exit_code,
        timed_out=result.timed_out,
        error=error,
        result=result,
    )


def summarize_rows(
    *,
    total_input_positions: int,
    limit_skipped: int,
    rows: list[dict[str, Any]],
) -> RunSummary:
    return RunSummary(
        total_input_positions=total_input_positions,
        requested=len(rows),
        ok=sum(1 for row in rows if row["status"] == "ok"),
        failed=sum(1 for row in rows if row["status"] == "failed"),
        skipped=sum(1 for row in rows if row["status"] == "skipped") + limit_skipped,
        timed_out=sum(1 for row in rows if row["timed_out"]),
        invalid_move_token=sum(1 for row in rows if row["move_token_valid"] is False),
        illegal_move=sum(1 for row in rows if row["legal_move_valid"] is False),
        legal_validation_failed=sum(
            1
            for row in rows
            if row["legal_move_valid"] is None
            and row["legal_validation_error"] not in (None, "dry-run")
        ),
        limit_skipped=limit_skipped,
    )


def validate_config(config: WorkflowConfig) -> None:
    if config.jobs <= 0:
        raise ScriptError("--jobs must be a positive integer")
    if config.position_log_mode not in POSITION_LOG_MODES:
        choices = "|".join(POSITION_LOG_MODES)
        raise ScriptError(f"--position-log-mode must be one of {choices}")


def run_positions_concurrently(
    config: WorkflowConfig,
    metadata: Metadata,
    positions: list[InputPosition],
) -> list[dict[str, Any]]:
    rows_by_index: dict[int, dict[str, Any]] = {}
    position_iter = iter(positions)
    max_pending = max(1, config.jobs * 2)

    with ThreadPoolExecutor(max_workers=config.jobs) as executor:
        pending: set[Any] = set()

        def submit_until_full() -> None:
            while len(pending) < max_pending:
                try:
                    position = next(position_iter)
                except StopIteration:
                    return
                pending.add(executor.submit(run_position, config, metadata, position))

        submit_until_full()
        while pending:
            done, _ = wait(pending, return_when=FIRST_COMPLETED)
            for future in done:
                pending.remove(future)
                row = future.result()
                rows_by_index[int(row["position_index"])] = row
            submit_until_full()

    return [rows_by_index[position.position_index] for position in positions]


def workflow_status(config: WorkflowConfig, summary: RunSummary) -> str:
    if config.dry_run:
        return "dry run"
    if summary.failed:
        return "completed with failures"
    return "completed"


def render_report(config: WorkflowConfig, metadata: Metadata, summary: RunSummary) -> str:
    report_engine_command = config.report_engine_command or config.engine_command
    report_workdir = config.report_workdir if config.report_workdir is not None else config.workdir
    command = quote_command(report_engine_command)
    invocation = quote_command(config.invocation) if config.invocation else "unknown"
    workdir = report_workdir if report_workdir is not None else "n/a"
    ntest_board9_support = (
        "supported by serializing board9 positions to NBoard BO[8 ...] text"
        if config.adapter == "ntest" and config.protocol == "nboard"
        else "board9 is passed directly to the selected adapter unless raw input is requested"
    )
    depth_text = str(config.depth) if config.depth is not None else "n/a"
    lines = [
        "# External Teacher Label Workflow",
        "",
        f"Status: {workflow_status(config, summary)}",
        "",
        "No strength claim. No Elo claim. No default promotion.",
        "",
        "## Metadata",
        "",
        f"- generated_at: `{metadata.generated_at}`",
        f"- git_sha: `{metadata.git_sha}`",
        f"- invocation: `{invocation}`",
        f"- input_positions_path: `{config.positions_path}`",
        f"- output_labels_path: `{config.labels_path}`",
        f"- logs_path: `{config.logs_dir}`",
        f"- engine_name: `{config.engine_name}`",
        f"- adapter: `{config.adapter}`",
        f"- protocol: `{config.protocol}`",
        f"- depth: `{depth_text}`",
        f"- timeout_ms: `{config.timeout_ms}`",
        f"- jobs: `{config.jobs}`",
        f"- position_log_mode: `{config.position_log_mode}`",
        f"- allow_failures: `{config.allow_failures}`",
        f"- workdir: `{workdir}`",
        f"- input_format: `{config.input_format}`",
        f"- legal_validator: `{config.legal_validator if config.legal_validator is not None else 'n/a'}`",
        f"- command_template: `{command}`",
        "",
        "## Counts",
        "",
        f"- total_input_positions: `{summary.total_input_positions}`",
        f"- requested: `{summary.requested}`",
        f"- ok: `{summary.ok}`",
        f"- failed: `{summary.failed}`",
        f"- skipped: `{summary.skipped}`",
        f"- timed_out: `{summary.timed_out}`",
        f"- invalid_move_token: `{summary.invalid_move_token}`",
        f"- illegal_move: `{summary.illegal_move}`",
        f"- legal_validation_failed: `{summary.legal_validation_failed}`",
    ]
    if summary.limit_skipped:
        lines.append(f"- skipped_by_limit: `{summary.limit_skipped}`")

    lines.extend(
        [
            "",
            "## Input Support",
            "",
            "- Existing 9-line board input is supported.",
            f"- NTest NBoard board9 handling: {ntest_board9_support}.",
            "- Raw external-engine text blocks are supported with `--input-format raw` or `--input-format nboard-game`.",
            "",
            "## Caveats",
            "",
            "- Teacher labels are reference-engine evidence only, not exact truth.",
            "- 9-line board labels are validated with the configured C++ rule-core validator.",
            "- Raw external-input labels cannot be legally validated unless a board9 position is also available.",
            "- Move-token validation only checks adapter-level parseability such as `a1` through `h8` or `pass`.",
            "- Real NTest is optional; fake-engine tests cover CI process handling and parser behavior.",
            "- Generated labels are local artifacts and should not be committed.",
            "- Raw subprocess logs are local artifacts and should not be committed.",
            "- Keep raw workflow output under `runs/` for normal use.",
            "- This workflow does not promote any evaluator default.",
            "- Next action: use labels for move-rank analysis, mistake mining, or pattern training.",
        ]
    )
    return "\n".join(lines) + "\n"


def run_workflow(config: WorkflowConfig) -> int:
    validate_config(config)
    config.out_dir.mkdir(parents=True, exist_ok=True)
    metadata = collect_metadata()
    positions = parse_positions(config.positions_path, config.input_format)
    requested_positions = positions
    if config.limit is not None:
        requested_positions = positions[: config.limit]
    limit_skipped = len(positions) - len(requested_positions)

    rows: list[dict[str, Any]] = []
    config.labels_path.parent.mkdir(parents=True, exist_ok=True)
    with config.labels_path.open("w", encoding="utf-8") as labels_file:
        if config.jobs == 1:
            for position in requested_positions:
                row = run_position(config, metadata, position)
                rows.append(row)
                labels_file.write(json.dumps(row, sort_keys=True) + "\n")
        else:
            rows = run_positions_concurrently(config, metadata, requested_positions)
            for row in rows:
                labels_file.write(json.dumps(row, sort_keys=True) + "\n")

    summary = summarize_rows(
        total_input_positions=len(positions),
        limit_skipped=limit_skipped,
        rows=rows,
    )
    config.report_path.write_text(render_report(config, metadata, summary), encoding="utf-8")

    if summary.failed and not config.allow_failures:
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
        return run_workflow(config)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
