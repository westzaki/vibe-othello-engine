from __future__ import annotations

import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]


CORNER_DIR = REPO_ROOT / "data" / "positions" / "tactical" / "corner"


def require_board_text(text: str) -> None:
    lines = text.splitlines()
    assert len(lines) == 9
    for line in lines[:8]:
        assert len(line) == 8
        assert all(value in ".BW" for value in line)
    assert lines[8] in {"side=B", "side=W"}


class CornerTacticalFixtureTests(unittest.TestCase):
    def test_pr115_immediate_corner_fixture_has_board_shape(self) -> None:
        text = (CORNER_DIR / "pr115_immediate_corner.txt").read_text(encoding="utf-8").strip()

        require_board_text(text)
        self.assertEqual(text.splitlines()[8], "side=W")

    def test_pr115_after_a1_fixture_has_board_shape(self) -> None:
        text = (CORNER_DIR / "pr115_after_a1_black_response.txt").read_text(
            encoding="utf-8"
        ).strip()

        require_board_text(text)
        self.assertEqual(text.splitlines()[8], "side=B")


if __name__ == "__main__":
    unittest.main()
