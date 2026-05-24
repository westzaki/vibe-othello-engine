from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import match_summary  # noqa: E402
from common import ScriptError  # noqa: E402


def record(
    game_index: int = 0,
    opening_index: int = 0,
    opening_name: str = "initial",
    score_diff_from_player_a: int = 16,
    plies: int = 60,
    passes: int = 2,
    illegal_or_error: bool = False,
    **extra: object,
) -> dict[str, object]:
    value: dict[str, object] = {
        "game_index": game_index,
        "player_a_spec": "search:depth=3",
        "player_b_spec": "random",
        "black_spec": "search:depth=3",
        "white_spec": "random",
        "black_is_player_a": True,
        "opening_index": opening_index,
        "opening_name": opening_name,
        "winner": "black",
        "black_score": 40,
        "white_score": 24,
        "score_diff_from_player_a": score_diff_from_player_a,
        "plies": plies,
        "passes": passes,
        "illegal_or_error": illegal_or_error,
    }
    value.update(extra)
    return value


class MatchSummaryTests(unittest.TestCase):
    def write_jsonl(self, lines: list[str]) -> Path:
        temp = tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False)
        with temp:
            for line in lines:
                temp.write(line)
                temp.write("\n")
        return Path(temp.name)

    def test_valid_jsonl_record_parse_and_summarize(self) -> None:
        parsed = match_summary.parse_jsonl_line(json.dumps(record()), 1)
        summary = match_summary.summarize([parsed])

        self.assertEqual(parsed["player_a_spec"], "search:depth=3")
        self.assertEqual(summary.games, 1)
        self.assertEqual(summary.valid_games, 1)
        self.assertEqual(summary.player_a_wins, 1)
        self.assertEqual(summary.average_diff, 16.0)

    def test_unknown_fields_are_ignored(self) -> None:
        parsed = match_summary.parse_jsonl_line(
            json.dumps(record(extra={"nested": [True, False, None, 12, "x"]})),
            1,
        )

        self.assertEqual(parsed["score_diff_from_player_a"], 16)

    def test_nodes_and_time_fields_are_averaged_when_present(self) -> None:
        records = [
            match_summary.parse_jsonl_line(
                json.dumps(
                    record(
                        game_index=0,
                        nodes_player_a=10,
                        nodes_player_b=20,
                        time_ms_player_a=1.5,
                        time_ms_player_b=2.5,
                    )
                ),
                1,
            ),
            match_summary.parse_jsonl_line(
                json.dumps(
                    record(
                        game_index=1,
                        nodes_player_a=30,
                        nodes_player_b=40,
                        time_ms_player_a=3.5,
                        time_ms_player_b=4.5,
                    )
                ),
                2,
            ),
        ]

        summary = match_summary.summarize(records)

        self.assertEqual(summary.optional_average("nodes_player_a"), 20.0)
        self.assertEqual(summary.optional_average("nodes_player_b"), 30.0)
        self.assertEqual(summary.optional_average("time_ms_player_a"), 2.5)
        self.assertEqual(summary.optional_average("time_ms_player_b"), 3.5)

    def test_missing_required_field_is_error(self) -> None:
        value = record()
        del value["game_index"]

        with self.assertRaises(ScriptError):
            match_summary.parse_jsonl_line(json.dumps(value), 1)

    def test_invalid_json_is_error(self) -> None:
        with self.assertRaises(ScriptError):
            match_summary.parse_jsonl_line('{"game_index":0,]', 1)

    def test_empty_input_file_is_error(self) -> None:
        path = self.write_jsonl(["", "   "])
        self.addCleanup(path.unlink)

        with self.assertRaises(ScriptError):
            match_summary.load_records(path)

    def test_illegal_games_are_counted_as_error_games(self) -> None:
        records = [
            match_summary.parse_jsonl_line(json.dumps(record(game_index=0, score_diff_from_player_a=10)), 1),
            match_summary.parse_jsonl_line(
                json.dumps(record(game_index=1, score_diff_from_player_a=-4, illegal_or_error=True)),
                2,
            ),
        ]

        summary = match_summary.summarize(records)

        self.assertEqual(summary.games, 2)
        self.assertEqual(summary.valid_games, 1)
        self.assertEqual(summary.error_games, 1)
        self.assertEqual(summary.player_a_wins, 1)
        self.assertEqual(summary.average_diff, 10.0)

    def test_opening_summary_aggregation(self) -> None:
        records = [
            match_summary.parse_jsonl_line(
                json.dumps(record(game_index=0, opening_index=0, opening_name="initial", score_diff_from_player_a=10)),
                1,
            ),
            match_summary.parse_jsonl_line(
                json.dumps(record(game_index=1, opening_index=0, opening_name="initial", score_diff_from_player_a=-4)),
                2,
            ),
            match_summary.parse_jsonl_line(
                json.dumps(
                    record(
                        game_index=2,
                        opening_index=1,
                        opening_name="c4-c3",
                        score_diff_from_player_a=6,
                        illegal_or_error=True,
                    )
                ),
                3,
            ),
        ]

        summary = match_summary.summarize(records)
        openings = list(summary.openings.values())

        self.assertEqual(len(openings), 2)
        self.assertEqual(openings[0].games, 2)
        self.assertEqual(openings[0].player_a_wins, 1)
        self.assertEqual(openings[0].player_b_wins, 1)
        self.assertEqual(openings[0].average_diff, 3.0)
        self.assertEqual(openings[1].games, 1)
        self.assertEqual(openings[1].error_games, 1)
        self.assertEqual(openings[1].valid_games, 0)

    def test_main_returns_one_for_error_games_by_default(self) -> None:
        path = self.write_jsonl([json.dumps(record(illegal_or_error=True))])
        self.addCleanup(path.unlink)

        with contextlib.redirect_stdout(io.StringIO()):
            exit_code = match_summary.main(["--input", str(path)])

        self.assertEqual(exit_code, 1)

    def test_main_allows_error_games_when_requested(self) -> None:
        path = self.write_jsonl([json.dumps(record(illegal_or_error=True))])
        self.addCleanup(path.unlink)

        with contextlib.redirect_stdout(io.StringIO()):
            exit_code = match_summary.main(["--input", str(path), "--allow-errors"])

        self.assertEqual(exit_code, 0)


if __name__ == "__main__":
    unittest.main()
