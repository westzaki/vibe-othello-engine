#!/usr/bin/env python3
"""Fake teacher score engine for teacher_score_label_workflow.py tests."""

from __future__ import annotations

import argparse
import json
import sys

from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from pattern_training.board9 import legal_moves_for_board  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("complete", "partial", "illegal", "move-scores"), default="complete")
    args = parser.parse_args()
    board = sys.stdin.read()
    legal_moves = sorted(legal_moves_for_board(board))
    scores = {move: 100 - index for index, move in enumerate(legal_moves)}
    if args.mode == "partial" and scores:
        scores.pop(sorted(scores)[-1])
    if args.mode == "illegal":
        scores["a1"] = 999
    if args.mode == "move-scores":
        print(
            json.dumps(
                {
                    "move_scores": [
                        {"move": move, "teacher_score_side_to_move": score}
                        for move, score in sorted(scores.items())
                    ]
                }
            )
        )
        return 0
    print(json.dumps({"root_scores": scores}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
