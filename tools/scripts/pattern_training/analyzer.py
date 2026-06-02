from __future__ import annotations

import errno
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from common import ScriptError, quote_command
from pattern_training.root_candidates import RootAnalysis, parse_analysis_stdout


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

