from __future__ import annotations

import sys
import unittest
import argparse
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import teacher_label_mistake_mining as mining  # noqa: E402


ANALYZE_OUTPUT = """\
Othello position analysis

side_to_move: black
legal_moves: a1 b2 d3
best_move: d3

evaluation_breakdown:
  side: black
  phase: midgame
  occupied_count: 42
  empty_count: 22

root_candidates:
  - move: d3
    score: 20
  - move: a1
    score: 20
  - move: b2
    score: -5
"""


class TeacherLabelMistakeMiningTests(unittest.TestCase):
    def test_parse_analysis_stdout_extracts_move_scores_and_phase(self) -> None:
        parsed = mining.parse_analysis_stdout(ANALYZE_OUTPUT)

        self.assertEqual(parsed.best_move, "d3")
        self.assertEqual(parsed.legal_moves, ["a1", "b2", "d3"])
        self.assertEqual(parsed.phase, "midgame")
        self.assertEqual(parsed.occupied_count, 42)
        self.assertEqual(parsed.empty_count, 22)
        self.assertEqual(parsed.root_scores, {"d3": 20, "a1": 20, "b2": -5})

    def test_rank_for_move_uses_top_score_group(self) -> None:
        parsed = mining.parse_analysis_stdout(ANALYZE_OUTPUT)

        self.assertEqual(mining.rank_for_move(parsed.root_scores, "d3"), 1)
        self.assertEqual(mining.rank_for_move(parsed.root_scores, "a1"), 1)
        self.assertEqual(mining.rank_for_move(parsed.root_scores, "b2"), 3)
        self.assertIsNone(mining.rank_for_move(parsed.root_scores, "h8"))

    def test_classification_prefers_teacher_exact_disagreement(self) -> None:
        bucket = mining.classify_bucket(
            teacher_move="a1",
            selected_move="d3",
            exact_best=["d3"],
            metadata={"tags": "corner_access,x_square_danger"},
            phase="midgame",
            empty_count=22,
        )

        self.assertEqual(bucket, "teacher_exact_disagreement")

    def test_parse_target_requires_label_and_path(self) -> None:
        target = mining.parse_target("classic=data/eval/classic_othello_v1.eval")

        self.assertEqual(target.label, "classic")
        self.assertEqual(target.path, Path("data/eval/classic_othello_v1.eval"))
        with self.assertRaises(argparse.ArgumentTypeError):
            mining.parse_target("missing")


if __name__ == "__main__":
    unittest.main()
