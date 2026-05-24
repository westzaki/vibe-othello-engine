#!/usr/bin/env python3
"""Run a JSON-defined matrix of othello_match_runner experiments."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from common import ScriptError, quote_command, run_command


@dataclass(frozen=True)
class MatrixExperiment:
    name: str
    runner: str
    summary_script: str
    black: str
    white: str
    games: int
    swap_sides: bool
    seed: int
    output: str
    openings: str | None = None
    summary: bool = False
    by_opening: bool = False
    allow_errors: bool = False


def _load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as input_file:
            return json.load(input_file)
    except json.JSONDecodeError as exc:
        raise ScriptError(f"invalid JSON config: {path}: {exc.msg}") from exc
    except OSError as exc:
        raise ScriptError(f"failed to open config: {path}: {exc}") from exc


def _require_mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ScriptError(f"{label} must be an object")
    return value


def _require_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or value == "":
        raise ScriptError(f"{label} must be a non-empty string")
    return value


def _require_bool(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise ScriptError(f"{label} must be boolean")
    return value


def _require_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ScriptError(f"{label} must be an integer")
    return value


def _optional_string(value: Any, label: str) -> str | None:
    if value is None:
        return None
    return _require_string(value, label)


def validate_experiment_name(name: str) -> None:
    if "/" in name or "\\" in name or ".." in name:
        raise ScriptError(f"experiment name must not contain '/', '\\', or '..': {name}")


def _merged(defaults: dict[str, Any], experiment: dict[str, Any]) -> dict[str, Any]:
    merged = dict(defaults)
    merged.update(experiment)
    return merged


def _field(config: dict[str, Any], merged: dict[str, Any], key: str) -> Any:
    if key in merged:
        return merged[key]
    if key in config:
        return config[key]
    raise ScriptError(f"missing required field: {key}")


def _field_or_none(config: dict[str, Any], merged: dict[str, Any], key: str) -> Any:
    if key in merged:
        return merged[key]
    return config.get(key)


def _default_false(merged: dict[str, Any], key: str) -> bool:
    if key not in merged:
        return False
    return _require_bool(merged[key], key)


def load_experiments(config_path: Path) -> list[MatrixExperiment]:
    config = _require_mapping(_load_json(config_path), "config")
    runner = _require_string(config.get("runner"), "runner")
    summary_script = _optional_string(
        config.get("summary_script"),
        "summary_script",
    ) or str(Path(__file__).with_name("match_summary.py"))
    output_dir = _optional_string(config.get("output_dir"), "output_dir")
    defaults = _require_mapping(config.get("defaults", {}), "defaults")

    raw_experiments = config.get("experiments")
    if not isinstance(raw_experiments, list) or len(raw_experiments) == 0:
        raise ScriptError("experiments must be a non-empty array")

    experiments: list[MatrixExperiment] = []
    for index, raw_experiment in enumerate(raw_experiments):
        experiment = _require_mapping(raw_experiment, f"experiments[{index}]")
        merged = _merged(defaults, experiment)

        name = _require_string(experiment.get("name"), f"experiments[{index}].name")
        validate_experiment_name(name)

        output_value = _field_or_none(config, merged, "output")
        if output_value is None:
            if output_dir is None:
                raise ScriptError(f"missing required field: output_dir for experiment {name}")
            output = str(Path(output_dir) / f"{name}.jsonl")
        else:
            output = _require_string(output_value, f"experiments[{index}].output")

        summary = _default_false(merged, "summary")
        by_opening = _default_false(merged, "by_opening")
        allow_errors = _default_false(merged, "allow_errors")

        experiments.append(
            MatrixExperiment(
                name=name,
                runner=runner,
                summary_script=summary_script,
                black=_require_string(experiment.get("black"), f"experiments[{index}].black"),
                white=_require_string(experiment.get("white"), f"experiments[{index}].white"),
                games=_require_int(_field(config, merged, "games"), f"experiments[{index}].games"),
                swap_sides=_require_bool(
                    _field(config, merged, "swap_sides"),
                    f"experiments[{index}].swap_sides",
                ),
                seed=_require_int(_field(config, merged, "seed"), f"experiments[{index}].seed"),
                openings=_optional_string(
                    _field_or_none(config, merged, "openings"),
                    f"experiments[{index}].openings",
                ),
                output=output,
                summary=summary,
                by_opening=by_opening,
                allow_errors=allow_errors,
            )
        )

    return experiments


def build_runner_command(experiment: MatrixExperiment) -> list[str]:
    command = [
        experiment.runner,
        "--black",
        experiment.black,
        "--white",
        experiment.white,
        "--games",
        str(experiment.games),
        "--swap-sides",
        "true" if experiment.swap_sides else "false",
        "--seed",
        str(experiment.seed),
    ]
    if experiment.openings:
        command.extend(["--openings", experiment.openings])
    command.extend(["--output", experiment.output])
    return command


def build_summary_command(experiment: MatrixExperiment) -> list[str] | None:
    if not experiment.summary:
        return None
    command = [sys.executable, experiment.summary_script, "--input", experiment.output]
    if experiment.allow_errors:
        command.append("--allow-errors")
    if experiment.by_opening:
        command.append("--by-opening")
    return command


def _print_experiment_header(experiment: MatrixExperiment) -> None:
    print(f"experiment: {experiment.name}", flush=True)
    print(f"output: {experiment.output}", flush=True)


def run_experiments(experiments: list[MatrixExperiment], *, dry_run: bool, fail_fast: bool) -> int:
    succeeded = 0
    failed = 0

    for experiment in experiments:
        _print_experiment_header(experiment)
        runner_command = build_runner_command(experiment)
        summary_command = build_summary_command(experiment)

        if dry_run:
            print(f"runner: {quote_command(runner_command)}", flush=True)
            if summary_command is not None:
                print(f"summary: {quote_command(summary_command)}", flush=True)
            succeeded += 1
            continue

        Path(experiment.output).parent.mkdir(parents=True, exist_ok=True)
        runner_exit_code = run_command(runner_command)
        if runner_exit_code != 0:
            failed += 1
            if fail_fast:
                print_summary(len(experiments), succeeded, failed)
                return runner_exit_code or 1
            continue

        if summary_command is not None:
            summary_exit_code = run_command(summary_command)
            if summary_exit_code != 0:
                failed += 1
                if fail_fast:
                    print_summary(len(experiments), succeeded, failed)
                    return summary_exit_code or 1
                continue

        succeeded += 1

    print_summary(len(experiments), succeeded, failed)
    return 1 if failed else 0


def print_summary(total: int, succeeded: int, failed: int) -> None:
    print(f"total: {total}", flush=True)
    print(f"succeeded: {succeeded}", flush=True)
    print(f"failed: {failed}", flush=True)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a JSON matrix of Othello match experiments.")
    parser.add_argument("--config", required=True, help="experiment matrix JSON config")
    parser.add_argument("--dry-run", action="store_true", help="print commands without running them")
    parser.add_argument("--fail-fast", action="store_true", help="stop on the first failed experiment")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_args(argv)
        experiments = load_experiments(Path(args.config))
        return run_experiments(experiments, dry_run=args.dry_run, fail_fast=args.fail_fast)
    except ScriptError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
