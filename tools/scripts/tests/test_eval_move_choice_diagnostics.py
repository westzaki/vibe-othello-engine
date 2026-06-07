from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import eval_move_choice_diagnostics as diagnostics  # noqa: E402
from pattern_training.root_candidates import RootAnalysis  # noqa: E402


BOARD = (
    "........\n"
    "........\n"
    "........\n"
    "...WB...\n"
    "...BW...\n"
    "........\n"
    "........\n"
    "........\n"
    "side=B"
)


class EvalMoveChoiceDiagnosticsTests(unittest.TestCase):
    def test_rank_for_move_counts_strictly_higher_scores(self) -> None:
        scores = {"d3": 10, "c4": 10, "f5": 4}

        self.assertEqual(diagnostics.rank_for_move(scores, "d3"), 1)
        self.assertEqual(diagnostics.rank_for_move(scores, "f5"), 3)
        self.assertIsNone(diagnostics.rank_for_move(scores, "a1"))

    def test_move_family_classifies_common_square_types(self) -> None:
        self.assertEqual(diagnostics.move_family("a1"), "corner")
        self.assertEqual(diagnostics.move_family("b2"), "x-square")
        self.assertEqual(diagnostics.move_family("a2"), "c-square")
        self.assertEqual(diagnostics.move_family("a4"), "edge")
        self.assertEqual(diagnostics.move_family("d4"), "interior")
        self.assertEqual(diagnostics.move_family("pass"), "pass")

    def test_summarize_target_reports_top_group_and_exact_rank(self) -> None:
        row = diagnostics.LabelRow(
            position_id="p1",
            split="validation",
            board_text=BOARD,
            teacher_move="d3",
            exact_best_moves=("c4",),
        )
        analysis = RootAnalysis(
            best_move="c4",
            root_scores={"d3": 10, "c4": 10, "f5": 4},
        )

        summary = diagnostics.summarize_target([row], {"p1": analysis})
        overall = summary["overall"]

        self.assertEqual(overall["rows"], 1)
        self.assertEqual(overall["selected_teacher_agreements"], 0)
        self.assertEqual(overall["teacher_rank1"], 1)
        self.assertEqual(overall["teacher_rank2_or_better"], 1)
        self.assertEqual(overall["teacher_rank3_or_better"], 1)
        self.assertEqual(overall["top_group_tie_rows"], 1)
        self.assertEqual(overall["teacher_in_top_group"], 1)
        self.assertEqual(overall["exact_best_rank_sum"], 1)
        self.assertEqual(overall["exact_best_top_group_hits"], 1)
        self.assertIn("validation", summary["by_split"])
        self.assertIn("opening", summary["by_phase"])
        self.assertIn("49-60", summary["by_empties"])
        self.assertIn("interior", summary["by_teacher_move_family"])
        self.assertIn("interior", summary["by_selected_move_family"])

    def test_render_markdown_includes_breakdown_sections(self) -> None:
        row = diagnostics.LabelRow(position_id="p1", split="train", board_text=BOARD, teacher_move="d3")
        analysis = RootAnalysis(best_move="c4", root_scores={"d3": 10, "c4": 12})
        summary = diagnostics.summarize_target([row], {"p1": analysis})

        markdown = diagnostics.render_markdown(
            {"targets": {"demo": summary}},
            ["diag", "--config", "demo=eval"],
        )

        self.assertIn("## Phases", markdown)
        self.assertIn("## Empties Buckets", markdown)
        self.assertIn("## Teacher Move Families", markdown)
        self.assertIn("## Selected Move Families", markdown)


if __name__ == "__main__":
    unittest.main()
