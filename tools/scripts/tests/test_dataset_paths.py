from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import dataset_paths  # noqa: E402
from common import ScriptError  # noqa: E402


def write_local_config(repo_root: Path, text: str) -> Path:
    config_path = repo_root / "config" / "datasets.local.toml"
    config_path.parent.mkdir(parents=True, exist_ok=True)
    config_path.write_text(textwrap.dedent(text), encoding="utf-8")
    return config_path


class DatasetPathTests(unittest.TestCase):
    def test_explicit_root_wins_over_env_and_local_config(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()
            cli_root = Path(temp) / "cli-root"
            env_root = Path(temp) / "env-root"
            config_root = Path(temp) / "config-root"
            write_local_config(
                repo,
                f"""
                [datasets]
                root = "{config_root}"
                """,
            )

            resolved = dataset_paths.resolve_dataset_root(
                str(cli_root),
                env={dataset_paths.DATASET_ROOT_ENV: str(env_root)},
                repo_root=repo,
            )

        self.assertEqual(resolved.path, cli_root.resolve(strict=False))
        self.assertEqual(resolved.source, "--dataset-root")

    def test_env_root_wins_over_local_config(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()
            env_root = Path(temp) / "env-root"
            config_root = Path(temp) / "config-root"
            write_local_config(
                repo,
                f"""
                [datasets]
                root = "{config_root}"
                """,
            )

            resolved = dataset_paths.resolve_dataset_root(
                env={dataset_paths.DATASET_ROOT_ENV: str(env_root)},
                repo_root=repo,
            )

        self.assertEqual(resolved.path, env_root.resolve(strict=False))
        self.assertEqual(resolved.source, dataset_paths.DATASET_ROOT_ENV)

    def test_local_config_root_resolves_relative_to_repo_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()
            write_local_config(
                repo,
                """
                [datasets]
                root = "shared-datasets"
                """,
            )

            resolved = dataset_paths.resolve_dataset_root(env={}, repo_root=repo)

        self.assertEqual(resolved.path, (repo / "shared-datasets").resolve(strict=False))
        self.assertEqual(
            resolved.source,
            str((repo / "config" / "datasets.local.toml").resolve(strict=False)),
        )

    def test_missing_root_explains_supported_setup_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()

            with self.assertRaises(ScriptError) as context:
                dataset_paths.resolve_dataset_root(env={}, repo_root=repo)

        message = str(context.exception)
        self.assertIn("--dataset-root", message)
        self.assertIn(dataset_paths.DATASET_ROOT_ENV, message)
        self.assertIn("config/datasets.example.toml", message)
        self.assertIn("runs/", message)

    def test_dataset_relative_reference_resolves_under_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "datasets"
            repo = Path(temp) / "repo"
            root.mkdir()
            repo.mkdir()

            resolved = dataset_paths.resolve_dataset_reference(
                "dataset:teacher/ntest-depth26-2027/labels/merged.jsonl",
                str(root),
                env={},
                repo_root=repo,
            )

        self.assertEqual(
            resolved,
            (root / "teacher" / "ntest-depth26-2027" / "labels" / "merged.jsonl").resolve(
                strict=False
            ),
        )

    def test_dataset_catalog_reference_resolves_entry_field(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo = Path(temp) / "repo"
            repo.mkdir()
            write_local_config(
                repo,
                """
                [datasets]
                root = "shared-datasets"

                [teacher.ntest_depth26_2027]
                dataset_id = "ntest-depth26-2027"
                path = "teacher/ntest-depth26-2027"
                labels = "labels/merged.jsonl"
                exact_overlap = "exact-overlap/labels.jsonl"
                """,
            )

            labels = dataset_paths.resolve_dataset_reference(
                "dataset:teacher.ntest_depth26_2027:labels",
                env={},
                repo_root=repo,
            )
            exact = dataset_paths.resolve_dataset_reference(
                "dataset:teacher.ntest_depth26_2027:exact_overlap",
                env={},
                repo_root=repo,
            )

        root = repo / "shared-datasets" / "teacher" / "ntest-depth26-2027"
        self.assertEqual(labels, (root / "labels" / "merged.jsonl").resolve(strict=False))
        self.assertEqual(exact, (root / "exact-overlap" / "labels.jsonl").resolve(strict=False))

    def test_dataset_reference_rejects_absolute_and_parent_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "datasets"
            repo = Path(temp) / "repo"
            root.mkdir()
            repo.mkdir()

            with self.assertRaises(ScriptError):
                dataset_paths.resolve_dataset_reference(
                    "dataset:/tmp/labels.jsonl",
                    str(root),
                    env={},
                    repo_root=repo,
                )
            with self.assertRaises(ScriptError):
                dataset_paths.resolve_dataset_reference(
                    "dataset:../labels.jsonl",
                    str(root),
                    env={},
                    repo_root=repo,
                )


if __name__ == "__main__":
    unittest.main()
