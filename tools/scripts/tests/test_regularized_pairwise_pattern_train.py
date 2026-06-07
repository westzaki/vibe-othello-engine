from __future__ import annotations

import io
import json
import random
import re
import sys
import tempfile
import time
import unittest
from unittest import mock
from dataclasses import replace
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import regularized_pairwise_pattern_train as trainer  # noqa: E402
import pattern_symmetry_diagnostics as diagnostics  # noqa: E402
from pattern_training import analyzer as shared_analyzer  # noqa: E402
from common import ScriptError  # noqa: E402
from pattern_specs import PATTERN_SPECS, invert_board9_colors, pattern_index  # noqa: E402
from pattern_training.board9 import parse_board  # noqa: E402


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

INITIAL_BOARD = (
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
        "pattern_table=patterns/base.tsv\n"
        "opening.pattern_table=4\n"
        "midgame.pattern_table=4\n"
        "late.pattern_table=4\n"
        "opening_max_occupied=20\n"
        "midgame_max_occupied=44\n",
        encoding="utf-8",
    )
    return path


def write_phase_tables(table_dir: Path, opening: str) -> dict[str, Path]:
    table_dir.mkdir(parents=True, exist_ok=True)
    paths = {phase: table_dir / f"{phase}.tsv" for phase in trainer.PHASES}
    paths["opening"].write_text(opening, encoding="utf-8")
    for phase in ("midgame", "late"):
        paths[phase].write_text("corner_2x3\t0\t0\n", encoding="utf-8")
    return paths


def write_current_default_like_eval_config(path: Path) -> Path:
    path.write_text(
        "# schema_version: eval.v1\n"
        "name=current_default_fixture\n"
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
    selected_move: str | None = None,
    position_id: str | None = None,
    source_bucket: str | None = None,
) -> dict[str, object]:
    row: dict[str, object] = {
        "schema": "teacher_label.v1",
        "status": "ok",
        "legal_move_valid": True,
        "move_token_valid": True,
        "board_text": board,
        "move": move,
        "position_split": split,
        "position_id": position_id or f"{split}-{move}",
    }
    if selected_move is not None:
        row["selected_move"] = selected_move
    if source_bucket is not None:
        row["source_bucket"] = source_bucket
    return row


def exact_row(board: str, best_move: str) -> dict[str, object]:
    return {
        "schema": "exact_label.v1",
        "board": board,
        "best_move": best_move,
        "best_moves": [best_move],
    }


def exact_row_with_scores(
    board: str,
    *,
    best_move: str,
    move_scores: dict[str, int],
    exact_score: int = 16,
) -> dict[str, object]:
    return {
        "schema": "exact_label.v1",
        "board": board,
        "best_move": best_move,
        "best_moves": [best_move],
        "exact_score_side_to_move": exact_score,
        "move_scores": [
            {"move": move, "exact_score_side_to_move": score}
            for move, score in sorted(move_scores.items())
        ],
    }


def fake_analyzer(
    config: trainer.TrainerConfig, board_text: str
) -> trainer.AnalyzeResult:
    del config
    trainer.apply_move_to_board(board_text, "b1")
    return trainer.AnalyzeResult(best_move="b1", root_scores={"b1": 20, "d1": 10})


def agreeing_fake_analyzer(
    config: trainer.TrainerConfig, board_text: str
) -> trainer.AnalyzeResult:
    del config
    trainer.apply_move_to_board(board_text, "d1")
    trainer.apply_move_to_board(board_text, "b1")
    return trainer.AnalyzeResult(best_move="d1", root_scores={"d1": 20, "b1": 10})


def wide_fake_analyzer(
    config: trainer.TrainerConfig, board_text: str
) -> trainer.AnalyzeResult:
    del config
    for move in ("a2", "b1", "d1", "h1"):
        trainer.apply_move_to_board(board_text, move)
    return trainer.AnalyzeResult(
        best_move="a2",
        root_scores={"a2": 60, "d1": 50, "b1": 40, "h1": 30},
    )


def boundary_board(target_occupied: int) -> tuple[str, list[str]]:
    board = INITIAL_BOARD
    passes = 0
    for _ in range(200):
        if trainer.occupied_count(board) == target_occupied:
            legal_moves = sorted(move for move in trainer.legal_moves_for_board(board) if move != "pass")
            if len(legal_moves) < 2:
                raise AssertionError(f"expected at least two legal moves at occupied={target_occupied}")
            return board, legal_moves

        legal_moves = sorted(move for move in trainer.legal_moves_for_board(board) if move != "pass")
        if legal_moves:
            board = trainer.apply_move_to_board(board, legal_moves[0])
            passes = 0
        else:
            board = trainer.apply_move_to_board(board, "pass")
            passes += 1
            if passes > 1:
                raise AssertionError(f"game ended before occupied={target_occupied}")

    raise AssertionError(f"failed to reach occupied={target_occupied}")


def analyzer_with_ranked_above(
    teacher_move: str,
    other_move: str,
) -> trainer.AnalyzeResult:
    return trainer.AnalyzeResult(
        best_move=other_move,
        root_scores={teacher_move: 10, other_move: 20},
    )


def eager_train_weights(
    config: trainer.TrainerConfig,
    pairs: list[trainer.PreferencePair],
) -> tuple[trainer.WeightsByPhase, list[dict[str, object]]]:
    weights: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}
    history: list[dict[str, object]] = []
    if not pairs:
        for epoch in range(1, config.epochs + 1):
            history.append({"epoch": epoch, **trainer.evaluate_pairs(config, weights, pairs)})
        return weights, history

    rng = random.Random(config.seed)
    for epoch in range(1, config.epochs + 1):
        shuffled = list(pairs)
        rng.shuffle(shuffled)
        for pair in shuffled:
            phase_weights = weights[pair.phase]
            trainer.shrink_phase_weights(
                phase_weights,
                learning_rate=config.learning_rate,
                l2=config.l2,
            )
            margin = trainer.model_margin(config, weights, pair)
            if config.loss == "hinge":
                factor = 1.0 if margin < 1.0 else 0.0
            else:
                factor = trainer.stable_sigmoid_negative_margin(margin)
            if factor == 0.0:
                continue
            for key, value in pair.features.items():
                current = phase_weights.get(key, 0.0)
                updated = current + config.learning_rate * pair.pair_weight * factor * value
                phase_weights[key] = trainer.clamp_weight(updated, config.max_abs_weight)
        history.append({"epoch": epoch, **trainer.evaluate_pairs(config, weights, pairs)})
    return weights, history


def assert_weights_close(
    testcase: unittest.TestCase,
    actual: trainer.WeightsByPhase,
    expected: trainer.WeightsByPhase,
    *,
    places: int = 12,
) -> None:
    for phase in trainer.PHASES:
        actual_weights = actual.get(phase, {})
        expected_weights = expected.get(phase, {})
        testcase.assertEqual(set(actual_weights), set(expected_weights))
        for key in actual_weights:
            testcase.assertAlmostEqual(actual_weights[key], expected_weights[key], places=places)


def assert_history_close(
    testcase: unittest.TestCase,
    actual: list[dict[str, object]],
    expected: list[dict[str, object]],
    *,
    places: int = 12,
) -> None:
    testcase.assertEqual(len(actual), len(expected))
    for actual_epoch, expected_epoch in zip(actual, expected, strict=True):
        testcase.assertEqual(set(actual_epoch), set(expected_epoch))
        for key, actual_value in actual_epoch.items():
            expected_value = expected_epoch[key]
            if isinstance(actual_value, float):
                testcase.assertIsInstance(expected_value, float)
                testcase.assertAlmostEqual(actual_value, expected_value, places=places)
            else:
                testcase.assertEqual(actual_value, expected_value)


def synthetic_pair(
    *,
    phase: str,
    feature: trainer.FeatureKey,
    feature_delta: int = 1,
    preferred_move: str = "d1",
    other_move: str = "b1",
) -> trainer.PreferencePair:
    return trainer.PreferencePair(
        position_id=f"synthetic-{phase}-{feature[0]}-{feature[1]}",
        source_path=Path("synthetic.jsonl"),
        source_line=1,
        split="train",
        phase=phase,
        board_text=INITIAL_BOARD,
        teacher_move=preferred_move,
        engine_move=other_move,
        preferred_move=preferred_move,
        other_move=other_move,
        pair_kind="synthetic",
        pair_weight=1.0,
        bucket="__missing__",
        bucket_weight=1.0,
        features={feature: feature_delta},
        exact_best_moves=(),
        preferred_score=0,
        other_score=0,
        rank_margin=1,
        score_margin=0,
    )


def pair_list_for_training_comparison(
    config: trainer.TrainerConfig,
) -> list[trainer.PreferencePair]:
    pairs, _, _ = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)
    repeated: list[trainer.PreferencePair] = []
    for index in range(4):
        for pair in pairs:
            repeated.append(replace(pair, pair_weight=1.0 + index * 0.25))
    return repeated


