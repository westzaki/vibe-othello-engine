from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import sys

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import common  # noqa: E402


class CommonTests(unittest.TestCase):
    def test_parse_csv_values_rejects_empty_segments(self) -> None:
        self.assertEqual(common.parse_csv_values("a, b"), ["a", "b"])

        with self.assertRaises(common.ScriptError):
            common.parse_csv_values("a,,b")

    def test_parse_csv_paths_returns_paths(self) -> None:
        self.assertEqual(common.parse_csv_paths("a.eval, b.eval"), [Path("a.eval"), Path("b.eval")])

    def test_slugify_keeps_stable_fallback(self) -> None:
        self.assertEqual(common.slugify("frontier/open 2"), "frontier-open-2")
        self.assertEqual(common.slugify("!!!", fallback="preset"), "preset")

    def test_read_jsonl_skips_blank_lines_and_requires_objects(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "records.jsonl"
            path.write_text('{"name":"a"}\n\n{"name":"b"}\n', encoding="utf-8")

            self.assertEqual(common.read_jsonl(path, require_object=True), [{"name": "a"}, {"name": "b"}])

    def test_write_report_section_appends_heading_body_and_blank_line(self) -> None:
        lines: list[str] = []

        common.write_report_section(lines, "Summary", ["- one", "- two"])

        self.assertEqual(lines, ["## Summary", "", "- one", "- two", ""])


if __name__ == "__main__":
    unittest.main()
