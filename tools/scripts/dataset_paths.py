#!/usr/bin/env python3
"""Resolve reusable teacher/exact dataset paths outside worktree-local runs/."""

from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

from common import ScriptError


DATASET_ROOT_ENV = "VIBE_OTHELLO_DATASET_ROOT"
DATASET_REFERENCE_PREFIX = "dataset:"
DEFAULT_LOCAL_CONFIG = Path("config") / "datasets.local.toml"
REPO_ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class DatasetRoot:
    path: Path
    source: str


@dataclass(frozen=True)
class DatasetCatalog:
    root: DatasetRoot
    entries: dict[str, dict[str, str]]


def local_config_path(repo_root: Path | None = None) -> Path:
    return (repo_root or REPO_ROOT) / DEFAULT_LOCAL_CONFIG


def _read_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as input_file:
            loaded = tomllib.load(input_file)
    except FileNotFoundError:
        return {}
    except tomllib.TOMLDecodeError as exc:
        raise ScriptError(f"failed to parse dataset config {path}: {exc}") from exc
    except OSError as exc:
        raise ScriptError(f"failed to read dataset config {path}: {exc}") from exc
    if not isinstance(loaded, dict):
        raise ScriptError(f"dataset config {path} must contain TOML tables")
    return loaded


def _normalize_root(value: str, *, base_dir: Path) -> Path:
    text = value.strip()
    if not text:
        raise ScriptError("dataset root path cannot be empty")
    path = Path(text).expanduser()
    if not path.is_absolute():
        path = base_dir / path
    return path.resolve(strict=False)


def _ensure_existing_root(root: DatasetRoot, *, require_exists: bool) -> DatasetRoot:
    if require_exists and not root.path.is_dir():
        raise ScriptError(
            f"dataset root from {root.source} does not exist or is not a directory: {root.path}"
        )
    return root


def _missing_root_error(config_path: Path) -> ScriptError:
    return ScriptError(
        "dataset root is not configured. Pass --dataset-root PATH, set "
        f"{DATASET_ROOT_ENV}, or copy config/datasets.example.toml to "
        f"{config_path} and set [datasets].root. Do not rely on old "
        "worktree-local runs/ paths for reusable teacher/exact datasets."
    )


def _local_config_root(config: Mapping[str, Any], *, config_path: Path) -> str | None:
    if not config:
        return None
    datasets = config.get("datasets")
    if not isinstance(datasets, dict):
        raise ScriptError(f"{config_path} must contain a [datasets] table with root = PATH")
    root = datasets.get("root")
    if root is None:
        raise ScriptError(f"{config_path} must contain [datasets].root")
    if not isinstance(root, str):
        raise ScriptError(f"{config_path} [datasets].root must be a string")
    return root


def resolve_dataset_root(
    explicit_root: str | Path | None = None,
    *,
    env: Mapping[str, str] | None = None,
    repo_root: Path | None = None,
    config_path: Path | None = None,
    require_exists: bool = False,
) -> DatasetRoot:
    """Resolve the shared dataset root in CLI, env, local-config priority order."""

    repo = (repo_root or REPO_ROOT).resolve(strict=False)
    config = config_path or local_config_path(repo)
    environment = os.environ if env is None else env

    if explicit_root is not None:
        root = DatasetRoot(
            path=_normalize_root(str(explicit_root), base_dir=Path.cwd()),
            source="--dataset-root",
        )
        return _ensure_existing_root(root, require_exists=require_exists)

    env_root = environment.get(DATASET_ROOT_ENV)
    if env_root is not None and env_root.strip():
        root = DatasetRoot(
            path=_normalize_root(env_root, base_dir=Path.cwd()),
            source=DATASET_ROOT_ENV,
        )
        return _ensure_existing_root(root, require_exists=require_exists)

    config_data = _read_toml(config)
    config_root = _local_config_root(config_data, config_path=config)
    if config_root is not None:
        root = DatasetRoot(
            path=_normalize_root(config_root, base_dir=repo),
            source=str(config),
        )
        return _ensure_existing_root(root, require_exists=require_exists)

    raise _missing_root_error(config)


def _flatten_entries(config: Mapping[str, Any]) -> dict[str, dict[str, str]]:
    entries: dict[str, dict[str, str]] = {}

    def visit(prefix: str, value: Any) -> None:
        if not isinstance(value, dict):
            return
        if prefix and all(not isinstance(child, dict) for child in value.values()):
            string_fields: dict[str, str] = {}
            for key, child in value.items():
                if isinstance(child, str):
                    string_fields[key] = child
            if string_fields:
                entries[prefix] = string_fields
            return
        for key, child in value.items():
            if key == "datasets" and not prefix:
                continue
            child_prefix = f"{prefix}.{key}" if prefix else str(key)
            visit(child_prefix, child)

    visit("", config)
    return entries


