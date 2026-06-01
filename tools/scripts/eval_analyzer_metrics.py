"""Parse othello_eval_vs_exact stdout metrics."""

from __future__ import annotations

import re
from dataclasses import dataclass

from common import ScriptError


MOVE_RANK_METRIC_KEYS = (
    "move_rank_records_with_scores",
    "move_rank_records_missing_scores",
    "move_rank_records_no_legal_root_moves",
    "move_rank_analyzed",
    "move_rank_top_exact_best",
    "move_rank_top_non_best",
    "move_rank_exact_best_rank_sum",
    "move_rank_eval_score_gap_sum",
    "move_rank_exact_score_gap_sum",
)


@dataclass(frozen=True)
class AnalyzerMetrics:
    records_read: int
    analyzed: int
    sign_agreements: int
    wrong_direction: int
    high_confidence_wrong_direction: int
    move_rank_records_with_scores: int | None = None
    move_rank_records_missing_scores: int | None = None
    move_rank_records_no_legal_root_moves: int | None = None
    move_rank_analyzed: int | None = None
    move_rank_top_exact_best: int | None = None
    move_rank_top_non_best: int | None = None
    move_rank_exact_best_rank_sum: int | None = None
    move_rank_eval_score_gap_sum: int | None = None
    move_rank_exact_score_gap_sum: int | None = None


def parse_metric(stdout: str, key: str) -> int:
    match = re.search(rf"\b{re.escape(key)}=(\d+)\b", stdout)
    if not match:
        raise ScriptError(f"analyzer stdout missing required metric: {key}")
    return int(match.group(1))


def parse_optional_metric(stdout: str, key: str) -> int | None:
    match = re.search(rf"\b{re.escape(key)}=(-?\d+)\b", stdout)
    return int(match.group(1)) if match else None


def parse_analyzer_stdout(stdout: str) -> AnalyzerMetrics:
    return AnalyzerMetrics(
        records_read=parse_metric(stdout, "records_read"),
        analyzed=parse_metric(stdout, "analyzed"),
        sign_agreements=parse_metric(stdout, "sign_agreements"),
        wrong_direction=parse_metric(stdout, "wrong_direction"),
        high_confidence_wrong_direction=parse_metric(
            stdout,
            "high_confidence_wrong_direction",
        ),
        **{key: parse_optional_metric(stdout, key) for key in MOVE_RANK_METRIC_KEYS},
    )


def move_rank_cells(metrics: AnalyzerMetrics | None) -> list[str]:
    if metrics is None:
        return ["n/a"] * len(MOVE_RANK_METRIC_KEYS)
    return [
        str(value) if (value := getattr(metrics, key)) is not None else "n/a"
        for key in MOVE_RANK_METRIC_KEYS
    ]
