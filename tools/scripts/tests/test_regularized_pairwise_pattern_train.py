from __future__ import annotations

import json
import re
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import regularized_pairwise_pattern_train as trainer  # noqa: E402
import pattern_teacher_v0_train as legacy_trainer  # noqa: E402
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
        "name=pattern_reboot_fixture\n"
        "pattern_table=patterns/base.tsv\n"
        "opening.pattern_table=4\n"
        "midgame.pattern_table=4\n"
        "late.pattern_table=4\n"
        "opening_max_occupied=20\n"
        "midgame_max_occupied=44\n",
        encoding="utf-8",
    )
    return path


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
) -> dict[str, object]:
    row: dict[str, object] = {
        "schema": "teacher_label.v1",
        "status": "ok",
        "legal_move_valid": True,
        "move_token_valid": True,
        "board_text": board,
        "move": move,
        "position_split": split,
        "position_id": f"{split}-{move}",
    }
    if selected_move is not None:
        row["selected_move"] = selected_move
    return row


def exact_row(board: str, best_move: str) -> dict[str, object]:
    return {
        "schema": "exact_label.v1",
        "board": board,
        "best_move": best_move,
        "best_moves": [best_move],
    }


def fake_analyzer(
    config: trainer.TrainerConfig, board_text: str
) -> trainer.AnalyzeResult:
    del config
    trainer.apply_move_to_board(board_text, "b1")
    return trainer.AnalyzeResult(best_move="b1", root_scores={"b1": 20, "d1": 10})


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


def make_config(
    temp_path: Path,
    labels: Path,
    *,
    exact_labels: Path | None = None,
    extra_args: list[str] | None = None,
) -> trainer.TrainerConfig:
    eval_config = write_eval_config(temp_path / "pattern_reboot_v0.eval")
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

    def test_pattern_indexes_match_existing_trainer_convention(self) -> None:
        rows, side = trainer.parse_board(TEACHER_BOARD)
        legacy_rows, legacy_side = legacy_trainer.parse_board(TEACHER_BOARD)

        self.assertEqual(side, legacy_side)
        for family in trainer.FAMILY_ORDER:
            self.assertEqual(trainer.PATTERN_SPECS[family], legacy_trainer.PATTERN_SPECS[family])
            for spec in trainer.PATTERN_SPECS[family]:
                self.assertEqual(
                    trainer.pattern_index(rows, side, spec),
                    legacy_trainer.pattern_index(legacy_rows, legacy_side, spec),
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

    def test_apply_move_and_preference_features_create_pairwise_delta(self) -> None:
        teacher_child = trainer.apply_move_to_board(TEACHER_BOARD, "d1")
        engine_child = trainer.apply_move_to_board(TEACHER_BOARD, "b1")

        features = trainer.preference_features(
            root_board_text=TEACHER_BOARD,
            teacher_child_board=teacher_child,
            engine_child_board=engine_child,
            families=("edge_8", "corner_2x3"),
        )

        self.assertTrue(features)
        self.assertIn("opening", trainer.PHASES)

    def test_preference_features_match_negated_child_side_search_score_delta(self) -> None:
        teacher_child = trainer.apply_move_to_board(TEACHER_BOARD, "d1")
        engine_child = trainer.apply_move_to_board(TEACHER_BOARD, "b1")
        _, root_side = trainer.parse_board(TEACHER_BOARD)
        child_side = trainer.opponent(root_side)

        features = trainer.preference_features(
            root_board_text=TEACHER_BOARD,
            teacher_child_board=teacher_child,
            engine_child_board=engine_child,
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
        self.assertTrue(any(record.status == "paired" for record in records))

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

    def test_model_margin_includes_base_score_margin_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(temp_path, labels, extra_args=["--pair-mode", "best-vs-all"])
            pairs, _, _ = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        pair = next(pair for pair in pairs if pair.other_move == "a2")
        empty_weights = {phase: {} for phase in trainer.PHASES}

        self.assertEqual(trainer.pair_margin(empty_weights, pair), 0.0)
        self.assertEqual(trainer.model_margin(config, empty_weights, pair), -10.0)

    def test_no_base_margin_preserves_delta_only_objective(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            labels = write_jsonl(temp_path / "labels.jsonl", [teacher_row()])
            config = make_config(
                temp_path,
                labels,
                extra_args=["--pair-mode", "best-vs-all", "--no-base-margin"],
            )
            pairs, _, _ = trainer.collect_pairs(config, analyzer=wide_fake_analyzer)

        pair = next(pair for pair in pairs if pair.other_move == "a2")
        empty_weights = {phase: {} for phase in trainer.PHASES}

        self.assertEqual(trainer.model_margin(config, empty_weights, pair), 0.0)

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

    def test_candidate_eval_inserts_missing_phase_pattern_weights_for_scalar_base(self) -> None:
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

        self.assertIn("opening.mobility=8", candidate)
        self.assertIn("pattern_table.opening=tables/opening.tsv", candidate)
        self.assertIn("opening.pattern_table=3", candidate)
        self.assertIn("midgame.pattern_table=3", candidate)
        self.assertIn("late.pattern_table=3", candidate)

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
