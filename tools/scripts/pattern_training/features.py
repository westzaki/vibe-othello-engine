from __future__ import annotations

import collections
from typing import Protocol

from pattern_specs import PATTERN_SPECS, board9_rows_to_square_index_rows, pattern_index
from pattern_training.board9 import board_key, opponent, parse_board

FeatureKey = tuple[str, int]


class FeatureCacheLike(Protocol):
    def counts(self, board_text: str, side: str) -> collections.Counter[FeatureKey]: ...


def pattern_counts(
    board_text: str,
    root_side: str,
    families: tuple[str, ...],
) -> collections.Counter[FeatureKey]:
    rows, _ = parse_board(board_text)
    square_index_rows = board9_rows_to_square_index_rows(rows)
    counts: collections.Counter[FeatureKey] = collections.Counter()
    for family in families:
        for spec in PATTERN_SPECS[family]:
            counts[(family, pattern_index(square_index_rows, root_side, spec))] += 1
    return counts


def _counts(
    board_text: str,
    side: str,
    families: tuple[str, ...],
    feature_cache: FeatureCacheLike | None,
) -> collections.Counter[FeatureKey]:
    if feature_cache is None:
        return pattern_counts(board_text, side, families)
    return feature_cache.counts(board_text, side)


def root_move_features(
    *,
    root_board_text: str,
    child_board_text: str,
    families: tuple[str, ...],
    feature_cache: FeatureCacheLike | None = None,
) -> dict[FeatureKey, int]:
    """Return PatternOnly root-move features for one legal root move.

    PatternOnly evaluates root moves as the negated child-side evaluation.
    Therefore each child-side pattern count contributes negatively to the root
    move score.
    """

    _, root_side = parse_board(root_board_text)
    child_side = opponent(root_side)
    counts = _counts(child_board_text, child_side, families, feature_cache)
    return {key: -value for key, value in counts.items() if value}


def preference_delta(
    *,
    root_board_text: str,
    preferred_child_board: str,
    compared_child_board: str,
    families: tuple[str, ...],
    feature_cache: FeatureCacheLike | None = None,
) -> dict[FeatureKey, int]:
    """Return pairwise features that increase preferred - compared root margin.

    Root scores negate child-side evaluations, so raising the preferred root
    move relative to the compared move uses compared child counts minus
    preferred child counts.
    """

    _, root_side = parse_board(root_board_text)
    child_side = opponent(root_side)
    preferred_counts = _counts(preferred_child_board, child_side, families, feature_cache)
    compared_counts = _counts(compared_child_board, child_side, families, feature_cache)
    delta: dict[FeatureKey, int] = {}
    for key in set(preferred_counts) | set(compared_counts):
        value = compared_counts[key] - preferred_counts[key]
        if value:
            delta[key] = value
    return delta


def root_move_cache_key(root_board_text: str, move: str) -> str:
    return f"{board_key(root_board_text)}\n{move}"
