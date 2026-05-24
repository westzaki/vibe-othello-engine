from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import extract_divergence_positions as divergence  # noqa: E402


INITIAL_BOARD = "\n".join(
    (
        "........",
        "........",
        "........",
        "...BW...",
        "...WB...",
        "........",
        "........",
        "........",
        "side=B",
    )
)


def record(
    game_index: int,
    black_is_player_a: bool,
    moves: list[str],
    score_diff_from_player_a: int = 12,
    opening_name: str = "initial",
    opening_moves: list[str] | None = None,
    start_board: str = INITIAL_BOARD,
) -> dict[str, object]:
    return {
        "game_index": game_index,
        "opening_index": 0,
        "opening_name": opening_name,
        "opening_moves": opening_moves or [],
        "start_board": start_board,
        "black_is_player_a": black_is_player_a,
        "score_diff_from_player_a": score_diff_from_player_a,
        "moves": moves,
        "illegal_or_error": False,
    }


class ExtractDivergencePositionsTests(unittest.TestCase):
    def test_board_round_trip_preserves_text_shape(self) -> None:
        board = divergence.parse_board(INITIAL_BOARD)

        self.assertEqual(divergence.format_board(board), INITIAL_BOARD)

    def test_replay_moves_reconstructs_known_smoke_opening(self) -> None:
        replay = divergence.replay_moves(INITIAL_BOARD, ["c4", "c3"])

        self.assertEqual(
            divergence.format_board(replay.board),
            "\n".join(
                (
                    "........",
                    "........",
                    "........",
                    "...BW...",
                    "..BWB...",
                    "..W.....",
                    "........",
                    "........",
                    "side=B",
                )
            ),
        )
        self.assertEqual(replay.turns, 2)
        self.assertEqual(replay.passes, 0)

    def test_extracts_first_divergence_from_swap_side_pair(self) -> None:
        records = [
            record(game_index=0, black_is_player_a=True, moves=["d3", "c3"], score_diff_from_player_a=-12),
            record(game_index=1, black_is_player_a=False, moves=["c4", "c3"], score_diff_from_player_a=-12),
        ]

        divergences = divergence.extract_divergences(records)

        self.assertEqual(len(divergences), 1)
        self.assertEqual(divergences[0].opening_name, "initial")
        self.assertEqual(divergences[0].ply, 0)
        self.assertEqual(divergences[0].side_to_move, "black")
        self.assertEqual(divergences[0].head_move, "d3")
        self.assertEqual(divergences[0].base_move, "c4")
        self.assertEqual(divergences[0].board_text, INITIAL_BOARD)

    def test_no_divergence_pair_is_omitted(self) -> None:
        records = [
            record(game_index=0, black_is_player_a=True, moves=["d3", "c3"]),
            record(game_index=1, black_is_player_a=False, moves=["d3", "c3"]),
        ]

        self.assertEqual(divergence.extract_divergences(records), [])

    def test_markdown_output_includes_stable_columns(self) -> None:
        divergences = [
            divergence.extract_pair_divergence(
                0,
                record(game_index=0, black_is_player_a=True, moves=["d3"]),
                record(game_index=1, black_is_player_a=False, moves=["c4"]),
            )
        ]

        text = divergence.render_markdown([item for item in divergences if item is not None])

        self.assertIn("| pair | opening | head game | base game | ply | side |", text)
        self.assertIn("| 0 | initial | 0 | 1 | 0 | black | d3 | c4 |", text)


if __name__ == "__main__":
    unittest.main()
