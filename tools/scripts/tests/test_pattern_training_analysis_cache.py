from __future__ import annotations

import collections
import sys
import tempfile
import time
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from common import ScriptError  # noqa: E402
from pattern_training.analysis_cache import (  # noqa: E402
    AnalysisCacheConfig,
    AnalysisRequest,
    AnalysisRunnerConfig,
    analysis_cache_key,
    analyze_requests,
    analysis_cache_path,
    sha256_file,
)
from pattern_training.board9 import board_hash  # noqa: E402
from pattern_training.root_candidates import RootAnalysis, parse_analysis_stdout  # noqa: E402


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

ALT_BOARD = (
    "........\n"
    "........\n"
    "........\n"
    "..BBB...\n"
    "...BW...\n"
    "........\n"
    "........\n"
    "........\n"
    "side=W"
)


def write_fixture_files(temp_path: Path) -> tuple[Path, Path]:
    analyzer = temp_path / "fake_analyze_position"
    analyzer.write_text("fake analyzer v1\n", encoding="utf-8")
    eval_config = temp_path / "eval.conf"
    eval_config.write_text("name=fake\nmidgame.mobility=1\n", encoding="utf-8")
    return analyzer, eval_config


def make_runner_config(
    *,
    analyzer: Path,
    eval_config: Path,
    cache_dir: Path,
    mode: str = "read-write",
    jobs: int = 1,
    depth: int = 3,
) -> AnalysisRunnerConfig:
    return AnalysisRunnerConfig(
        analysis_cache=AnalysisCacheConfig(directory=cache_dir, mode=mode),
        analysis_jobs=jobs,
        analyze_position=analyzer,
        eval_config=eval_config,
        analysis_depth=depth,
    )


def make_request(
    *,
    source_index: int,
    board_text: str,
    eval_config_hash: str,
    depth: int = 3,
) -> AnalysisRequest:
    return AnalysisRequest(
        source_index=source_index,
        board_text=board_text,
        cache_key=analysis_cache_key(
            board_hash=board_hash(board_text),
            eval_config_hash=eval_config_hash,
            analysis_depth=depth,
        ),
        position_id=f"position-{source_index}",
    )


def fake_root(move: str = "d3", score: int = 12) -> RootAnalysis:
    return parse_analysis_stdout(
        f"best_move: {move}\n"
        "root_candidates:\n"
        f"  - move: {move}\n"
        f"    score: {score}\n"
    )


