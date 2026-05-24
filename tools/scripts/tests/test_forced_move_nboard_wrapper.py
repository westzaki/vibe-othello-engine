from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import forced_move_nboard_wrapper as wrapper  # noqa: E402
from common import ScriptError  # noqa: E402
from extract_divergence_positions import format_board, parse_board  # noqa: E402


PR115_BOARD = "\n".join(
    (
        "........",
        "........",
        "........",
        "...BW...",
        "..BBW...",
        "..BWW...",
        ".B......",
        "........",
        "side=W",
    )
)


class ForcedMoveNBoardWrapperTests(unittest.TestCase):
    def test_extracts_ggf_moves_and_reaches_pr115_board(self) -> None:
        game = "(;GM[Othello]B[D3]W[C3]B[C4]W[E3]B[B2];)"

        self.assertEqual(
            wrapper.extract_moves_from_game_text(game),
            ["d3", "c3", "c4", "e3", "b2"],
        )
        self.assertEqual(format_board(wrapper.board_after_game_text(game)), PR115_BOARD)

    def test_forces_move_only_on_target_board(self) -> None:
        target = wrapper.normalize_board_text(PR115_BOARD)
        board = wrapper.board_after_game_text("(;B[D3]W[C3]B[C4]W[E3]B[B2];)")
        initial = parse_board(wrapper.INITIAL_BOARD)

        self.assertEqual(wrapper.forced_move_for_board(board, target, "a1"), "a1")
        self.assertIsNone(wrapper.forced_move_for_board(initial, target, "a1"))

    def test_rejects_illegal_forced_move_on_target_board(self) -> None:
        target = wrapper.normalize_board_text(PR115_BOARD)
        board = wrapper.board_after_game_text("(;B[D3]W[C3]B[C4]W[E3]B[B2];)")

        with self.assertRaises(ScriptError):
            wrapper.forced_move_for_board(board, target, "h8")


if __name__ == "__main__":
    unittest.main()