def load_dataset_catalog(
    explicit_root: str | Path | None = None,
    *,
    env: Mapping[str, str] | None = None,
    repo_root: Path | None = None,
    config_path: Path | None = None,
    require_root_exists: bool = False,
) -> DatasetCatalog:
    repo = (repo_root or REPO_ROOT).resolve(strict=False)
    config = config_path or local_config_path(repo)
    root = resolve_dataset_root(
        explicit_root,
        env=env,
        repo_root=repo,
        config_path=config,
        require_exists=require_root_exists,
    )
    return DatasetCatalog(root=root, entries=_flatten_entries(_read_toml(config)))


def is_dataset_reference(value: str | Path) -> bool:
    return str(value).startswith(DATASET_REFERENCE_PREFIX)


def _safe_relative_path(value: str, *, label: str) -> Path:
    if not value.strip():
        raise ScriptError(f"{label} cannot be empty")
    path = Path(value)
    if path.is_absolute():
        raise ScriptError(f"{label} must be relative to the dataset root, got: {value}")
    if any(part == ".." for part in path.parts):
        raise ScriptError(f"{label} must not contain '..': {value}")
    return path


def _join_under_root(root: Path, relative_path: Path) -> Path:
    root_resolved = root.resolve(strict=False)
    candidate = (root_resolved / relative_path).resolve(strict=False)
    try:
        candidate.relative_to(root_resolved)
    except ValueError as exc:
        raise ScriptError(f"dataset path escapes dataset root: {relative_path}") from exc
    return candidate


def _resolve_catalog_field(catalog: DatasetCatalog, entry_name: str, field_name: str) -> Path:
    entry = catalog.entries.get(entry_name)
    if entry is None:
        known = ", ".join(sorted(catalog.entries)) or "none"
        raise ScriptError(f"unknown dataset entry {entry_name!r}; known entries: {known}")
    base_text = entry.get("path")
    if base_text is None:
        raise ScriptError(f"dataset entry {entry_name!r} must define path")
    if field_name == "path":
        field_text = ""
    else:
        field_text = entry.get(field_name)
        if field_text is None:
            known = ", ".join(sorted(entry))
            raise ScriptError(
                f"dataset entry {entry_name!r} has no field {field_name!r}; known fields: {known}"
            )

    base = _safe_relative_path(base_text, label=f"{entry_name}.path")
    suffix = (
        _safe_relative_path(field_text, label=f"{entry_name}.{field_name}")
        if field_text
        else Path()
    )
    return _join_under_root(catalog.root.path, base / suffix)


def resolve_dataset_reference(
    value: str | Path,
    explicit_root: str | Path | None = None,
    *,
    env: Mapping[str, str] | None = None,
    repo_root: Path | None = None,
    config_path: Path | None = None,
    require_root_exists: bool = False,
) -> Path:
    """Resolve `dataset:relative/path` or `dataset:entry.name:field` references."""

    text = str(value)
    if not text.startswith(DATASET_REFERENCE_PREFIX):
        raise ScriptError(f"not a dataset reference: {text}")
    body = text[len(DATASET_REFERENCE_PREFIX) :]
    if ":" in body:
        parts = body.split(":")
        if len(parts) != 2:
            raise ScriptError(f"dataset reference has too many ':' separators: {text}")
        entry_name, field_name = parts
        if not entry_name or not field_name:
            raise ScriptError(f"dataset catalog reference must be dataset:ENTRY:FIELD, got: {text}")
        catalog = load_dataset_catalog(
            explicit_root,
            env=env,
            repo_root=repo_root,
            config_path=config_path,
            require_root_exists=require_root_exists,
        )
        return _resolve_catalog_field(catalog, entry_name, field_name)

    root = resolve_dataset_root(
        explicit_root,
        env=env,
        repo_root=repo_root,
        config_path=config_path,
        require_exists=require_root_exists,
    )
    return _join_under_root(root.path, _safe_relative_path(body, label="dataset path"))


def resolve_path_reference(
    value: str | Path,
    explicit_root: str | Path | None = None,
    *,
    env: Mapping[str, str] | None = None,
    repo_root: Path | None = None,
    config_path: Path | None = None,
    require_root_exists: bool = False,
) -> Path:
    if is_dataset_reference(value):
        return resolve_dataset_reference(
            value,
            explicit_root,
            env=env,
            repo_root=repo_root,
            config_path=config_path,
            require_root_exists=require_root_exists,
        )
    return Path(value)


def resolve_path_references(
    values: list[str] | tuple[str, ...],
    explicit_root: str | Path | None = None,
    *,
    env: Mapping[str, str] | None = None,
    repo_root: Path | None = None,
    config_path: Path | None = None,
    require_root_exists: bool = False,
) -> list[Path]:
    return [
        resolve_path_reference(
            value,
            explicit_root,
            env=env,
            repo_root=repo_root,
            config_path=config_path,
            require_root_exists=require_root_exists,
        )
        for value in values
    ]