class PatternTrainingAnalysisCacheTests(unittest.TestCase):
    def test_analysis_cache_miss_then_hit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer, eval_config = write_fixture_files(temp_path)
            cache_dir = temp_path / "analysis-cache"
            eval_hash = sha256_file(eval_config)
            config = make_runner_config(
                analyzer=analyzer,
                eval_config=eval_config,
                cache_dir=cache_dir,
            )
            request = make_request(source_index=0, board_text=BOARD, eval_config_hash=eval_hash)
            calls = 0

            def analyzer_fn(board_text: str) -> RootAnalysis:
                nonlocal calls
                calls += 1
                self.assertEqual(board_text, BOARD)
                return fake_root()

            first_stats: collections.Counter[str] = collections.Counter()
            first = analyze_requests(
                config=config,
                requests=[request],
                analyzer=analyzer_fn,
                stats=first_stats,
                eval_config_hash=eval_hash,
            )
            second_stats: collections.Counter[str] = collections.Counter()
            second = analyze_requests(
                config=config,
                requests=[request],
                analyzer=analyzer_fn,
                stats=second_stats,
                eval_config_hash=eval_hash,
            )

        self.assertEqual(first[0].best_move, "d3")
        self.assertEqual(second[0].root_scores, {"d3": 12})
        self.assertEqual(calls, 1)
        self.assertEqual(first_stats["analysis_cache_hits"], 0)
        self.assertEqual(first_stats["analysis_cache_misses"], 1)
        self.assertEqual(first_stats["analysis_cache_writes"], 1)
        self.assertEqual(second_stats["analysis_cache_hits"], 1)
        self.assertEqual(second_stats["analysis_cache_misses"], 0)

    def test_read_only_cache_miss_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer, eval_config = write_fixture_files(temp_path)
            eval_hash = sha256_file(eval_config)
            config = make_runner_config(
                analyzer=analyzer,
                eval_config=eval_config,
                cache_dir=temp_path / "analysis-cache",
                mode="read-only",
            )
            request = make_request(source_index=0, board_text=BOARD, eval_config_hash=eval_hash)

            with self.assertRaisesRegex(ScriptError, "analysis cache missing"):
                analyze_requests(
                    config=config,
                    requests=[request],
                    analyzer=lambda _: fake_root(),
                    stats=collections.Counter(),
                    eval_config_hash=eval_hash,
                )

    def test_invalid_analyzer_binary_hash_is_not_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer, eval_config = write_fixture_files(temp_path)
            cache_dir = temp_path / "analysis-cache"
            eval_hash = sha256_file(eval_config)
            config = make_runner_config(
                analyzer=analyzer,
                eval_config=eval_config,
                cache_dir=cache_dir,
            )
            request = make_request(source_index=0, board_text=BOARD, eval_config_hash=eval_hash)
            calls = 0

            def analyzer_fn(_: str) -> RootAnalysis:
                nonlocal calls
                calls += 1
                return fake_root(score=10 + calls)

            analyze_requests(
                config=config,
                requests=[request],
                analyzer=analyzer_fn,
                stats=collections.Counter(),
                eval_config_hash=eval_hash,
            )
            analyzer.write_text("fake analyzer v2\n", encoding="utf-8")
            stale_stats: collections.Counter[str] = collections.Counter()
            result = analyze_requests(
                config=config,
                requests=[request],
                analyzer=analyzer_fn,
                stats=stale_stats,
                eval_config_hash=eval_hash,
            )

        self.assertEqual(calls, 2)
        self.assertEqual(result[0].root_scores, {"d3": 12})
        self.assertEqual(stale_stats["analysis_cache_hits"], 0)
        self.assertEqual(stale_stats["analysis_cache_misses"], 1)

    def test_cache_key_generation_is_deterministic_and_input_sensitive(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            _, eval_config = write_fixture_files(Path(temp))
            eval_hash = sha256_file(eval_config)

        first = analysis_cache_key(
            board_hash=board_hash(BOARD),
            eval_config_hash=eval_hash,
            analysis_depth=3,
        )
        second = analysis_cache_key(
            board_hash=board_hash(BOARD),
            eval_config_hash=eval_hash,
            analysis_depth=3,
        )
        different_depth = analysis_cache_key(
            board_hash=board_hash(BOARD),
            eval_config_hash=eval_hash,
            analysis_depth=4,
        )
        different_board = analysis_cache_key(
            board_hash=board_hash(ALT_BOARD),
            eval_config_hash=eval_hash,
            analysis_depth=3,
        )

        self.assertEqual(first, second)
        self.assertNotEqual(first, different_depth)
        self.assertNotEqual(first, different_board)

    def test_duplicate_requests_invoke_analyzer_once(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer, eval_config = write_fixture_files(temp_path)
            eval_hash = sha256_file(eval_config)
            config = make_runner_config(
                analyzer=analyzer,
                eval_config=eval_config,
                cache_dir=temp_path / "analysis-cache",
                jobs=2,
            )
            requests = [
                make_request(source_index=0, board_text=BOARD, eval_config_hash=eval_hash),
                make_request(source_index=1, board_text=BOARD, eval_config_hash=eval_hash),
            ]
            calls = 0

            def analyzer_fn(_: str) -> RootAnalysis:
                nonlocal calls
                calls += 1
                return fake_root()

            stats: collections.Counter[str] = collections.Counter()
            result = analyze_requests(
                config=config,
                requests=requests,
                analyzer=analyzer_fn,
                stats=stats,
                eval_config_hash=eval_hash,
            )

        self.assertEqual(calls, 1)
        self.assertEqual(result[0].root_scores, result[1].root_scores)
        self.assertEqual(stats["analysis_cache_misses"], 2)
        self.assertEqual(stats["analysis_cache_writes"], 1)

    def test_parallel_analysis_returns_results_in_source_order(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer, eval_config = write_fixture_files(temp_path)
            eval_hash = sha256_file(eval_config)
            config = make_runner_config(
                analyzer=analyzer,
                eval_config=eval_config,
                cache_dir=temp_path / "analysis-cache",
                jobs=2,
            )
            requests = [
                make_request(source_index=0, board_text=BOARD, eval_config_hash=eval_hash),
                make_request(source_index=1, board_text=ALT_BOARD, eval_config_hash=eval_hash),
            ]

            def analyzer_fn(board_text: str) -> RootAnalysis:
                if board_text == BOARD:
                    time.sleep(0.02)
                    return fake_root("d3", 1)
                return fake_root("c4", 2)

            result = analyze_requests(
                config=config,
                requests=requests,
                analyzer=analyzer_fn,
                stats=collections.Counter(),
                eval_config_hash=eval_hash,
            )

        self.assertEqual(list(result), [0, 1])
        self.assertEqual([result[index].best_move for index in result], ["d3", "c4"])

    def test_batch_analysis_streams_cache_writes_before_full_completion(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            analyzer, eval_config = write_fixture_files(temp_path)
            cache_dir = temp_path / "analysis-cache"
            eval_hash = sha256_file(eval_config)
            config = make_runner_config(
                analyzer=analyzer,
                eval_config=eval_config,
                cache_dir=cache_dir,
            )
            requests = [
                make_request(source_index=0, board_text=BOARD, eval_config_hash=eval_hash),
                make_request(source_index=1, board_text=ALT_BOARD, eval_config_hash=eval_hash),
            ]

            def batch_analyzer(
                batch_requests: list[AnalysisRequest],
            ):
                yield batch_requests[0], fake_root("d3", 1)
                self.assertTrue(analysis_cache_path(cache_dir).is_file())
                self.assertIn(
                    '"position_id":"position-0"',
                    analysis_cache_path(cache_dir).read_text(encoding="utf-8"),
                )
                raise ScriptError("simulated interruption")

            with self.assertRaisesRegex(ScriptError, "simulated interruption"):
                analyze_requests(
                    config=config,
                    requests=requests,
                    analyzer=lambda _: fake_root(),
                    stats=collections.Counter(),
                    eval_config_hash=eval_hash,
                    batch_analyzer=batch_analyzer,
                )

            cache_text = analysis_cache_path(cache_dir).read_text(encoding="utf-8")
            self.assertIn('"position_id":"position-0"', cache_text)
            self.assertNotIn('"position_id":"position-1"', cache_text)


if __name__ == "__main__":
    unittest.main()
