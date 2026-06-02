from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class RootCandidate:
    move: str
    score: int | None
    child_board: str | None = None


@dataclass(frozen=True)
class RootAnalysis:
    best_move: str | None = None
    root_scores: dict[str, int] = field(default_factory=dict)
    candidates: tuple[RootCandidate, ...] = ()
    stdout: str = ""


def normalize_move(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    return text or None


def parse_int_value(value: str) -> int | None:
    try:
        return int(value)
    except ValueError:
        return None


def parse_analysis_stdout(text: str) -> RootAnalysis:
    best_move: str | None = None
    root_scores: dict[str, int] = {}
    candidates: list[RootCandidate] = []
    current_move: str | None = None
    current_score: int | None = None
    child_lines: list[str] | None = None
    in_root_candidates = False

    def finish_candidate() -> None:
        nonlocal current_move, current_score, child_lines
        if current_move is not None:
            child_board = "\n".join(child_lines) if child_lines else None
            candidates.append(
                RootCandidate(move=current_move, score=current_score, child_board=child_board)
            )
        current_move = None
        current_score = None
        child_lines = None

    for raw_line in text.splitlines():
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped:
            continue
        if stripped == "root_candidates:":
            in_root_candidates = True
            current_move = None
            current_score = None
            child_lines = None
            continue
        if not line.startswith(" "):
            finish_candidate()
            key, separator, value = stripped.partition(":")
            if separator and key == "best_move":
                best_move = normalize_move(value)
            if stripped.endswith(":") and stripped != "root_candidates:":
                in_root_candidates = False
            continue
        if not in_root_candidates:
            continue
        if line.startswith("  - move:"):
            finish_candidate()
            current_move = normalize_move(stripped.split(":", 1)[1]) or "pass"
            continue
        if current_move is None:
            continue
        if line == "    child_board:":
            child_lines = []
            continue
        if child_lines is not None and len(child_lines) < 9 and line.startswith("      "):
            child_lines.append(line[6:])
            continue
        if line.startswith("    score:"):
            current_score = parse_int_value(stripped.split(":", 1)[1].strip())
            if current_score is not None:
                root_scores[current_move] = current_score
    finish_candidate()

    if best_move is None and root_scores:
        best_move = sorted(root_scores.items(), key=lambda item: (-item[1], item[0]))[0][0]
    return RootAnalysis(
        best_move=best_move,
        root_scores=root_scores,
        candidates=tuple(candidates),
        stdout=text,
    )
