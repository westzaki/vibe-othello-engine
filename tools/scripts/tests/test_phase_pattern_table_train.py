from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import phase_pattern_table_train as phase_trainer  # noqa: E402
import pattern_teacher_v0_train as base_trainer  # noqa: E402
from common import ScriptError  # noqa: E402


OPENING_BOARD = (
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

MIDGAME_BOARD = (
    ".....W..\n"
    "...WWW..\n"
    "...WWW..\n"
    "..WWWWBB\n"
    "..WWWWBW\n"
    "..WWWWWW\n"
    "...WB...\n"
    "...W....\n"
    "side=B"
)

LATE_BOARD = (
    "...B....\n"
    "WWBWWW.B\n"
    "WWWWWWWW\n"
    "WBWWWWW.\n"
    "WWWBBWBB\n"
    "WBBBWB.B\n"
    "WWBWBWBB\n"
    "WWWWWWWB\n"
    "side=B"
)

TEACHER_CHILD = (
    "BBB.....\n"
    "BB....B.\n"
    "..B.....\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "side=B"
)

COMPARED_CHILD = (
    "WWW.....\n"
    "WW....W.\n"
    "..W.....\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "side=B"
)


def fake_analyzer(
    config: phase_trainer.PhaseTrainConfig, board_text: str
) -> base_trainer.AnalyzeResult:
    del config, board_text
    return base_trainer.AnalyzeResult(
        candidates=(
            base_trainer.Candidate(move="c4", score=30, child_board=COMPARED_CHILD),
            base_trainer.Candidate(move="d3", score=10, child_board=TEACHER_CHILD),
        )
    )


def cacheable_fake_analyzer(
    config: phase_trainer.PhaseTrainConfig, board_text: str
) -> base_trainer.AnalyzeResult:
    del config, board_text
    return base_trainer.AnalyzeResult(
        candidates=(
            base_trainer.Candidate(move="c4", score=30, child_board=COMPARED_CHILD),
            base_trainer.Candidate(move="d3", score=10, child_board=TEACHER_CHILD),
        ),
        best_move="c4",
        root_scores={"c4": 30, "d3": 10},
        stdout=(
            "root_candidates:\n"
            "  - move: c4\n"
            "    score: 30\n"
            "    child_board:\n"
            + "".join(f"      {line}\n" for line in COMPARED_CHILD.splitlines())
            + "  - move: d3\n"
            "    score: 10\n"
            "    child_board:\n"
            + "".join(f"      {line}\n" for line in TEACHER_CHILD.splitlines())
        ),
    )


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
        encoding="utf-8",
    )


def teacher_row(board: str, *, split: str = "train", move: str = "d3") -> dict[str, object]:
    return {
        "status": "ok",
        "legal_move_valid": True,
        "move_token_valid": True,
        "move": move,
        "board_text": board,
        "position_split": split,
    }


def exact_row(board: str, best_move: str) -> dict[str, object]:
    return {"board": board, "best_move": best_move}


def data_lines(path: Path) -> list[str]:
    return [
        line
        for line in path.read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("#")
    ]


def sentinel_line() -> str:
    family = phase_trainer.SENTINEL_FAMILY
    index, value = phase_trainer.SENTINEL_ENTRY
    return f"{family}\t{index}\t{value}"


def write_fake_analyze_position(path: Path) -> None:
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import sys\n"
        "sys.stdin.read()\n"
        "print('root_candidates:')\n"
        "print('  - move: c4')\n"
        "print('    score: 30')\n"
        "print('    child_board:')\n"
        + "".join(f"print('      {line}')\n" for line in COMPARED_CHILD.splitlines())
        + "print('  - move: d3')\n"
        "print('    score: 10')\n"
        "print('    child_board:')\n"
        + "".join(f"print('      {line}')\n" for line in TEACHER_CHILD.splitlines()),
        encoding="utf-8",
    )
    path.chmod(0o755)


def make_config(
    temp_path: Path,
    teacher_labels: Path,
    *,
    exact_labels: Path | None = None,
    extra_args: list[str] | None = None,
) -> phase_trainer.PhaseTrainConfig:
    args = [
        "--teacher-labels",
        str(teacher_labels),
        "--eval-config",
        str(REPO_ROOT / "data" / "eval" / "pattern_reboot_v0.eval"),
        "--analyze-position",
        str(temp_path / "fake_analyze_position"),
        "--out-dir",
        str(temp_path / "runs" / "phase-broad-v0"),
        "--table-name",
        "phase_broad_v0",
        "--families",
        "broad_all",
        "--update-mode",
        "rank",
        "--split",
        "train",
        "--min-abs-diff",
        "1",
        "--scale",
        "1",
        "--max-abs-weight",
        "4",
    ]
    if exact_labels is not None:
        args.extend(["--exact-labels", str(exact_labels)])
    if extra_args:
        args.extend(extra_args)
    parsed = phase_trainer.parse_args(args)
    return phase_trainer.config_from_args(
        parsed,
        invocation=["phase_pattern_table_train.py", *args],
    )


