from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import balanced_position_pool as pool  # noqa: E402


def board9_with_empties(empties: int, salt: int) -> str:
    cells = ["B" if index % 2 == 0 else "W" for index in range(64)]
    for offset in range(empties):
        cells[(salt + offset) % 64] = "."
    rows = ["".join(cells[index : index + 8]) for index in range(0, 64, 8)]
    side = "B" if salt % 2 == 0 else "W"
    return "\n".join([*rows, f"side={side}"])


def write_sampler_output(command: list[str], boards: list[str]) -> subprocess.CompletedProcess[str]:
    output_path = Path(command[command.index("--output") + 1])
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n\n".join(boards) + "\n", encoding="utf-8")
    return subprocess.CompletedProcess(command, 0, stdout="sampled\n", stderr="")


def deterministic_runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
    count = int(command[command.index("--count") + 1])
    empties = int(command[command.index("--target-empties") + 1])
    seed = int(command[command.index("--seed") + 1])
    boards = [board9_with_empties(empties, seed + index) for index in range(count)]
    return write_sampler_output(command, boards)


def config_for(
    output_path: Path,
    *,
    bucket_spec: str = "52:2,48:1",
    seed: int = 7,
) -> pool.PoolConfig:
    return pool.PoolConfig(
        output_path=output_path,
        buckets=pool.parse_bucket_spec(bucket_spec),
        seed=seed,
        sampler=Path("fake_sampler"),
        max_attempts_per_bucket=123,
        dry_run=False,
        invocation=[
            "balanced_position_pool.py",
            "--output",
            str(output_path),
            "--bucket-spec",
            bucket_spec,
            "--seed",
            str(seed),
            "--sampler",
            "fake_sampler",
        ],
    )


class BalancedPositionPoolTests(unittest.TestCase):
    def test_parse_bucket_spec_accepts_named_and_custom_specs(self) -> None:
        custom = pool.parse_bucket_spec("52:4,51:5 48:6")

        self.assertEqual(
            [(bucket.empties, bucket.count) for bucket in custom],
            [(52, 4), (51, 5), (48, 6)],
        )
        smoke = pool.parse_bucket_spec("smoke")
        self.assertGreaterEqual(sum(bucket.count for bucket in smoke), 1000)
        self.assertLessEqual(sum(bucket.count for bucket in smoke), 5000)

    def test_default_300k_derived_seeds_are_bounded_and_deterministic(self) -> None:
        buckets = pool.parse_bucket_spec("default-300k")

        first = [
            pool.derived_seed(20260601, bucket, bucket_index, round_index)
            for bucket_index, bucket in enumerate(buckets)
            for round_index in range(pool.MAX_BUCKET_ROUNDS)
        ]
        second = [
            pool.derived_seed(20260601, bucket, bucket_index, round_index)
            for bucket_index, bucket in enumerate(buckets)
            for round_index in range(pool.MAX_BUCKET_ROUNDS)
        ]

        self.assertEqual(sum(bucket.count for bucket in buckets), 300000)
        self.assertEqual(first, second)
        self.assertTrue(all(1 <= seed <= pool.MAX_SAFE_SAMPLER_SEED for seed in first))

    def test_parse_bucket_spec_rejects_invalid_segments(self) -> None:
        with self.assertRaisesRegex(pool.ScriptError, "duplicate empty-count"):
            pool.parse_bucket_spec("52:1,52:2")

        with self.assertRaisesRegex(pool.ScriptError, "must be in"):
            pool.parse_bucket_spec("65:1")

        with self.assertRaisesRegex(pool.ScriptError, "count must be positive"):
            pool.parse_bucket_spec("52:0")

    def test_source_controlled_data_output_is_rejected(self) -> None:
        with self.assertRaisesRegex(pool.ScriptError, "source-controlled data"):
            pool.reject_source_controlled_output(pool.REPO_ROOT / "data" / "pool" / "positions.txt")

    def test_manifest_generation_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output_path = Path(temp) / "pool" / "positions.txt"
            config = config_for(output_path)

            pool.run_pool(config, runner=deterministic_runner)
            first_manifest = json.loads((output_path.parent / "manifest.json").read_text())

            pool.run_pool(config, runner=deterministic_runner)
            second_manifest = json.loads((output_path.parent / "manifest.json").read_text())

        self.assertEqual(first_manifest, second_manifest)
        self.assertEqual(first_manifest["counts"]["requested_unique_positions"], 3)
        self.assertEqual(first_manifest["counts"]["generated_unique_positions"], 3)

    def test_identical_board9_positions_are_deduplicated_and_topped_up(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output_path = Path(temp) / "pool" / "positions.txt"
            config = config_for(output_path, bucket_spec="52:2")
            calls = 0

            def duplicate_runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                nonlocal calls
                calls += 1
                if calls == 1:
                    board = board9_with_empties(52, 1)
                    return write_sampler_output(command, [board, board])
                return write_sampler_output(command, [board9_with_empties(52, 2)])

            pool.run_pool(config, runner=duplicate_runner)
            positions = pool.parse_board9_positions(
                output_path.read_text(encoding="utf-8"),
                bucket_empties=52,
                source_name=str(output_path),
            )
            duplicate_report = (output_path.parent / "qc" / "duplicate_report.tsv").read_text(
                encoding="utf-8"
            )
            manifest = json.loads((output_path.parent / "manifest.json").read_text())

        self.assertEqual(calls, 2)
        self.assertEqual(len(positions), 2)
        self.assertEqual(len({record.position_id for record in positions}), 2)
        self.assertIn("sha256:", duplicate_report)
        self.assertEqual(manifest["counts"]["duplicate_candidates"], 1)

    def test_duplicate_only_sampler_round_continues_to_next_seed(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            output_path = Path(temp) / "pool" / "positions.txt"
            config = config_for(output_path, bucket_spec="52:2")
            board_a = board9_with_empties(52, 1)
            board_b = board9_with_empties(52, 2)
            calls = 0

            def duplicate_only_round_runner(
                command: list[str], **_: object
            ) -> subprocess.CompletedProcess[str]:
                nonlocal calls
                calls += 1
                if calls == 1:
                    return write_sampler_output(command, [board_a])
                if calls == 2:
                    return write_sampler_output(command, [board_a])
                return write_sampler_output(command, [board_b])

            pool.run_pool(config, runner=duplicate_only_round_runner)
            positions = pool.parse_board9_positions(
                output_path.read_text(encoding="utf-8"),
                bucket_empties=52,
                source_name=str(output_path),
            )
            manifest = json.loads((output_path.parent / "manifest.json").read_text())

        self.assertEqual(calls, 3)
        self.assertEqual(len(positions), 2)
        self.assertEqual(manifest["counts"]["duplicate_candidates"], 1)
        self.assertEqual(manifest["buckets"][0]["sampler_invocations"], 3)


if __name__ == "__main__":
    unittest.main()
