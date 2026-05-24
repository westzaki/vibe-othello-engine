from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SCRIPT_DIR))

import extract_divergence_positions as divergence  # noqa: E402


CORNER_DIR = REPO_ROOT / "data" / "positions" / "tactical" / "corner"


class CornerTacticalFixtureTests(unittest.TestCase):
    def test_pr115_immediate_corner_fixture_parses_and_has_a1(self) -> None:
        text = (CORNER_DIR / "pr115_immediate_corner.txt").read_text(encoding="utf-8").strip()
        board = divergence.parse_board(text)

        self.assertEqual(divergence.format_board(board), text)
        self.assertEqual(board.side, "W")
        self.assertIn(divergence.parse_move("a1"), divergence.legal_moves(board))

    def test_pr115_after_a1_fixture_parses_with_black_to_move(self) -> None:
        text = (CORNER_DIR / "pr115_after_a1_black_response.txt").read_text(
            encoding="utf-8"
        ).strip()
        board = divergence.parse_board(text)

        self.assertEqual(divergence.format_board(board), text)
        self.assertEqual(board.side, "B")


if __name__ == "__main__":
    unittest.main()
