"""Persistent external engine protocol adapter boundary.

Stateful protocols such as NTest or Edax should grow here, separate from the
one-shot process adapter. This scaffold intentionally does not implement a
protocol yet.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field


@dataclass(frozen=True)
class PersistentEngineConfig:
    command: Sequence[str]
    workdir: str | None = None
    env: Mapping[str, str] = field(default_factory=dict)
    startup_timeout_ms: int = 1000
    move_timeout_ms: int = 1000


class PersistentProtocolAdapter:
    """Placeholder for stateful external engine protocols."""

    def __init__(self, config: PersistentEngineConfig) -> None:
        self.config = config

    def request_best_move(self, board_text: str) -> str:
        raise NotImplementedError("persistent external engine protocols are not implemented yet")
