from __future__ import annotations

import collections

from common import ScriptError, quote_command
from pattern_specs import COMMON_FAMILY_ALIASES, FAMILY_ORDER, PATTERN_SPECS


LEGACY_FAMILY_ALIASES: dict[str, tuple[str, ...]] = {
    **COMMON_FAMILY_ALIASES,
    "corner_only": ("corner_3x3",),
    "edge_context_only": ("edge_x_10",),
}


def parse_families(
    text: str,
    *,
    aliases: dict[str, tuple[str, ...]] = LEGACY_FAMILY_ALIASES,
) -> tuple[str, ...]:
    families: list[str] = []
    for raw_part in text.split(","):
        part = raw_part.strip()
        if not part:
            continue
        expanded = aliases.get(part, (part,))
        for family in expanded:
            if family not in PATTERN_SPECS:
                raise ScriptError(f"unknown pattern family: {family}")
            if family not in families:
                families.append(family)
    if not families:
        raise ScriptError("--families selected no pattern families")
    return tuple(families)


def swapped_index(index: int, cells: int) -> int:
    swapped = 0
    place = 1
    for _ in range(cells):
        state = index % 3
        index //= 3
        if state == 1:
            state = 2
        elif state == 2:
            state = 1
        swapped += state * place
        place *= 3
    return swapped


def clamp(value: int, maximum: int) -> int:
    return max(-maximum, min(maximum, value))


def sparse_entries(
    counts: collections.Counter[int],
    *,
    cells: int,
    limit_pairs: int,
    min_abs_diff: int,
    scale: int,
    max_abs_weight: int,
) -> list[tuple[int, int]]:
    pairs: list[tuple[int, int, int]] = []
    visited: set[int] = set()
    for index in set(counts):
        if index in visited or index == 0:
            continue
        partner = swapped_index(index, cells)
        visited.add(index)
        visited.add(partner)
        if index > partner:
            continue
        diff = counts[index] - counts[partner]
        if abs(diff) < min_abs_diff:
            continue
        weight = clamp(round(diff / scale), max_abs_weight)
        if weight != 0:
            pairs.append((abs(diff), index, weight))

    pairs.sort(reverse=True)
    entries: list[tuple[int, int]] = []
    for _, index, weight in pairs[:limit_pairs]:
        partner = swapped_index(index, cells)
        entries.append((index, weight))
        if partner != index:
            entries.append((partner, -weight))
    return sorted(entries)


def render_table(
    *,
    name: str = "pattern_teacher_v0",
    corner_entries: list[tuple[int, int]] | None = None,
    edge_entries: list[tuple[int, int]] | None = None,
    family_entries: dict[str, list[tuple[int, int]]] | None = None,
    stats: dict[str, int],
    command: list[str],
    generated_by: str = "tools/scripts/pattern_training/pattern_tables.py",
) -> str:
    entries_by_family: dict[str, list[tuple[int, int]]] = {}
    if family_entries is not None:
        entries_by_family.update(family_entries)
    if corner_entries is not None:
        entries_by_family["corner_2x3"] = corner_entries
    if edge_entries is not None:
        entries_by_family["edge_8"] = edge_entries

    lines = [
        "# schema_version: pattern_table.v1",
        f"# name: {name}",
        f"# generated_by: {generated_by}",
        f"# command: {quote_command(command)}",
    ]
    for key in sorted(stats):
        lines.append(f"# {key}: {stats[key]}")
    lines.append("")
    for family in FAMILY_ORDER:
        lines.extend(
            f"{family}\t{index}\t{value}"
            for index, value in entries_by_family.get(family, [])
        )
    return "\n".join(lines) + "\n"
