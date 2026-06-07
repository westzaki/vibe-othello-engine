from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import regularized_pairwise_pattern_train as trainer  # noqa: E402
from common import ScriptError  # noqa: E402


TEACHER_BOARD = (
    ".BBBBBB.\n"
    ".BBWWB..\n"
    "BWWWWWWW\n"
    "BWWBBBBB\n"
    "WWBWBBBB\n"
    "WWWBBBB.\n"
    ".WWWBWWW\n"
    "W.B.BBB.\n"
    "side=B"
)


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
        encoding="utf-8",
    )
    return path


def write_eval_config(path: Path) -> Path:
    path.write_text(
        "# schema_version: eval.v1\n"
        "name=pattern_fixture\n"
        "opening.mobility=8\n"
        "midgame.mobility=10\n"
        "late.mobility=6\n"
        "opening_max_occupied=20\n"
        "midgame_max_occupied=44\n",
        encoding="utf-8",
    )
    return path


def teacher_row(
    *,
    board: str = TEACHER_BOARD,
    move: str = "d1",
    split: str = "train",
    position_id: str = "fixture",
) -> dict[str, object]:
    return {
        "schema": "teacher_label.v1",
        "status": "ok",
        "legal_move_valid": True,
        "move_token_valid": True,
        "board_text": board,
        "move": move,
        "position_split": split,
        "position_id": position_id,
    }


def exact_row(
    board: str,
    *,
    best_move: str,
    move_scores: dict[str, int] | None = None,
) -> dict[str, object]:
    row: dict[str, object] = {
        "schema": "exact_label.v1",
        "board": board,
        "best_move": best_move,
        "best_moves": [best_move],
        "exact_score_side_to_move": 12,
    }
    if move_scores is not None:
        row["move_scores"] = [
            {"move": move, "exact_score_side_to_move": score}
            for move, score in sorted(move_scores.items())
        ]
    return row


def fake_analyzer(
    config: trainer.TrainerConfig,
    board_text: str,
) -> trainer.AnalyzeResult:
    del config
    legal = sorted(trainer.legal_moves_for_board(board_text))
    return trainer.AnalyzeResult(
        best_move=legal[0],
        root_scores={move: 100 - index for index, move in enumerate(legal)},
    )


def make_config(
    temp_path: Path,
    labels: Path,
    *,
    exact_labels: Path | None = None,
    extra_args: list[str] | None = None,
) -> trainer.TrainerConfig:
    eval_config = write_eval_config(temp_path / "base.eval")
    args = [
        "--teacher-labels",
        str(labels),
        "--eval-config",
        str(eval_config),
        "--analyze-position",
        str(temp_path / "fake_analyze_position"),
        "--out-dir",
        str(temp_path / "runs" / "trainer"),
        "--families",
        "legacy",
        "--epochs",
        "3",
        "--learning-rate",
        "0.2",
        "--l2",
        "0",
        "--seed",
        "7",
    ]
    if exact_labels is not None:
        args.extend(["--exact-labels", str(exact_labels)])
    if extra_args is not None:
        args.extend(extra_args)
    return trainer.config_from_args(
        trainer.parse_args(args),
        invocation=["regularized_pairwise_pattern_train.py", *args],
    )


def all_legal_move_scores(board: str, best_move: str) -> dict[str, int]:
    return {
        move: (20 if move == best_move else -index)
        for index, move in enumerate(sorted(trainer.legal_moves_for_board(board)), start=1)
    }