class PhasePatternTableTrainTests(unittest.TestCase):
    def test_phase_assignment_uses_root_occupied_count_and_broad_alias(self) -> None:
        cutoffs = phase_trainer.PhaseCutoffs(opening_max_occupied=20, midgame_max_occupied=44)

        self.assertEqual(phase_trainer.phase_for_board(OPENING_BOARD, cutoffs), "opening")
        self.assertEqual(phase_trainer.phase_for_board(MIDGAME_BOARD, cutoffs), "midgame")
        self.assertEqual(phase_trainer.phase_for_board(LATE_BOARD, cutoffs), "late")
        self.assertEqual(
            base_trainer.parse_families("broad_all"),
            ("corner_3x3", "edge_8", "edge_x_10", "diagonal_8", "inner_row_8"),
        )

    def test_output_dir_rejects_source_controlled_data(self) -> None:
        with self.assertRaisesRegex(ScriptError, "source-controlled data"):
            phase_trainer.resolve_out_dir(str(REPO_ROOT / "data" / "generated-patterns"))

    def test_run_analysis_parses_fake_analyze_position_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = temp_path / "labels.jsonl"
            write_jsonl(labels, [teacher_row(OPENING_BOARD)])
            fake_tool = temp_path / "fake_analyze_position"
            write_fake_analyze_position(fake_tool)
            config = make_config(temp_path, labels)

            analysis = phase_trainer.run_analysis(config, OPENING_BOARD)

        self.assertEqual([candidate.move for candidate in analysis.candidates], ["c4", "d3"])
        self.assertEqual(analysis.candidates[0].score, 30)
        self.assertEqual(analysis.candidates[1].child_board, TEACHER_CHILD)

    def test_training_writes_phase_tables_candidate_and_summary(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = temp_path / "labels.jsonl"
            write_jsonl(
                labels,
                [
                    teacher_row(OPENING_BOARD),
                    teacher_row(MIDGAME_BOARD),
                    teacher_row(LATE_BOARD),
                ],
            )
            config = make_config(temp_path, labels)

            result = phase_trainer.train_phase_tables(config, analyzer=fake_analyzer)

            for phase in phase_trainer.PHASES:
                table = result.table_paths[phase]
                self.assertTrue(table.exists())
                table_text = table.read_text(encoding="utf-8")
                self.assertIn(f"# phase: {phase}", table_text)
                self.assertIn("edge_x_10\t", table_text)

            candidate = result.candidate_eval_path.read_text(encoding="utf-8")
            self.assertIn("pattern_table.opening=tables/opening.tsv", candidate)
            self.assertIn("pattern_table.midgame=tables/midgame.tsv", candidate)
            self.assertIn("pattern_table.late=tables/late.tsv", candidate)
            self.assertNotIn("\npattern_table=patterns/", candidate)
            entries = phase_trainer.eval_config_entries(candidate)
            scalar_nonzero = [
                key
                for key, value in entries.items()
                if key.partition(".")[0] in phase_trainer.PHASES
                and key.rpartition(".")[2] != "pattern_table"
                and int(value) != 0
            ]
            self.assertEqual(scalar_nonzero, [])

            rows = result.summary["rows"]
            phases = result.summary["phases"]
            self.assertEqual(rows["accepted_teacher_rows"], 3)
            self.assertEqual(rows["training_rows"], 3)
            for phase in phase_trainer.PHASES:
                self.assertEqual(phases[phase]["teacher_rows"], 1)
                self.assertEqual(phases[phase]["updates"], 1)
                self.assertGreater(
                    sum(phases[phase]["entries_by_family"].values()),
                    0,
                )

            self.assertTrue((config.out_dir / "summary.json").exists())
            phase_summary = (config.out_dir / "phase_summary.tsv").read_text(encoding="utf-8")
            self.assertIn(
                "phase\tteacher_rows\tupdates\tskipped\tempty_phase_sentinel\tcorner_3x3_entries",
                phase_summary,
            )
            report = result.report_path.read_text(encoding="utf-8")
            self.assertIn("not a strength claim", report)
            self.assertIn("not a default-promotion recommendation", report)

    def test_exact_split_and_empty_filters_are_counted(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = temp_path / "labels.jsonl"
            exact = temp_path / "exact.jsonl"
            write_jsonl(
                labels,
                [
                    teacher_row(MIDGAME_BOARD),
                    teacher_row(OPENING_BOARD, split="validation"),
                    teacher_row(LATE_BOARD),
                    teacher_row(OPENING_BOARD),
                ],
            )
            write_jsonl(
                exact,
                [
                    exact_row(MIDGAME_BOARD, "d3"),
                    exact_row(OPENING_BOARD, "c4"),
                    exact_row(LATE_BOARD, "d3"),
                ],
            )
            config = make_config(
                temp_path,
                labels,
                exact_labels=exact,
                extra_args=["--empty-min", "20"],
            )

            result = phase_trainer.train_phase_tables(config, analyzer=fake_analyzer)
            rows = result.summary["rows"]
            phases = result.summary["phases"]

            self.assertEqual(rows["accepted_teacher_rows"], 4)
            self.assertEqual(rows["split_skipped"], 1)
            self.assertEqual(rows["empty_min_skipped"], 1)
            self.assertEqual(rows["teacher_exact_disagreements_skipped"], 1)
            self.assertEqual(rows["training_rows"], 1)
            self.assertEqual(rows["updates"], 1)
            self.assertEqual(phases["midgame"]["teacher_rows"], 1)
            self.assertEqual(phases["midgame"]["updates"], 1)
            self.assertEqual(phases["opening"]["skipped"], 2)
            self.assertEqual(phases["late"]["empty_min_skipped"], 1)
            self.assertGreater(len(data_lines(result.table_paths["midgame"])), 0)
            self.assertEqual(data_lines(result.table_paths["opening"]), [sentinel_line()])
            self.assertEqual(data_lines(result.table_paths["late"]), [sentinel_line()])
            opening_table = result.table_paths["opening"].read_text(encoding="utf-8")
            self.assertIn("# empty_phase_sentinel: true", opening_table)
            self.assertIn("# zero_effect: true", opening_table)
            self.assertEqual(phases["opening"]["empty_phase_sentinel"], True)
            self.assertEqual(phases["opening"]["sentinel"]["value"], 0)

    def test_empty_max_filter_is_counted(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = temp_path / "labels.jsonl"
            write_jsonl(
                labels,
                [
                    teacher_row(OPENING_BOARD),
                    teacher_row(MIDGAME_BOARD),
                ],
            )
            config = make_config(
                temp_path,
                labels,
                extra_args=["--empty-max", "50"],
            )

            result = phase_trainer.train_phase_tables(config, analyzer=fake_analyzer)
            rows = result.summary["rows"]
            phases = result.summary["phases"]
            opening_lines = data_lines(result.table_paths["opening"])

            self.assertEqual(rows["empty_max_skipped"], 1)
            self.assertEqual(rows["training_rows"], 1)
            self.assertEqual(phases["opening"]["empty_max_skipped"], 1)
            self.assertEqual(phases["midgame"]["updates"], 1)
            self.assertEqual(opening_lines, [sentinel_line()])

    def test_training_uses_shared_analysis_cache_between_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = temp_path / "labels.jsonl"
            write_jsonl(labels, [teacher_row(OPENING_BOARD)])
            cache_dir = temp_path / "analysis-cache"
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--analysis-cache-dir",
                    str(cache_dir),
                    "--analysis-cache-mode",
                    "read-write",
                ],
            )
            config.analyze_position.write_text("fake analyzer\n", encoding="utf-8")
            calls = 0

            def counting_analyzer(
                inner_config: phase_trainer.PhaseTrainConfig,
                board_text: str,
            ) -> base_trainer.AnalyzeResult:
                nonlocal calls
                calls += 1
                return cacheable_fake_analyzer(inner_config, board_text)

            first = phase_trainer.train_phase_tables(config, analyzer=counting_analyzer)
            second = phase_trainer.train_phase_tables(config, analyzer=counting_analyzer)

        self.assertEqual(calls, 1)
        self.assertEqual(first.summary["rows"]["analysis_cache_misses"], 1)
        self.assertEqual(first.summary["rows"]["analysis_cache_writes"], 1)
        self.assertEqual(second.summary["rows"]["analysis_cache_hits"], 1)
        self.assertEqual(second.summary["rows"]["analysis_cache_misses"], 0)


if __name__ == "__main__":
    unittest.main()
