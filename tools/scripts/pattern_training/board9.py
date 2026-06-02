from __future__ import annotations

import hashlib
from pathlib import Path

from common import ScriptError


def board_key(board_text: str) -> str:
    return "\n".join(line.rstrip() for line in board_text.strip().splitlines())


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as input_file:
            for chunk in iter(lambda: input_file.read(65536), b""):
                digest.update(chunk)
    except OSError as exc:
        raise ScriptError(f"failed to read file for SHA256: {path}: {exc}") from exc
    return digest.hexdigest()


def board_hash(board_text: str) -> str:
    return sha256_text(board_key(board_text))