class CanonicalPatternOnlyListwiseTrainerTests(unittest.TestCase):
    def test_eval_config_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            labels = write_jsonl(Path(temp) / "labels.jsonl", [teacher_row()])
            with mock.patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit):
                trainer.parse_args(
                    [
                        "--teacher-labels",
                        str(labels),
                        "--out-dir",
                        str(Path(temp) / "runs" / "missing-eval-config"),
                    ]
                )

    def test_dataset_reference_escape_rejection_comes_from_shared_resolver(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "datasets"
            root.mkdir()
            with self.assertRaises(ScriptError):
                trainer.parse_label_paths("dataset:../labels.jsonl", dataset_root=str(root))

    def test_deleted_cli_flags_are_not_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            eval_config = write_eval_config(temp_path / "base.eval")
            for flag in (
                "--" + "objective",
                "--" + "loss",
                "--" + "pair" + "-mode",
                "--" + "pair" + "-weighting",
                "--" + "guard" + "-mode",
                "--" + "exact" + "-score-soft-target",
            ):
                with self.subTest(flag=flag):
                    with mock.patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit):
                        trainer.parse_args(
                            [
                                "--teacher-labels",
                                str(labels),
                                "--eval-config",
                                str(eval_config),
                                "--out-dir",
                                str(temp_path / "runs" / "bad"),
                                flag,
                                "x",
                            ]
                        )

    def test_complete_move_scores_create_soft_exact_score_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    exact_row(
                        TEACHER_BOARD,
                        best_move="b1",
                        move_scores=all_legal_move_scores(TEACHER_BOARD, "b1"),
                    )
                ],
            )
            config = make_config(temp_path, labels, exact_labels=exact)

            examples, _, stats = trainer.collect_training_data(config, analyzer=fake_analyzer)

        self.assertEqual(len(examples), 1)
        self.assertIsNotNone(examples[0].target_probabilities)
        probabilities = dict(
            zip(
                [candidate.move for candidate in examples[0].candidates],
                examples[0].target_probabilities or (),
                strict=True,
            )
        )
        self.assertGreater(probabilities["b1"], probabilities["d1"])
        self.assertEqual(stats["soft_target_examples"], 1)

    def test_exact_best_without_move_scores_uses_uniform_exact_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(move="d1")])
            exact = write_jsonl(temp_path / "exact.jsonl", [exact_row(TEACHER_BOARD, best_move="b1")])
            config = make_config(temp_path, labels, exact_labels=exact)

            examples, _, stats = trainer.collect_training_data(config, analyzer=fake_analyzer)
            compact = trainer.compact_listwise_dataset(examples)

        self.assertEqual(examples[0].target_moves, ("b1",))
        self.assertIsNone(examples[0].target_probabilities)
        target_probabilities = dict(
            zip(compact.candidate_moves, compact.candidate_target_probabilities, strict=True)
        )
        self.assertEqual(target_probabilities["b1"], 1.0)
        self.assertEqual(target_probabilities["d1"], 0.0)
        self.assertEqual(stats["exact_best_examples"], 1)

    def test_rows_without_exact_label_fall_back_to_teacher_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(move="d1")])
            config = make_config(temp_path, labels)

            examples, _, stats = trainer.collect_training_data(config, analyzer=fake_analyzer)

        self.assertEqual(examples[0].target_moves, ("d1",))
        self.assertEqual(stats["teacher_fallback_examples"], 1)

    def test_listwise_softmax_overfits_synthetic_target_move(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--epochs", "16", "--learning-rate", "0.4", "--l2", "0"],
            )
            example = trainer.ListwiseExample(
                position_id="synthetic-listwise",
                source_path=Path("synthetic.jsonl"),
                source_line=1,
                split="validation",
                board_text=TEACHER_BOARD,
                teacher_move="bad",
                engine_move="bad",
                target_moves=("good",),
                exact_best_moves=("good",),
                exact_root_score=16,
                candidates=(
                    trainer.CandidateMove(
                        move="good",
                        phase="midgame",
                        features={("edge_8", 2131): 1},
                        exact_score=8,
                    ),
                    trainer.CandidateMove(
                        move="bad",
                        phase="midgame",
                        features={("edge_8", 2179): 1},
                        exact_score=-8,
                    ),
                ),
                target_probabilities=None,
                example_weight=1.0,
                bucket="__missing__",
                bucket_weight=1.0,
            )
            initial: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}

            weights, history = trainer.train_weights(config, [example])

        self.assertLess(
            history[-1]["loss"],
            trainer.evaluate_listwise_examples(config, initial, [example])["loss"],
        )
        scores = trainer.listwise_scores(config, weights, example)
        self.assertGreater(scores["good"], scores["bad"])

    def test_candidate_eval_is_pattern_only_and_omits_base_scalars(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--candidate-pattern-table-weight", "3"],
            )

            candidate = trainer.render_candidate_eval(config)

        self.assertIn("schema_version=eval.v1", candidate)
        self.assertIn("mode=pattern_only", candidate)
        self.assertIn("# model: pattern_only_delta_only", candidate)
        self.assertIn("opening.pattern_table=3", candidate)
        self.assertNotIn("opening.mobility", candidate)

    def test_training_writes_tables_candidate_validation_and_report(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    exact_row(
                        TEACHER_BOARD,
                        best_move="b1",
                        move_scores=all_legal_move_scores(TEACHER_BOARD, "b1"),
                    )
                ],
            )
            config = make_config(temp_path, labels, exact_labels=exact)

            result = trainer.train_pattern_tables(config, analyzer=fake_analyzer)

            self.assertTrue(result.candidate_eval_path.exists())
            self.assertTrue(result.validation_path.exists())
            self.assertTrue(result.report_path.exists())
            self.assertIn("mode=pattern_only", result.candidate_eval_path.read_text(encoding="utf-8"))
            self.assertIn("trained", result.validation_path.read_text(encoding="utf-8"))
            self.assertEqual(result.summary["rows"]["training_examples"], 1)
            self.assertEqual(result.summary["rows"]["soft_target_examples"], 1)
            for phase in trainer.PHASES:
                self.assertTrue(result.table_paths[phase].exists())

    def test_help_omits_removed_research_options(self) -> None:
        with mock.patch("sys.stdout", io.StringIO()) as stdout:
            with self.assertRaises(SystemExit):
                trainer.parse_args(["--help"])
        help_text = stdout.getvalue()
        self.assertIn("--exact-score-temperature", help_text)
        self.assertIn("--listwise-feature-cache-mode", help_text)
        for removed in (
            "--" + "objective",
            "--" + "loss",
            "--" + "pair" + "-mode",
            "--" + "pair" + "-weighting",
            "--" + "guard" + "-mode",
            "--" + "exact" + "-score-soft-target",
        ):
            self.assertNotIn(removed, help_text)


if __name__ == "__main__":
    unittest.main()