def synthetic_listwise_example(
    *,
    target_move: str = "good",
    other_move: str = "bad",
    split: str = "validation",
    exact_root_score: int | None = 16,
) -> trainer.ListwiseExample:
    return trainer.ListwiseExample(
        position_id="synthetic-listwise",
        source_path=Path("synthetic.jsonl"),
        source_line=1,
        split=split,
        board_text=INITIAL_BOARD,
        teacher_move=other_move,
        engine_move=other_move,
        target_moves=(target_move,),
        exact_best_moves=(target_move,),
        exact_root_score=exact_root_score,
        candidates=(
            trainer.CandidateMove(
                move=target_move,
                phase="midgame",
                features={("edge_8", 2131): 1},
                exact_score=8,
            ),
            trainer.CandidateMove(
                move=other_move,
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


def boundary_pair_moves(
    config: trainer.TrainerConfig,
    board: str,
    moves: list[str],
) -> tuple[str, str]:
    for teacher_move in moves:
        teacher_child = trainer.apply_move_to_board(board, teacher_move)
        for other_move in moves:
            if other_move == teacher_move:
                continue
            other_child = trainer.apply_move_to_board(board, other_move)
            features = trainer.preference_delta(
                root_board_text=board,
                preferred_child_board=teacher_child,
                compared_child_board=other_child,
                families=config.families,
            )
            if features:
                return teacher_move, other_move
    raise AssertionError("expected at least one boundary move pair with non-empty features")


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
        str(temp_path / "runs" / "regularized-pairwise"),
        "--families",
        "legacy",
        "--split",
        "train",
        "--loss",
        "logistic",
        "--l2",
        "0.01",
        "--epochs",
        "3",
        "--learning-rate",
        "0.2",
        "--max-abs-weight",
        "2",
        "--seed",
        "7",
    ]
    if exact_labels is not None:
        args.extend(["--exact-labels", str(exact_labels)])
    if extra_args:
        args.extend(extra_args)
    parsed = trainer.parse_args(args)
    return trainer.config_from_args(
        parsed,
        invocation=["regularized_pairwise_pattern_train.py", *args],
    )


class RegularizedPairwisePatternTrainTests(unittest.TestCase):
    def test_eval_config_is_required(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])

            with mock.patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit):
                trainer.parse_args(
                    [
                        "--teacher-labels",
                        str(labels),
                        "--out-dir",
                        str(temp_path / "runs" / "missing-eval-config"),
                    ]
                )

    def test_dataset_reference_escape_rejection_comes_from_shared_resolver(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "datasets"
            root.mkdir()

            with self.assertRaises(ScriptError):
                trainer.parse_label_paths("dataset:../labels.jsonl", dataset_root=str(root))

    def test_relative_dataset_reference_resolves_under_dataset_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "datasets"
            root.mkdir()

            paths = trainer.parse_label_paths(
                "dataset:teacher/demo/train.jsonl,local.jsonl",
                dataset_root=str(root),
            )

        self.assertEqual(paths[0], (root / "teacher" / "demo" / "train.jsonl").resolve(strict=False))
        self.assertEqual(paths[1], Path("local.jsonl"))

    def test_post_training_symmetrize_default_leaves_generated_tsv_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels)
            table_paths = write_phase_tables(
                config.out_dir / "tables",
                "# schema_version: pattern_table.v1\nedge_8\t1\t5\nedge_8\t2187\t0\n",
            )
            before = {phase: path.read_text(encoding="utf-8") for phase, path in table_paths.items()}

            summary = trainer.apply_post_training_symmetrize(config, table_paths)

            self.assertFalse(summary["enabled"])
            self.assertEqual(summary["modes"], [])
            self.assertEqual(summary["phases"], {})
            after = {phase: path.read_text(encoding="utf-8") for phase, path in table_paths.items()}
            self.assertEqual(after, before)

    def test_post_training_symmetrize_reversed_index_averages_generated_tsv(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--post-training-symmetrize", "reversed-index"],
            )
            table_paths = write_phase_tables(
                config.out_dir / "tables",
                "edge_8\t1\t5\nedge_8\t2187\t1\n",
            )

            summary = trainer.apply_post_training_symmetrize(config, table_paths)
            weights = diagnostics.read_pattern_table(table_paths["opening"])

            self.assertTrue(summary["enabled"])
            self.assertEqual(summary["modes"], ["reversed-index"])
            self.assertEqual(weights["edge_8"][1], 3)
            self.assertEqual(weights["edge_8"][2187], 3)
            self.assertGreater(summary["phases"]["opening"]["changed_entries"], 0)
            self.assertIn("violations_before", summary["phases"]["opening"])
            self.assertIn("violations_after", summary["phases"]["opening"])

    def test_post_training_symmetrize_color_inversion_enforces_antisymmetry(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--post-training-symmetrize", "color-inversion"],
            )
            table_paths = write_phase_tables(
                config.out_dir / "tables",
                "edge_8\t1\t5\nedge_8\t2\t1\n",
            )

            trainer.apply_post_training_symmetrize(config, table_paths)
            weights = diagnostics.read_pattern_table(table_paths["opening"])

            self.assertEqual(weights["edge_8"][1], 2)
            self.assertEqual(weights["edge_8"][2], -2)

    def test_post_training_symmetrize_writes_sentinel_when_phase_table_becomes_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--post-training-symmetrize", "color-inversion"],
            )
            table_paths = write_phase_tables(
                config.out_dir / "tables",
                "edge_8\t1\t1\nedge_8\t2\t1\n",
            )

            summary = trainer.apply_post_training_symmetrize(config, table_paths)
            weights = diagnostics.read_pattern_table(table_paths["opening"])

            self.assertEqual(weights[trainer.SENTINEL_FAMILY][trainer.SENTINEL_ENTRY[0]], 0)
            self.assertEqual(sum(len(entries) for entries in weights.values()), 1)
            self.assertTrue(summary["phases"]["opening"]["sentinel_entry_written"])
            self.assertEqual(summary["phases"]["opening"]["entries_written"], 1)
            self.assertEqual(summary["phases"]["opening"]["zero_entries_introduced"], 3)

    def test_post_training_symmetrize_multiple_modes_are_order_independent(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            opening = "edge_8\t1\t5\nedge_8\t2\t1\nedge_8\t2187\t-3\nedge_8\t4374\t4\n"
            config_a = make_config(
                temp_path,
                labels,
                extra_args=["--post-training-symmetrize", "reversed-index,color-inversion"],
            )
            paths_a = write_phase_tables(config_a.out_dir / "tables-a", opening)
            config_b = replace(
                config_a,
                post_training_symmetrize_modes=("color-inversion", "reversed-index"),
            )
            paths_b = write_phase_tables(config_a.out_dir / "tables-b", opening)

            trainer.apply_post_training_symmetrize(config_a, paths_a)
            trainer.apply_post_training_symmetrize(config_b, paths_b)

            self.assertEqual(
                diagnostics.read_pattern_table(paths_a["opening"]),
                diagnostics.read_pattern_table(paths_b["opening"]),
            )

    def test_post_training_symmetrize_report_section_lists_before_after_violations(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--post-training-symmetrize", "reversed-index"],
            )
            table_paths = write_phase_tables(
                config.out_dir / "tables",
                "edge_8\t1\t5\nedge_8\t2187\t1\n",
            )
            post_summary = trainer.apply_post_training_symmetrize(config, table_paths)
            metrics = {
                "pairs": 0,
                "weighted_loss": 0.0,
                "unweighted_loss": 0.0,
                "accuracy": 0.0,
                "weighted_accuracy": 0.0,
                "avg_margin": 0.0,
                "total_pair_weight": 0.0,
            }
            summary = {
                "rows": {},
                "training": {
                    "initial_metrics": metrics,
                    "final_metrics": metrics,
                    "listwise_compact": {},
                },
                "analysis_cache_mode": "off",
                "analysis_cache_dir": "",
                "analysis_jobs": 1,
                "diagnostics": {},
                "post_training_symmetrize": post_summary,
            }

            report = trainer.render_report(
                config=config,
                summary=summary,
                table_paths=table_paths,
                candidate_eval_path=config.out_dir / "candidate.eval",
                validation_path=config.out_dir / "validation.tsv",
            )

            self.assertIn("## Post-Training Symmetrize", report)
            self.assertIn("violations before", report)
            self.assertIn("violations after", report)
            self.assertIn("reversed-index", report)

    def test_post_training_symmetrize_rejects_source_controlled_data_eval_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])

            with self.assertRaisesRegex(ScriptError, "--out-dir must not be under source-controlled data/"):
                make_config(
                    temp_path,
                    labels,
                    extra_args=[
                        "--out-dir",
                        str(trainer.REPO_ROOT / "data" / "eval" / "post-training-symmetrize-test"),
                        "--post-training-symmetrize",
                        "reversed-index",
                    ],
                )

    def test_pattern_indexes_match_existing_trainer_convention(self) -> None:
        side = "B"
        rows = trainer.board9_rows_to_square_index_rows(trainer.parse_board(TEACHER_BOARD)[0])
        shared_rows = trainer.board9_rows_to_square_index_rows(parse_board(TEACHER_BOARD)[0])

        for family in trainer.FAMILY_ORDER:
            self.assertEqual(trainer.PATTERN_SPECS[family], PATTERN_SPECS[family])
            for spec in trainer.PATTERN_SPECS[family]:
                self.assertEqual(
                    trainer.pattern_index(rows, side, spec),
                    pattern_index(shared_rows, side, spec),
                )

    def test_pattern_counts_convert_board9_display_to_square_index_order(self) -> None:
        square_index_rows = [
            "BWB.WB.W",
            "WB..BW.B",
            "..W.B...",
            "...BW...",
            "...WB...",
            "...B.W..",
            "B.W..WB.",
            "W.BW.BWB",
        ]
        board9 = "\n".join(reversed(square_index_rows)) + "\nside=B"

        counts = trainer.pattern_counts(board9, "B", ("corner_2x3", "edge_8"))

        self.assertEqual(counts[("corner_2x3", 151)], 1)
        self.assertEqual(counts[("edge_8", 4795)], 1)

    def test_apply_move_uses_board9_row_orientation(self) -> None:
        expected = (
            ".BBBBBB.\n"
            ".BBWWB..\n"
            "BWWWWWWW\n"
            "BWWBBBBB\n"
            "WWBWBBBB\n"
            "WWWBBBB.\n"
            ".WWBBWWW\n"
            "W.BBBBB.\n"
            "side=W"
        )

        self.assertEqual(trainer.apply_move_to_board(TEACHER_BOARD, "d1"), expected)

    def test_apply_move_and_preference_delta_create_pairwise_delta(self) -> None:
        teacher_child = trainer.apply_move_to_board(TEACHER_BOARD, "d1")
        engine_child = trainer.apply_move_to_board(TEACHER_BOARD, "b1")

        features = trainer.preference_delta(
            root_board_text=TEACHER_BOARD,
            preferred_child_board=teacher_child,
            compared_child_board=engine_child,
            families=("edge_8", "corner_2x3"),
        )

        self.assertTrue(features)
        self.assertIn("opening", trainer.PHASES)

    def test_run_analysis_retries_transient_spawn_resource_errors(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels)
            completed = shared_analyzer.subprocess.CompletedProcess(
                args=trainer.analyze_command(config),
                returncode=0,
                stdout="best_move: d1\nroot_candidates:\n  - move: d1\n    score: 3\n",
                stderr="",
            )

            with mock.patch.object(
                shared_analyzer.subprocess,
                "run",
                side_effect=[
                    BlockingIOError(shared_analyzer.errno.EAGAIN, "Resource temporarily unavailable"),
                    completed,
                ],
            ), mock.patch.object(shared_analyzer.time, "sleep") as sleep:
                result = trainer.run_analysis(config, TEACHER_BOARD)

        self.assertEqual(result.best_move, "d1")
        self.assertEqual(result.root_scores, {"d1": 3})
        sleep.assert_called_once()

    def test_preference_delta_matches_negated_child_side_search_score_delta(self) -> None:
        teacher_child = trainer.apply_move_to_board(TEACHER_BOARD, "d1")
        engine_child = trainer.apply_move_to_board(TEACHER_BOARD, "b1")
        _, root_side = trainer.parse_board(TEACHER_BOARD)
        child_side = trainer.opponent(root_side)

        features = trainer.preference_delta(
            root_board_text=TEACHER_BOARD,
            preferred_child_board=teacher_child,
            compared_child_board=engine_child,
            families=("edge_8",),
        )
        teacher_counts = trainer.pattern_counts(teacher_child, child_side, ("edge_8",))
        engine_counts = trainer.pattern_counts(engine_child, child_side, ("edge_8",))
        expected = {
            key: engine_counts[key] - teacher_counts[key]
            for key in set(teacher_counts) | set(engine_counts)
            if engine_counts[key] - teacher_counts[key]
        }

        self.assertEqual(features, expected)

    def test_root_move_feature_convention_negates_child_side_counts(self) -> None:
        preferred_child = trainer.apply_move_to_board(TEACHER_BOARD, "d1")
        compared_child = trainer.apply_move_to_board(TEACHER_BOARD, "b1")
        _, root_side = trainer.parse_board(TEACHER_BOARD)
        child_side = trainer.opponent(root_side)
        families = ("edge_8", "corner_2x3")

        preferred_counts = trainer.pattern_counts(preferred_child, child_side, families)
        compared_counts = trainer.pattern_counts(compared_child, child_side, families)
        preferred_root_features = trainer.root_move_features(
            root_board_text=TEACHER_BOARD,
            child_board_text=preferred_child,
            families=families,
        )
        compared_root_features = trainer.root_move_features(
            root_board_text=TEACHER_BOARD,
            child_board_text=compared_child,
            families=families,
        )

        self.assertEqual(
            preferred_root_features,
            {key: -value for key, value in preferred_counts.items() if value},
        )
        self.assertEqual(
            compared_root_features,
            {key: -value for key, value in compared_counts.items() if value},
        )

        weights = {key: float((index % 5) + 1) for index, key in enumerate(sorted(set(preferred_counts) | set(compared_counts)))}
        preferred_child_eval = sum(weights.get(key, 0.0) * value for key, value in preferred_counts.items())
        compared_child_eval = sum(weights.get(key, 0.0) * value for key, value in compared_counts.items())
        preferred_root_score = sum(
            weights.get(key, 0.0) * value for key, value in preferred_root_features.items()
        )
        compared_root_score = sum(
            weights.get(key, 0.0) * value for key, value in compared_root_features.items()
        )

        self.assertEqual(preferred_root_score, -preferred_child_eval)
        self.assertEqual(compared_root_score, -compared_child_eval)
        self.assertGreater(
            preferred_root_score - compared_root_score,
            0.0,
            "lowering the preferred child-side eval raises the preferred root move",
        )

        compared_minus_preferred = {
            key: compared_counts[key] - preferred_counts[key]
            for key in set(preferred_counts) | set(compared_counts)
            if compared_counts[key] - preferred_counts[key]
        }
        preferred_minus_compared = {
            key: preferred_counts[key] - compared_counts[key]
            for key in set(preferred_counts) | set(compared_counts)
            if preferred_counts[key] - compared_counts[key]
        }
        features = trainer.preference_delta(
            root_board_text=TEACHER_BOARD,
            preferred_child_board=preferred_child,
            compared_child_board=compared_child,
            families=families,
        )

        self.assertEqual(features, compared_minus_preferred)
        self.assertEqual(
            sum(weights.get(key, 0.0) * value for key, value in features.items()),
            preferred_root_score - compared_root_score,
        )
        self.assertEqual(
            {key: -value for key, value in preferred_minus_compared.items()},
            compared_minus_preferred,
        )

    def test_preference_delta_is_side_relative_under_color_inversion(self) -> None:
        teacher_child = trainer.apply_move_to_board(TEACHER_BOARD, "d1")
        engine_child = trainer.apply_move_to_board(TEACHER_BOARD, "b1")
        inverted_root = invert_board9_colors(TEACHER_BOARD)
        inverted_teacher_child = trainer.apply_move_to_board(inverted_root, "d1")
        inverted_engine_child = trainer.apply_move_to_board(inverted_root, "b1")

        features = trainer.preference_delta(
            root_board_text=TEACHER_BOARD,
            preferred_child_board=teacher_child,
            compared_child_board=engine_child,
            families=("edge_8", "corner_2x3", "diagonal_8"),
        )
        inverted_features = trainer.preference_delta(
            root_board_text=inverted_root,
            preferred_child_board=inverted_teacher_child,
            compared_child_board=inverted_engine_child,
            families=("edge_8", "corner_2x3", "diagonal_8"),
        )

        self.assertTrue(features)
        self.assertEqual(inverted_features, features)

    def test_teacher_child_other_child_delta_sign_matches_root_move_preference(self) -> None:
        preferred_child = trainer.apply_move_to_board(TEACHER_BOARD, "d1")
        other_child = trainer.apply_move_to_board(TEACHER_BOARD, "b1")
        features = trainer.preference_delta(
            root_board_text=TEACHER_BOARD,
            preferred_child_board=preferred_child,
            compared_child_board=other_child,
            families=("edge_8",),
        )
        weights: trainer.WeightsByPhase = {
            phase: {} for phase in trainer.PHASES
        }
        phase = trainer.phase_for_board(preferred_child, trainer.PhaseCutoffs(20, 44))
        weights[phase] = {("edge_8", 2131): 5.0}
        pair = synthetic_pair(
            phase=phase,
            feature=("edge_8", 2131),
            preferred_move="d1",
            other_move="b1",
        )
        pair = replace(
            pair,
            board_text=TEACHER_BOARD,
            features=features,
        )

        # A positive child-side value on the other child should increase the
        # preferred root margin because root scores negate child scores.
        self.assertEqual(features[("edge_8", 2131)], 1)
        self.assertGreater(trainer.pair_margin(weights, pair), 0.0)

    def test_best_vs_engine_pair_generation_preserves_original_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels)

            pairs, records, stats = trainer.collect_pairs(config, analyzer=fake_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].preferred_move, "d1")
        self.assertEqual(pairs[0].other_move, "b1")
        self.assertEqual(pairs[0].pair_kind, "teacher")
        self.assertEqual(pairs[0].pair_weight, 1.0)
        self.assertEqual(stats["preference_pairs"], 1)
        self.assertEqual(stats["guard_pairs"], 0)
        self.assertTrue(any(record.status == "paired" for record in records))
        self.assertEqual(stats["analysis_cache_mode"], "off")
        self.assertEqual(stats["analysis_jobs"], 1)

    def test_best_vs_engine_reuses_label_engine_move_without_analysis(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(
                temp_path / "labels.jsonl",
                [teacher_row(selected_move=None) | {"engine_move": "b1"}],
            )
            config = make_config(temp_path, labels)

            def failing_analyzer(
                inner_config: trainer.TrainerConfig,
                board_text: str,
            ) -> trainer.AnalyzeResult:
                del inner_config, board_text
                raise AssertionError("precomputed engine_move should avoid analysis")

            pairs, _, stats = trainer.collect_pairs(config, analyzer=failing_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].engine_move, "b1")
        self.assertEqual(stats["analysis_cache_misses"], 0)

    def test_analysis_cache_read_write_writes_then_hits(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            cache_dir = temp_path / "runs" / "analysis-cache"
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
                inner_config: trainer.TrainerConfig,
                board_text: str,
            ) -> trainer.AnalyzeResult:
                nonlocal calls
                calls += 1
                return fake_analyzer(inner_config, board_text)

            first_pairs, _, first_stats = trainer.collect_pairs(config, analyzer=counting_analyzer)
            second_pairs, _, second_stats = trainer.collect_pairs(config, analyzer=counting_analyzer)

        self.assertEqual(len(first_pairs), 1)
        self.assertEqual(len(second_pairs), 1)
        self.assertEqual(calls, 1)
        self.assertEqual(first_stats["analysis_cache_hits"], 0)
        self.assertEqual(first_stats["analysis_cache_misses"], 1)
        self.assertEqual(first_stats["analysis_cache_writes"], 1)
        self.assertEqual(second_stats["analysis_cache_hits"], 1)
        self.assertEqual(second_stats["analysis_cache_misses"], 0)

    def test_analysis_cache_stale_eval_config_hash_is_not_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            cache_dir = temp_path / "runs" / "analysis-cache"
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
                inner_config: trainer.TrainerConfig,
                board_text: str,
            ) -> trainer.AnalyzeResult:
                nonlocal calls
                calls += 1
                return fake_analyzer(inner_config, board_text)

            trainer.collect_pairs(config, analyzer=counting_analyzer)
            config.eval_config.write_text(
                config.eval_config.read_text(encoding="utf-8") + "late.corner=1\n",
                encoding="utf-8",
            )
            _, _, stale_stats = trainer.collect_pairs(config, analyzer=counting_analyzer)

        self.assertEqual(calls, 2)
        self.assertEqual(stale_stats["analysis_cache_hits"], 0)
        self.assertEqual(stale_stats["analysis_cache_misses"], 1)

    def test_analysis_cache_stale_analyzer_hash_is_not_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            cache_dir = temp_path / "runs" / "analysis-cache"
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
            config.analyze_position.write_text("fake analyzer v1\n", encoding="utf-8")
            calls = 0

            def counting_analyzer(
                inner_config: trainer.TrainerConfig,
                board_text: str,
            ) -> trainer.AnalyzeResult:
                nonlocal calls
                calls += 1
                return fake_analyzer(inner_config, board_text)

            trainer.collect_pairs(config, analyzer=counting_analyzer)
            config.analyze_position.write_text("fake analyzer v2\n", encoding="utf-8")
            _, _, stale_stats = trainer.collect_pairs(config, analyzer=counting_analyzer)
            cache_rows = [
                json.loads(line)
                for line in (cache_dir / "root_analysis.jsonl").read_text(encoding="utf-8").splitlines()
            ]

        self.assertEqual(calls, 2)
        self.assertEqual(stale_stats["analysis_cache_hits"], 0)
        self.assertEqual(stale_stats["analysis_cache_misses"], 1)
        self.assertEqual(stale_stats["analysis_cache_writes"], 1)
        self.assertEqual(len(cache_rows), 2)
        self.assertNotEqual(
            cache_rows[0]["analyze_position_hash"],
            cache_rows[1]["analyze_position_hash"],
        )

    def test_analysis_cache_read_only_fails_when_missing(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--analysis-cache-dir",
                    str(temp_path / "runs" / "analysis-cache"),
                    "--analysis-cache-mode",
                    "read-only",
                ],
            )

            with self.assertRaisesRegex(ScriptError, "analysis cache missing"):
                trainer.collect_pairs(config, analyzer=fake_analyzer)

    def test_analysis_cache_refresh_ignores_existing_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            cache_dir = temp_path / "runs" / "analysis-cache"
            warm_config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--analysis-cache-dir",
                    str(cache_dir),
                    "--analysis-cache-mode",
                    "read-write",
                ],
            )
            warm_config.analyze_position.write_text("fake analyzer\n", encoding="utf-8")
            refresh_config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--analysis-cache-dir",
                    str(cache_dir),
                    "--analysis-cache-mode",
                    "refresh",
                ],
            )
            calls = 0

            def counting_analyzer(
                inner_config: trainer.TrainerConfig,
                board_text: str,
            ) -> trainer.AnalyzeResult:
                nonlocal calls
                calls += 1
                return fake_analyzer(inner_config, board_text)

            trainer.collect_pairs(warm_config, analyzer=counting_analyzer)
            _, _, refresh_stats = trainer.collect_pairs(refresh_config, analyzer=counting_analyzer)

        self.assertEqual(calls, 2)
        self.assertEqual(refresh_stats["analysis_cache_hits"], 0)
        self.assertEqual(refresh_stats["analysis_cache_misses"], 1)
        self.assertEqual(refresh_stats["analysis_cache_writes"], 1)

    def test_parallel_analysis_preserves_deterministic_pair_order(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(
                temp_path / "labels.jsonl",
                [
                    teacher_row(position_id="first"),
                    teacher_row(position_id="second"),
                    teacher_row(position_id="third"),
                ],
            )
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--analysis-jobs",
                    "3",
                ],
            )

            def slow_analyzer(
                inner_config: trainer.TrainerConfig,
                board_text: str,
            ) -> trainer.AnalyzeResult:
                del inner_config
                time.sleep(0.01)
                return wide_fake_analyzer(config, board_text)

            pairs, _, stats = trainer.collect_pairs(config, analyzer=slow_analyzer)

        self.assertEqual(stats["analysis_jobs"], 3)
        self.assertEqual([pair.source_line for pair in pairs], [1, 1, 1, 2, 2, 2, 3, 3, 3])

    def test_best_vs_all_pair_generation_uses_root_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels, extra_args=["--pair-mode", "best-vs-all"])

            pairs, _, stats = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        self.assertEqual({pair.other_move for pair in pairs}, {"a2", "b1", "h1"})
        self.assertTrue(all(pair.preferred_move == "d1" for pair in pairs))
        self.assertEqual(stats["preference_pairs"], 3)
        self.assertEqual(stats["avg_pairs_per_position"], 3.0)

    def test_batch_analysis_runner_feeds_collect_pairs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--pair-mode", "best-vs-all", "--analysis-runner", "batch"],
            )

            def fake_batch_analysis(
                analyzer_config: shared_analyzer.AnalyzerConfig,
                requests: object,
            ):
                self.assertEqual(analyzer_config.analyze_position, config.analyze_position)
                rows = list(requests)
                self.assertEqual(len(rows), 1)
                cache_key, board_text = rows[0]
                self.assertEqual(board_text, TEACHER_BOARD)
                yield cache_key, wide_fake_analyzer(config, board_text)

            with mock.patch.object(
                trainer,
                "shared_run_batch_analysis",
                side_effect=fake_batch_analysis,
            ) as batch:
                pairs, _, stats = trainer.collect_pairs(config, analyzer=fake_analyzer)

        self.assertEqual({pair.other_move for pair in pairs}, {"a2", "b1", "h1"})
        self.assertEqual(stats["analysis_cache_misses"], 1)
        batch.assert_called_once()

    def test_rank_weighted_pair_generation_uses_lower_ranked_candidates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "rank-weighted",
                    "--pair-weighting",
                    "rank-margin",
                ],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        self.assertEqual({pair.other_move for pair in pairs}, {"b1", "h1"})
        self.assertNotIn("a2", {pair.other_move for pair in pairs})
        self.assertTrue(all(pair.rank_margin and pair.rank_margin > 0 for pair in pairs))
        self.assertTrue(all(pair.pair_weight > 1.0 for pair in pairs))
        self.assertEqual(stats["teacher_pairs"], 2)

    def test_teacher_vs_ranked_above_uses_candidates_scored_above_teacher(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "teacher-vs-ranked-above",
                    "--pair-weighting",
                    "score-margin",
                ],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        self.assertEqual({pair.other_move for pair in pairs}, {"a2"})
        self.assertTrue(all(pair.preferred_move == "d1" for pair in pairs))
        self.assertTrue(all(pair.other_score and pair.preferred_score for pair in pairs))
        self.assertTrue(all(pair.other_score > pair.preferred_score for pair in pairs))
        self.assertEqual(stats["teacher_pairs"], 1)

    def test_base_agreement_guard_creates_guard_pair_when_base_matches_teacher(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--guard-mode", "base-agreement"],
            )

            pairs, records, stats = trainer.collect_pairs(config, analyzer=agreeing_fake_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].pair_kind, "guard")
        self.assertEqual(pairs[0].preferred_move, "d1")
        self.assertEqual(pairs[0].other_move, "b1")
        self.assertEqual(pairs[0].pair_weight, 0.25)
        self.assertEqual(stats["guard_pairs"], 1)
        self.assertEqual(stats["guarded_positions"], 1)
        self.assertEqual(stats["main_preference_pairs"], 0)
        self.assertEqual(stats["guard_pair_weight_mass"], 0.25)
        self.assertTrue(any(record.status == "paired" and record.pair_kind == "guard" for record in records))

    def test_base_agreement_guard_skips_when_base_disagrees_with_teacher(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--guard-mode", "base-agreement"],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=fake_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].pair_kind, "teacher")
        self.assertEqual(stats["teacher_pairs"], 1)
        self.assertEqual(stats["guard_pairs"], 0)
        self.assertEqual(stats["guard_base_disagreement_skipped"], 1)

    def test_guard_pair_weight_uses_guard_weight_and_bucket_weight(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(
                temp_path / "labels.jsonl",
                [teacher_row(source_bucket="base_guard")],
            )
            weights = temp_path / "bucket_weights.json"
            weights.write_text(json.dumps({"base_guard": 2.5}), encoding="utf-8")
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--guard-mode",
                    "base-agreement",
                    "--guard-weight",
                    "0.4",
                    "--bucket-weights",
                    str(weights),
                ],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=agreeing_fake_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].pair_kind, "guard")
        self.assertEqual(pairs[0].bucket, "base_guard")
        self.assertEqual(pairs[0].bucket_weight, 2.5)
        self.assertEqual(pairs[0].pair_weight, 1.0)
        self.assertEqual(stats["guard_pair_weight_mass"], 1.0)
        self.assertEqual(stats["bucket_pair_weight_mass"], {"base_guard": 1.0})

    def test_guard_summary_records_counts_and_mass(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--guard-mode", "base-agreement", "--epochs", "1", "--l2", "0"],
            )

            result = trainer.train_pairwise_tables(config, analyzer=agreeing_fake_analyzer)

        self.assertEqual(result.summary["rows"]["guard_pairs"], 1)
        self.assertEqual(result.summary["rows"]["guard_pair_weight_mass"], 0.25)
        self.assertEqual(result.summary["guard"]["mode"], "base-agreement")
        self.assertEqual(result.summary["guard"]["weight"], 0.25)

    def test_guard_pair_uses_child_phase_for_boundary_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            board, moves = boundary_board(20)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=moves[0])])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--guard-mode", "base-agreement"],
            )
            teacher_move, other_move = boundary_pair_moves(config, board, moves)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=teacher_move)])

            pairs, _, stats = trainer.collect_pairs(
                config,
                analyzer=lambda _config, _board: trainer.AnalyzeResult(
                    best_move=teacher_move,
                    root_scores={teacher_move: 20, other_move: 10},
                ),
            )

        self.assertEqual(trainer.phase_for_board(board, config.phase_cutoffs), "opening")
        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].pair_kind, "guard")
        self.assertEqual(pairs[0].phase, "midgame")
        self.assertEqual(stats["midgame_pairs"], 1)

    def test_bucket_weight_multiplies_pair_weight(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(
                temp_path / "labels.jsonl",
                [teacher_row(source_bucket="teacher_rank_regression")],
            )
            weights = temp_path / "bucket_weights.json"
            weights.write_text(json.dumps({"teacher_rank_regression": 2.5}), encoding="utf-8")
            config = make_config(
                temp_path,
                labels,
                extra_args=["--bucket-weights", str(weights)],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=fake_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].bucket, "teacher_rank_regression")
        self.assertEqual(pairs[0].bucket_weight, 2.5)
        self.assertEqual(pairs[0].pair_weight, 2.5)
        self.assertEqual(stats["bucket_pair_counts"], {"teacher_rank_regression": 1})
        self.assertEqual(stats["bucket_pair_weight_mass"], {"teacher_rank_regression": 2.5})

    def test_missing_bucket_uses_default_bucket_weight(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            weights = temp_path / "bucket_weights.json"
            weights.write_text(json.dumps({"other_bucket": 3.0}), encoding="utf-8")
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--bucket-weights",
                    str(weights),
                    "--default-bucket-weight",
                    "0.25",
                ],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=fake_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].bucket, "__missing__")
        self.assertEqual(pairs[0].bucket_weight, 0.25)
        self.assertEqual(pairs[0].pair_weight, 0.25)
        self.assertEqual(stats["bucket_pair_counts"], {"__missing__": 1})
        self.assertEqual(stats["bucket_pair_weight_mass"], {"__missing__": 0.25})

    def test_bucket_weight_summary_records_counts_and_mass(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(
                temp_path / "labels.jsonl",
                [teacher_row(source_bucket="promoted_legacy_strong_disagreement")],
            )
            weights = temp_path / "bucket_weights.json"
            weights.write_text(
                json.dumps({"promoted_legacy_strong_disagreement": 1.5}),
                encoding="utf-8",
            )
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--bucket-weights",
                    str(weights),
                    "--epochs",
                    "1",
                    "--l2",
                    "0",
                ],
            )

            result = trainer.train_pairwise_tables(config, analyzer=fake_analyzer)

        self.assertEqual(
            result.summary["rows"]["bucket_pair_counts"],
            {"promoted_legacy_strong_disagreement": 1},
        )
        self.assertEqual(
            result.summary["rows"]["bucket_pair_weight_mass"],
            {"promoted_legacy_strong_disagreement": 1.5},
        )
        self.assertEqual(result.summary["bucket_weighting"]["field"], "source_bucket")
        self.assertEqual(
            result.summary["bucket_weighting"]["weights"],
            {"promoted_legacy_strong_disagreement": 1.5},
        )

    def test_no_bucket_weights_preserves_default_pair_weight(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels)

            pairs, _, stats = trainer.collect_pairs(config, analyzer=fake_analyzer)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].pair_weight, 1.0)
        self.assertEqual(pairs[0].bucket_weight, 1.0)
        self.assertEqual(stats["bucket_pair_counts"], {"__missing__": 1})
        self.assertEqual(stats["bucket_pair_weight_mass"], {"__missing__": 1.0})

    def test_exact_aware_pair_generation_prefers_exact_best_moves(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            exact = write_jsonl(temp_path / "exact.jsonl", [exact_row(TEACHER_BOARD, "b1")])
            config = make_config(
                temp_path,
                labels,
                exact_labels=exact,
                extra_args=[
                    "--pair-mode",
                    "exact-aware",
                    "--pair-weighting",
                    "exact-boost",
                    "--exact-best-weight",
                    "2.5",
                ],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        self.assertEqual({pair.preferred_move for pair in pairs}, {"b1"})
        self.assertEqual({pair.other_move for pair in pairs}, {"a2", "d1", "h1"})
        self.assertTrue(all(pair.pair_kind == "exact" for pair in pairs))
        self.assertTrue(all(pair.pair_weight == 2.5 for pair in pairs))
        self.assertEqual(stats["exact_aware_pairs"], 3)
        self.assertEqual(stats["teacher_exact_disagreements_used_by_exact_aware"], 1)
        self.assertEqual(stats["teacher_exact_disagreements_skipped"], 0)

    def test_exact_aware_only_when_available_skips_teacher_fallback_pairs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "exact-aware",
                    "--exact-aware-only-when-available",
                ],
            )
            calls = 0

            def counting_analyzer(
                config: trainer.TrainerConfig, board_text: str
            ) -> trainer.AnalyzeResult:
                nonlocal calls
                calls += 1
                return wide_fake_analyzer(config, board_text)

            pairs, records, stats = trainer.collect_pairs(config, analyzer=counting_analyzer)

        self.assertEqual(pairs, [])
        self.assertEqual(calls, 0)
        self.assertEqual(stats["exact_unavailable_fallback_positions"], 1)
        self.assertEqual(stats["no_pair_generated_skipped"], 1)
        self.assertEqual(stats["analysis_cache_misses"], 0)
        self.assertTrue(any(record.status == "exact_unavailable" for record in records))

    def test_dataset_diagnostics_keep_mixed_source_bucket_pair_counts_separate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(
                temp_path / "labels.jsonl",
                [
                    teacher_row(position_id="bucket-a", source_bucket="A"),
                    teacher_row(position_id="bucket-b", source_bucket="B"),
                ],
            )
            exact = write_jsonl(temp_path / "exact.jsonl", [exact_row(TEACHER_BOARD, "b1")])
            config = make_config(
                temp_path,
                labels,
                exact_labels=exact,
                extra_args=[
                    "--pair-mode",
                    "exact-aware",
                ],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        self.assertEqual(len(pairs), 6)
        diagnostics = stats["dataset_diagnostics"]["by_source_bucket"]
        self.assertEqual(diagnostics["A"]["pair_count_distribution"], {"3": 1})
        self.assertEqual(diagnostics["B"]["pair_count_distribution"], {"3": 1})
        self.assertEqual(diagnostics["A"]["pair_weight_mass"], 3.0)
        self.assertEqual(diagnostics["B"]["pair_weight_mass"], 3.0)
        self.assertEqual(diagnostics["A"]["engine_not_in_exact_best"], 1)
        self.assertEqual(diagnostics["B"]["engine_not_in_exact_best"], 1)
        self.assertEqual(diagnostics["A"]["teacher_rank_distribution"], {"2": 1})
        self.assertEqual(diagnostics["B"]["teacher_rank_distribution"], {"2": 1})
        self.assertEqual(diagnostics["A"]["exact_best_not_in_engine_top_group"], 1)
        self.assertEqual(diagnostics["B"]["exact_best_not_in_engine_top_group"], 1)
        self.assertNotEqual(diagnostics["B"]["pair_weight_mass"], 6.0)

    def test_max_top_group_size_filter_skips_noisy_exact_rows(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    {
                        "schema": "exact_label.v1",
                        "board": TEACHER_BOARD,
                        "best_move": "d1",
                        "best_moves": ["a2", "b1", "d1"],
                    }
                ],
            )
            config = make_config(
                temp_path,
                labels,
                exact_labels=exact,
                extra_args=["--max-top-group-size-for-training", "2"],
            )

            pairs, records, stats = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        self.assertEqual(pairs, [])
        self.assertEqual(stats["large_exact_top_group_skipped"], 1)
        self.assertTrue(any(record.status == "large_exact_top_group" for record in records))

    def test_max_pairs_per_position_caps_generated_pairs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--max-pairs-per-position",
                    "2",
                ],
            )

            pairs, _, stats = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        self.assertEqual(len(pairs), 2)
        self.assertEqual(stats["max_pairs_truncated"], 1)
        self.assertEqual(stats["pairs_truncated"], 1)

    def test_pair_weights_affect_training_updates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels, extra_args=["--epochs", "1", "--l2", "0"])
            pairs, _, _ = trainer.collect_pairs(config, analyzer=fake_analyzer)
            light = replace(pairs[0], pair_weight=1.0)
            heavy = replace(pairs[0], pair_weight=3.0)

            light_weights, _ = trainer.train_weights(config, [light])
            heavy_weights, _ = trainer.train_weights(config, [heavy])

        key = next(iter(light.features))
        self.assertGreater(abs(heavy_weights[light.phase][key]), abs(light_weights[light.phase][key]))

    def test_lazy_l2_matches_eager_exactly_when_l2_is_zero(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "3",
                    "--l2",
                    "0",
                ],
            )
            pairs = pair_list_for_training_comparison(config)

            lazy_weights, lazy_history = trainer.train_weights(config, pairs)
            eager_weights, eager_history = eager_train_weights(config, pairs)

        self.assertEqual(lazy_weights, eager_weights)
        self.assertEqual(lazy_history, eager_history)

    def test_lazy_l2_matches_eager_logistic_with_regularization(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--loss",
                    "logistic",
                    "--epochs",
                    "4",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                ],
            )
            pairs = pair_list_for_training_comparison(config)

            lazy_weights, lazy_history = trainer.train_weights(config, pairs)
            eager_weights, eager_history = eager_train_weights(config, pairs)

        assert_weights_close(self, lazy_weights, eager_weights)
        assert_history_close(self, lazy_history, eager_history)

    def test_lazy_l2_matches_eager_hinge_with_regularization(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--loss",
                    "hinge",
                    "--epochs",
                    "4",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                ],
            )
            pairs = pair_list_for_training_comparison(config)

            lazy_weights, lazy_history = trainer.train_weights(config, pairs)
            eager_weights, eager_history = eager_train_weights(config, pairs)

        assert_weights_close(self, lazy_weights, eager_weights)
        assert_history_close(self, lazy_history, eager_history)

    def test_lazy_l2_clamping_matches_eager_behavior(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "3",
                    "--learning-rate",
                    "0.5",
                    "--l2",
                    "0.02",
                    "--max-abs-weight",
                    "0.01",
                ],
            )
            pairs = pair_list_for_training_comparison(config)

            lazy_weights, lazy_history = trainer.train_weights(config, pairs)
            eager_weights, eager_history = eager_train_weights(config, pairs)

        assert_weights_close(self, lazy_weights, eager_weights)
        assert_history_close(self, lazy_history, eager_history)
        self.assertTrue(
            any(
                abs(value) >= config.max_abs_weight
                for phase_weights in lazy_weights.values()
                for value in phase_weights.values()
            )
        )

    def test_lazy_l2_output_tables_and_report_metrics_match_eager_helper(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            lazy_config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--out-dir",
                    str(temp_path / "runs" / "lazy"),
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "3",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                    "--output-scale",
                    "8",
                ],
            )
            eager_config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--out-dir",
                    str(temp_path / "runs" / "eager"),
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "3",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                    "--output-scale",
                    "8",
                ],
            )
            original_train_weights = trainer.train_weights
            try:
                lazy_result = trainer.train_pairwise_tables(lazy_config, analyzer=wide_fake_analyzer)
                trainer.train_weights = eager_train_weights  # type: ignore[assignment]
                eager_result = trainer.train_pairwise_tables(eager_config, analyzer=wide_fake_analyzer)
                lazy_tables = {
                    phase: lazy_result.table_paths[phase].read_text(encoding="utf-8")
                    for phase in trainer.PHASES
                }
                eager_tables = {
                    phase: eager_result.table_paths[phase].read_text(encoding="utf-8")
                    for phase in trainer.PHASES
                }
                lazy_report = lazy_result.report_path.read_text(encoding="utf-8")
            finally:
                trainer.train_weights = original_train_weights  # type: ignore[assignment]

        for phase in trainer.PHASES:
            self.assertEqual(lazy_tables[phase], eager_tables[phase])
        self.assertEqual(
            lazy_result.summary["training"]["initial_metrics"],
            eager_result.summary["training"]["initial_metrics"],
        )
        assert_history_close(
            self,
            lazy_result.summary["training"]["history"],
            eager_result.summary["training"]["history"],
        )
        self.assertEqual(
            set(lazy_result.summary["training"]["final_metrics"]),
            {
                "pairs",
                "loss",
                "weighted_loss",
                "unweighted_loss",
                "accuracy",
                "weighted_accuracy",
                "avg_margin",
                "total_pair_weight",
                "avg_pair_weight",
            },
        )
        self.assertIn("## Pair Metrics", lazy_report)

    def test_pair_cache_hit_skips_pair_generation_analysis(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--pair-mode", "best-vs-all", "--epochs", "1", "--l2", "0"],
            )

            first = trainer.train_pairwise_tables(config, analyzer=wide_fake_analyzer)

            def failing_analyzer(
                inner_config: trainer.TrainerConfig,
                board_text: str,
            ) -> trainer.AnalyzeResult:
                del inner_config, board_text
                raise AssertionError("pair cache hit should skip analysis and pair generation")

            second = trainer.train_pairwise_tables(config, analyzer=failing_analyzer)

        self.assertEqual(first.summary["rows"]["pair_cache_status"], "miss")
        self.assertEqual(second.summary["rows"]["pair_cache_status"], "hit")
        self.assertEqual(second.summary["pair_cache"]["status"], "hit")
        self.assertEqual(
            first.summary["training"]["initial_metrics"],
            second.summary["training"]["initial_metrics"],
        )

    def test_compact_checkpoint_resume_matches_fresh_training(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            partial_config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--out-dir",
                    str(temp_path / "runs" / "resume"),
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "1",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                ],
            )
            resume_config = replace(partial_config, epochs=3, resume_checkpoint=True)
            fresh_config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--out-dir",
                    str(temp_path / "runs" / "fresh"),
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "3",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                ],
            )

            trainer.train_pairwise_tables(partial_config, analyzer=wide_fake_analyzer)
            resumed = trainer.train_pairwise_tables(resume_config, analyzer=wide_fake_analyzer)
            fresh = trainer.train_pairwise_tables(fresh_config, analyzer=wide_fake_analyzer)

        self.assertEqual(resumed.summary["checkpoint"]["resumed_from_epoch"], 1)
        assert_history_close(
            self,
            resumed.summary["training"]["history"],
            fresh.summary["training"]["history"],
        )
        self.assertEqual(
            resumed.summary["training"]["weights"],
            fresh.summary["training"]["weights"],
        )

    def test_compact_checkpoint_resume_rejects_different_labels(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            first_labels = write_jsonl(temp_path / "labels-a.jsonl", [teacher_row(position_id="first")])
            second_labels = write_jsonl(temp_path / "labels-b.jsonl", [teacher_row(position_id="second")])
            config = make_config(
                temp_path,
                first_labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "1",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                ],
            )
            trainer.train_pairwise_tables(config, analyzer=wide_fake_analyzer)
            mismatched_config = make_config(
                temp_path,
                second_labels,
                extra_args=[
                    "--pair-mode",
                    "best-vs-all",
                    "--epochs",
                    "2",
                    "--learning-rate",
                    "0.07",
                    "--l2",
                    "0.03",
                    "--resume-checkpoint",
                ],
            )

            with self.assertRaisesRegex(ScriptError, "checkpoint config hash"):
                trainer.train_pairwise_tables(mismatched_config, analyzer=wide_fake_analyzer)

    def test_pair_cache_and_checkpoint_dirs_reject_source_data(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])

            with self.assertRaisesRegex(ScriptError, "--pair-cache-dir"):
                make_config(
                    temp_path,
                    labels,
                    extra_args=["--pair-cache-dir", "data/pair-cache"],
                )
            with self.assertRaisesRegex(ScriptError, "--checkpoint-dir"):
                make_config(
                    temp_path,
                    labels,
                    extra_args=["--checkpoint-dir", "data/checkpoints"],
                )

    def test_model_margin_ignores_root_scores(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels, extra_args=["--pair-mode", "best-vs-all"])
            pairs, _, _ = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        pair = next(pair for pair in pairs if pair.other_move == "a2")
        empty_weights = {phase: {} for phase in trainer.PHASES}

        self.assertEqual(trainer.pair_margin(empty_weights, pair), 0.0)
        self.assertEqual(pair.preferred_score, 50)
        self.assertEqual(pair.other_score, 60)
        self.assertEqual(trainer.model_margin(config, empty_weights, pair), 0.0)

    def test_scalar_base_eval_config_does_not_enter_training_score(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            eval_config = write_current_default_like_eval_config(temp_path / "current_default.eval")
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--eval-config",
                    str(eval_config),
                    "--pair-mode",
                    "best-vs-all",
                ],
            )
            pairs, _, _ = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        pair = next(pair for pair in pairs if pair.other_move == "a2")
        empty_weights = {phase: {} for phase in trainer.PHASES}

        self.assertEqual(trainer.model_margin(config, empty_weights, pair), 0.0)

    def test_one_pair_one_feature_overfits_loss_down_and_margin_up(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--epochs",
                    "12",
                    "--learning-rate",
                    "0.5",
                    "--l2",
                    "0",
                    "--max-abs-weight",
                    "20",
                ],
            )
            pair = synthetic_pair(phase="midgame", feature=("edge_8", 2131))
            initial_weights: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}

            weights, history = trainer.train_weights(config, [pair])

        self.assertLess(history[-1]["loss"], trainer.evaluate_pairs(config, initial_weights, [pair])["loss"])
        self.assertGreater(trainer.model_margin(config, weights, pair), 0.0)
        self.assertGreater(
            trainer.model_margin(config, weights, pair),
            trainer.model_margin(config, initial_weights, pair),
        )
        self.assertEqual(set(weights["midgame"]), {("edge_8", 2131)})

    def test_tiny_dataset_overfits_opening_midgame_late_one_feature_pairs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--epochs",
                    "18",
                    "--learning-rate",
                    "0.4",
                    "--l2",
                    "0",
                    "--max-abs-weight",
                    "20",
                ],
            )
            pairs = [
                synthetic_pair(phase="opening", feature=("edge_8", 2131)),
                synthetic_pair(phase="midgame", feature=("corner_2x3", 673)),
                synthetic_pair(phase="late", feature=("row_8", 42)),
            ]
            initial_weights: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}

            weights, history = trainer.train_weights(config, pairs)

        self.assertLess(history[-1]["loss"], trainer.evaluate_pairs(config, initial_weights, pairs)["loss"])
        for pair in pairs:
            self.assertGreater(trainer.model_margin(config, weights, pair), 0.0)
            preferred_root_score = trainer.model_margin(config, weights, pair)
            other_root_score = 0.0
            self.assertGreater(preferred_root_score, other_root_score)

    def test_listwise_softmax_overfits_synthetic_target_move(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--objective",
                    "listwise-softmax",
                    "--epochs",
                    "16",
                    "--learning-rate",
                    "0.4",
                    "--l2",
                    "0",
                ],
            )
            example = synthetic_listwise_example()
            initial: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}

            weights, history = trainer.train_weights(config, [], [example])

        self.assertLess(
            history[-1]["loss"],
            trainer.evaluate_listwise_examples(config, initial, [example])["loss"],
        )
        scores = trainer.listwise_scores(config, weights, example)
        self.assertGreater(scores["good"], scores["bad"])

    def test_compact_listwise_metrics_match_object_examples(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--objective",
                    "listwise-softmax",
                    "--epochs",
                    "1",
                    "--l2",
                    "0",
                ],
            )
            example = synthetic_listwise_example()
            compact = trainer.compact_listwise_dataset([example])
            weights = {
                "opening": {},
                "midgame": {("edge_8", 2131): 0.5, ("edge_8", 2179): -0.5},
                "late": {},
            }

        self.assertEqual(compact.example_count, 1)
        self.assertEqual(compact.candidate_count, 2)
        self.assertEqual(
            trainer.evaluate_listwise_examples(config, weights, [example]),
            trainer.evaluate_listwise_examples(config, weights, compact),
        )
        self.assertEqual(
            trainer.move_choice_metrics(config, weights, [example]),
            trainer.move_choice_metrics(config, weights, compact),
        )

    def test_listwise_feature_cache_reuses_child_pattern_features(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(move="d1")])
            cache_dir = temp_path / "feature-cache"
            common_args = [
                "--objective",
                "listwise-softmax",
                "--listwise-feature-cache-dir",
                str(cache_dir),
            ]
            warm_config = make_config(
                temp_path,
                labels,
                extra_args=[*common_args, "--listwise-feature-cache-mode", "read-write"],
            )
            _, warm_examples, _, warm_stats = trainer.collect_training_data(
                warm_config,
                analyzer=wide_fake_analyzer,
            )
            read_config = make_config(
                temp_path,
                labels,
                extra_args=[*common_args, "--listwise-feature-cache-mode", "read-only"],
            )
            _, read_examples, _, read_stats = trainer.collect_training_data(
                read_config,
                analyzer=wide_fake_analyzer,
            )

        self.assertEqual(len(warm_examples), 1)
        self.assertEqual(len(read_examples), 1)
        self.assertEqual(warm_examples[0].candidates, read_examples[0].candidates)
        self.assertGreater(warm_stats["listwise_feature_cache_writes"], 0)
        self.assertEqual(read_stats["listwise_feature_cache_misses"], 0)
        self.assertEqual(
            read_stats["listwise_feature_cache_hits"],
            len(read_examples[0].candidates),
        )

    def test_exact_aware_listwise_uses_exact_best_when_teacher_disagrees(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(move="d1")])
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    exact_row_with_scores(
                        TEACHER_BOARD,
                        best_move="b1",
                        move_scores={"b1": 12, "d1": -4},
                    )
                ],
            )
            config = make_config(
                temp_path,
                labels,
                exact_labels=exact,
                extra_args=[
                    "--objective",
                    "exact-aware-listwise",
                    "--epochs",
                    "12",
                    "--learning-rate",
                    "0.3",
                    "--l2",
                    "0",
                    "--sign-penalty",
                    "1",
                ],
            )

            pairs, examples, records, stats = trainer.collect_training_data(
                config,
                analyzer=wide_fake_analyzer,
            )
            weights, _ = trainer.train_weights(config, pairs, examples)

        self.assertEqual(len(examples), 1)
        self.assertEqual(examples[0].target_moves, ("b1",))
        self.assertEqual(stats["teacher_exact_disagreements_used_by_exact_aware"], 1)
        scores = trainer.listwise_scores(config, weights, examples[0])
        self.assertGreater(scores["b1"], scores["d1"])
        self.assertTrue(any(record.status == "paired" for record in records))

    def test_exact_score_soft_target_uses_move_score_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(move="d1")])
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [
                    exact_row_with_scores(
                        TEACHER_BOARD,
                        best_move="b1",
                        move_scores={
                            "a2": 9,
                            "a7": -12,
                            "b1": 10,
                            "d1": 6,
                            "g7": -14,
                            "h1": -30,
                            "h3": -8,
                            "h7": -16,
                        },
                    )
                ],
            )
            config = make_config(
                temp_path,
                labels,
                exact_labels=exact,
                extra_args=[
                    "--objective",
                    "exact-aware-listwise",
                    "--exact-score-soft-target",
                    "--exact-score-temperature",
                    "2",
                    "--exact-score-target-floor",
                    "0.001",
                    "--exact-score-near-best-window",
                    "8",
                    "--epochs",
                    "1",
                    "--l2",
                    "0",
                ],
            )

            _, examples, _, _ = trainer.collect_training_data(
                config,
                analyzer=wide_fake_analyzer,
            )
            compact = trainer.compact_listwise_dataset(examples)
            weights: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}
            metrics = trainer.evaluate_listwise_examples(config, weights, compact)
            diagnostics = trainer.move_choice_metrics(config, weights, compact)

        self.assertEqual(len(examples), 1)
        self.assertIsNotNone(examples[0].target_probabilities)
        probabilities = dict(
            zip(
                [candidate.move for candidate in examples[0].candidates],
                examples[0].target_probabilities or (),
                strict=True,
            )
        )
        self.assertGreater(probabilities["b1"], probabilities["a2"])
        self.assertGreater(probabilities["a2"], probabilities["d1"])
        self.assertGreater(probabilities["h1"], 0.0)
        self.assertLess(probabilities["h1"], probabilities["d1"])
        self.assertGreater(metrics["weighted_loss"], 0.0)
        self.assertIsNotNone(diagnostics["soft_target_cross_entropy"])
        self.assertEqual(diagnostics["soft_target_rows"], 1)
        self.assertIn("wrong_direction_by_phase", diagnostics)

    def test_exact_score_soft_target_falls_back_to_teacher_without_move_scores(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(move="d1")])
            exact = write_jsonl(
                temp_path / "exact.jsonl",
                [exact_row(TEACHER_BOARD, best_move="b1")],
            )
            config = make_config(
                temp_path,
                labels,
                exact_labels=exact,
                extra_args=[
                    "--objective",
                    "exact-aware-listwise",
                    "--exact-score-soft-target",
                ],
            )

            _, examples, _, _ = trainer.collect_training_data(
                config,
                analyzer=wide_fake_analyzer,
            )
            compact = trainer.compact_listwise_dataset(examples)

        self.assertEqual(len(examples), 1)
        self.assertEqual(examples[0].target_moves, ("d1",))
        self.assertIsNone(examples[0].target_probabilities)
        target_probabilities = dict(
            zip(compact.candidate_moves, compact.candidate_target_probabilities, strict=True)
        )
        self.assertEqual(target_probabilities["d1"], 1.0)
        self.assertEqual(target_probabilities["b1"], 0.0)

    def test_soft_sign_anchor_requires_soft_exact_score_target(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])

            with self.assertRaisesRegex(
                ScriptError,
                "--soft-sign-anchor-mode selected/expected requires",
            ):
                make_config(
                    temp_path,
                    labels,
                    extra_args=[
                        "--objective",
                        "exact-aware-listwise",
                        "--soft-sign-anchor-mode",
                        "selected",
                        "--soft-sign-anchor-weight",
                        "0.05",
                    ],
                )

    def test_soft_sign_anchor_diagnostics_detect_selected_violation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--objective",
                    "exact-aware-listwise",
                    "--exact-score-soft-target",
                    "--soft-sign-anchor-mode",
                    "selected",
                    "--soft-sign-anchor-weight",
                    "0.05",
                    "--soft-sign-anchor-margin",
                    "1",
                ],
            )
            example = synthetic_listwise_example(exact_root_score=8)
            weights: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}
            weights["midgame"] = {
                ("edge_8", 2131): -2.0,
                ("edge_8", 2179): -2.0,
            }

            diagnostics = trainer.move_choice_metrics(config, weights, [example])

        self.assertEqual(diagnostics["sign_anchor_rows"], 1)
        self.assertEqual(diagnostics["sign_anchor_updates"], 1)
        self.assertAlmostEqual(diagnostics["sign_anchor_loss"], 3.0)

    def test_soft_sign_anchor_expected_mode_spreads_updates(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--objective",
                    "exact-aware-listwise",
                    "--exact-score-soft-target",
                    "--soft-sign-anchor-mode",
                    "expected",
                    "--soft-sign-anchor-weight",
                    "0.1",
                    "--soft-sign-anchor-margin",
                    "1",
                    "--epochs",
                    "1",
                    "--learning-rate",
                    "0.1",
                    "--l2",
                    "0",
                ],
            )
            example = synthetic_listwise_example(exact_root_score=8)

            _, history = trainer.train_weights(config, [], [example])

        self.assertEqual(history[-1]["sign_anchor_rows"], 1)
        self.assertEqual(history[-1]["sign_anchor_updates"], 2)
        self.assertGreater(history[-1]["sign_anchor_loss"], 0.0)

    def test_output_scale_calibration_selects_best_grid_value(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--objective",
                    "listwise-softmax",
                    "--calibrate-output-scale",
                    "--scale-grid",
                    "1,10",
                ],
            )
            example = replace(synthetic_listwise_example(), teacher_move="good")
            weights: trainer.WeightsByPhase = {phase: {} for phase in trainer.PHASES}
            weights["midgame"] = {
                ("edge_8", 2131): 0.06,
                ("edge_8", 2179): 0.0,
            }

            calibrated, summary = trainer.calibrate_output_scale(config, weights, [example])

        self.assertEqual(summary["status"], "selected")
        self.assertEqual(calibrated.output_scale, 10.0)

    def test_same_seed_is_deterministic_for_generated_tables(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config_a = make_config(
                temp_path,
                labels,
                extra_args=["--out-dir", str(temp_path / "runs" / "a"), "--pair-mode", "best-vs-all"],
            )
            config_b = make_config(
                temp_path,
                labels,
                extra_args=["--out-dir", str(temp_path / "runs" / "b"), "--pair-mode", "best-vs-all"],
            )

            result_a = trainer.train_pairwise_tables(config_a, analyzer=wide_fake_analyzer)
            result_b = trainer.train_pairwise_tables(config_b, analyzer=wide_fake_analyzer)
            table_a = result_a.table_paths["late"].read_text(encoding="utf-8")
            table_b = result_b.table_paths["late"].read_text(encoding="utf-8")

        self.assertEqual(table_a, table_b)

    def test_training_writes_required_outputs_under_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            exact = write_jsonl(temp_path / "exact.jsonl", [exact_row(TEACHER_BOARD, "d1")])
            config = make_config(temp_path, labels, exact_labels=exact)

            result = trainer.train_pairwise_tables(config, analyzer=fake_analyzer)

            self.assertTrue(result.candidate_eval_path.exists())
            self.assertTrue(result.validation_path.exists())
            self.assertTrue(result.report_path.exists())
            for phase in trainer.PHASES:
                self.assertEqual(result.table_paths[phase].parent.name, "tables")
                self.assertTrue(result.table_paths[phase].exists())

            candidate = result.candidate_eval_path.read_text(encoding="utf-8")
            self.assertIn("mode=pattern_only", candidate)
            self.assertIn("pattern_table.opening=tables/opening.tsv", candidate)
            self.assertIn("pattern_table.midgame=tables/midgame.tsv", candidate)
            self.assertIn("pattern_table.late=tables/late.tsv", candidate)
            self.assertNotIn("pattern_table=patterns/base.tsv", candidate)

            validation = result.validation_path.read_text(encoding="utf-8")
            self.assertIn("paired", validation)
            self.assertIn("\td1\tb1\t", validation)
            self.assertEqual(result.summary["rows"]["preference_pairs"], 1)
            self.assertEqual(result.summary["rows"]["accepted_teacher_rows"], 1)
            self.assertGreater(
                result.summary["training"]["weights"]["late"]["entries"],
                0,
            )
            self.assertTrue(result.summary["no_strength_claim"])
            self.assertFalse(result.summary["default_promotion"])

    def test_candidate_eval_is_always_pattern_only_for_scalar_base(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            eval_config = write_current_default_like_eval_config(temp_path / "current_default.eval")
            args = [
                "--teacher-labels",
                str(labels),
                "--eval-config",
                str(eval_config),
                "--out-dir",
                str(temp_path / "runs" / "candidate"),
                "--candidate-pattern-table-weight",
                "3",
            ]
            config = trainer.config_from_args(trainer.parse_args(args))

            candidate = trainer.render_candidate_eval(config)

        self.assertIn("schema_version=eval.v1", candidate)
        self.assertIn("mode=pattern_only", candidate)
        self.assertIn("pattern_table.opening=tables/opening.tsv", candidate)
        self.assertIn("opening.pattern_table=3", candidate)
        self.assertIn("midgame.pattern_table=3", candidate)
        self.assertIn("late.pattern_table=3", candidate)
        self.assertNotIn("opening.mobility", candidate)
        self.assertNotIn("midgame.mobility", candidate)
        self.assertNotIn("late.mobility", candidate)

    def test_pattern_only_candidate_eval_omits_base_scalar_weights(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            eval_config = write_current_default_like_eval_config(temp_path / "current_default.eval")
            args = [
                "--teacher-labels",
                str(labels),
                "--eval-config",
                str(eval_config),
                "--out-dir",
                str(temp_path / "runs" / "candidate"),
                "--candidate-pattern-table-weight",
                "3",
            ]
            config = trainer.config_from_args(trainer.parse_args(args))

            candidate = trainer.render_candidate_eval(config)

        self.assertIn("schema_version=eval.v1", candidate)
        self.assertIn("mode=pattern_only", candidate)
        self.assertIn("name=regularized_pairwise_pattern_candidate", candidate)
        self.assertIn("# model: pattern_only_delta_only", candidate)
        self.assertIn("pattern_table.opening=tables/opening.tsv", candidate)
        self.assertIn("pattern_table.midgame=tables/midgame.tsv", candidate)
        self.assertIn("pattern_table.late=tables/late.tsv", candidate)
        self.assertIn("opening.pattern_table=3", candidate)
        self.assertIn("midgame.pattern_table=3", candidate)
        self.assertIn("late.pattern_table=3", candidate)
        self.assertIn("opening_max_occupied=20", candidate)
        self.assertIn("midgame_max_occupied=44", candidate)
        self.assertNotIn("opening.mobility", candidate)
        self.assertNotIn("midgame.mobility", candidate)
        self.assertNotIn("late.mobility", candidate)

    def test_report_uses_single_pattern_only_model_name(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels)
            summary = {
                "rows": {},
                "training": {
                    "initial_metrics": {
                        "weighted_loss": 0.0,
                        "unweighted_loss": 0.0,
                        "accuracy": 0.0,
                        "weighted_accuracy": 0.0,
                        "avg_margin": 0.0,
                    },
                    "final_metrics": {
                        "pairs": 0,
                        "weighted_loss": 0.0,
                        "unweighted_loss": 0.0,
                        "accuracy": 0.0,
                        "weighted_accuracy": 0.0,
                        "avg_margin": 0.0,
                        "total_pair_weight": 0.0,
                    },
                },
                "analysis_cache_mode": "off",
                "analysis_cache_dir": "",
                "analysis_jobs": 1,
            }

            report = trainer.render_report(
                config=config,
                summary=summary,
                table_paths={phase: temp_path / f"{phase}.tsv" for phase in trainer.PHASES},
                candidate_eval_path=temp_path / "candidate.eval",
                validation_path=temp_path / "validation.tsv",
            )

        self.assertIn("- model: `pattern_only_delta_only`", report)
        self.assertNotIn("candidate" + "_eval_shape", report)
        self.assertNotIn("model_margin", report)

    def test_table_data_lines_are_integer_values(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--output-scale", "8", "--max-abs-output-weight", "4"],
            )

            result = trainer.train_pairwise_tables(config, analyzer=fake_analyzer)

            data_line = re.compile(r"^[a-z0-9_]+\t[0-9]+\t-?[0-9]+$")
            for table_path in result.table_paths.values():
                for line in table_path.read_text(encoding="utf-8").splitlines():
                    if not line or line.startswith("#"):
                        continue
                    self.assertRegex(line, data_line)
                    value = int(line.rsplit("\t", 1)[1])
                    self.assertLessEqual(abs(value), 4)

    def test_quantized_tsv_roundtrip_preserves_python_margin_within_scale(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=[
                    "--output-scale",
                    "10",
                    "--max-abs-output-weight",
                    "1000",
                ],
            )
            pair = synthetic_pair(phase="late", feature=("edge_8", 2131))
            weights: trainer.WeightsByPhase = {
                phase: {} for phase in trainer.PHASES
            }
            weights["late"] = {("edge_8", 2131): 0.5}

            rendered = trainer.render_phase_table(
                config=config,
                phase="late",
                entries=trainer.phase_entries(weights, "late"),
                stats={"fixture": "quantization_roundtrip"},
            )

        data_lines = [
            line for line in rendered.splitlines() if line and not line.startswith("#")
        ]
        self.assertEqual(data_lines, ["edge_8\t2131\t5"])
        python_margin = trainer.model_margin(config, weights, pair)
        quantized_margin = int(data_lines[0].split("\t")[2]) / config.output_scale
        self.assertEqual(python_margin, 0.5)
        self.assertLessEqual(abs(quantized_margin - python_margin), 0.5 / config.output_scale)

    def test_exact_disagreement_filters_teacher_row(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(selected_move="b1")])
            exact = write_jsonl(temp_path / "exact.jsonl", [exact_row(TEACHER_BOARD, "b1")])
            config = make_config(temp_path, labels, exact_labels=exact)

            result = trainer.train_pairwise_tables(config, analyzer=fake_analyzer)
            validation = result.validation_path.read_text(encoding="utf-8")

        self.assertEqual(result.summary["rows"]["preference_pairs"], 0)
        self.assertEqual(result.summary["rows"]["teacher_exact_disagreements_skipped"], 1)
        self.assertIn("teacher_exact_disagreement", validation)

    def test_diagnose_dataset_writes_report_without_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(
                temp_path / "labels.jsonl",
                [
                    teacher_row(position_id="ok", source_bucket="balanced"),
                    teacher_row(move="z9", position_id="illegal", source_bucket="bad"),
                ],
            )
            config = make_config(
                temp_path,
                labels,
                extra_args=["--diagnose-dataset", "--pair-mode", "best-vs-all"],
            )

            result = trainer.diagnose_dataset(config, analyzer=wide_fake_analyzer)
            self.assertTrue(result.report_path.exists())
            self.assertFalse((config.out_dir / "candidate.eval").exists())
            self.assertTrue((config.out_dir / "diagnostic_validation.tsv").exists())
            self.assertEqual(result.summary["training_performed"], False)
            diagnostics = result.summary["rows"]["dataset_diagnostics"]
            self.assertEqual(diagnostics["by_source_bucket"]["balanced"]["accepted_rows"], 1)
            self.assertEqual(diagnostics["by_source_bucket"]["bad"]["illegal_skipped_rows"], 1)
            self.assertIn("By Source Bucket", result.report_path.read_text(encoding="utf-8"))

    def test_split_filter_uses_file_hint_and_can_train_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "validation.jsonl", [teacher_row(split="validation")])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--split", "validation"],
            )

            result = trainer.train_pairwise_tables(config, analyzer=fake_analyzer)

        self.assertEqual(result.summary["rows"]["preference_pairs"], 1)
        self.assertEqual(result.summary["rows"]["validation_rows"], 1)

    def test_boundary_opening_root_pair_uses_midgame_child_phase(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            board, moves = boundary_board(20)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=moves[0])])
            config = make_config(temp_path, labels)
            teacher_move, other_move = boundary_pair_moves(config, board, moves)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=teacher_move)])

            pairs, records, stats = trainer.collect_pairs(
                config,
                analyzer=lambda _config, _board: analyzer_with_ranked_above(teacher_move, other_move),
            )

        self.assertEqual(trainer.phase_for_board(board, config.phase_cutoffs), "opening")
        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].phase, "midgame")
        self.assertEqual(stats["opening_pairs"], 0)
        self.assertEqual(stats["midgame_pairs"], 1)
        self.assertTrue(any(record.status == "paired" and record.phase == "midgame" for record in records))

    def test_boundary_midgame_root_pair_uses_late_child_phase(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            board, moves = boundary_board(44)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=moves[0])])
            config = make_config(temp_path, labels)
            teacher_move, other_move = boundary_pair_moves(config, board, moves)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=teacher_move)])

            pairs, records, stats = trainer.collect_pairs(
                config,
                analyzer=lambda _config, _board: analyzer_with_ranked_above(teacher_move, other_move),
            )

        self.assertEqual(trainer.phase_for_board(board, config.phase_cutoffs), "midgame")
        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0].phase, "late")
        self.assertEqual(stats["midgame_pairs"], 0)
        self.assertEqual(stats["late_pairs"], 1)
        self.assertTrue(any(record.status == "paired" and record.phase == "late" for record in records))

    def test_boundary_pair_updates_child_phase_table_not_root_phase_table(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            board, moves = boundary_board(20)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=moves[0])])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--epochs", "1", "--l2", "0"],
            )
            teacher_move, other_move = boundary_pair_moves(config, board, moves)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row(board=board, move=teacher_move)])

            result = trainer.train_pairwise_tables(
                config,
                analyzer=lambda _config, _board: analyzer_with_ranked_above(teacher_move, other_move),
            )

        self.assertEqual(result.summary["rows"]["opening_pairs"], 0)
        self.assertEqual(result.summary["rows"]["midgame_pairs"], 1)
        self.assertEqual(result.summary["training"]["weights"]["opening"]["entries"], 0)
        self.assertGreater(result.summary["training"]["weights"]["midgame"]["entries"], 0)

    def test_output_dir_must_be_under_runs_and_not_data_eval(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            eval_config = write_eval_config(temp_path / "base.eval")

            bad_args = trainer.parse_args(
                [
                    "--teacher-labels",
                    str(labels),
                    "--eval-config",
                    str(eval_config),
                    "--out-dir",
                    str(temp_path / "data" / "eval" / "bad"),
                ]
            )

            with self.assertRaises(ScriptError):
                trainer.config_from_args(bad_args)


if __name__ == "__main__":
    unittest.main()
