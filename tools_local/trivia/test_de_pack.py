#!/usr/bin/env python3
"""Offline regression tests for validated German question packs (no API calls)."""
import json
import tempfile
import unittest
from pathlib import Path

import build_de_pack
import pack_format


class GermanTriviaPackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.src = self.root / "source.jsonl"
        self.translated = self.root / "de.jsonl"
        self.output = self.root / "pack-de.dat"
        self.original = {
            "id": "f123456789ab", "d": 2, "y": 2025,
            "q": "Which of these European capitals is located in France?",
            "a": "Paris", "alt": ["City of Paris"],
            "w": ["Berlin", "Madrid", "Rome"],
        }
        self.trans = {
            "id": self.original["id"],
            "source_hash": build_de_pack.source_hash(self.original),
            "reviewed": True,
            "q": "Welche dieser europäischen Hauptstädte liegt in Frankreich?",
            "a": "Paris", "alt": ["Stadt Paris"],
            "w": ["Berlin", "Madrid", "Rom"],
        }
        self.write()

    def write(self):
        self.src.write_text(json.dumps(self.original, ensure_ascii=False) + "\n", encoding="utf-8")
        self.translated.write_text(json.dumps(self.trans, ensure_ascii=False) + "\n", encoding="utf-8")

    def test_builds_pack_readable_by_reference_reader(self):
        n, size, solo = build_de_pack.build(self.src, self.translated, self.output)
        self.assertEqual((n, solo), (1, 1))
        self.assertEqual(size, self.output.stat().st_size)
        pack = pack_format.open_pack(self.output)
        try:
            row = pack_format.read_one(pack, 0)
            self.assertEqual(row["q"], self.trans["q"])
            self.assertEqual(row["a"], self.trans["a"])
            self.assertEqual(row["alt"], self.trans["alt"])
            self.assertEqual(row["w"], self.trans["w"])
            self.assertEqual((row["d"], row["y"]), (2, 2025))
        finally:
            pack["f"].close()

    def test_requires_review_before_release(self):
        self.trans["reviewed"] = False
        self.write()
        with self.assertRaisesRegex(build_de_pack.TranslationError, "unreviewed"):
            build_de_pack.build(self.src, self.translated, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_changed_source(self):
        self.original["w"][0] = "Vienna"
        self.write()
        with self.assertRaisesRegex(build_de_pack.TranslationError, "source changed"):
            build_de_pack.build(self.src, self.translated, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_wrong_option_count(self):
        self.trans["w"] = ["Rom", "Madrid"]
        self.write()
        with self.assertRaisesRegex(build_de_pack.TranslationError, "count/order"):
            build_de_pack.build(self.src, self.translated, self.output)

    def test_rejects_duplicate_correct_distractor(self):
        self.trans["w"] = ["Paris", "Madrid", "Rom"]
        self.write()
        with self.assertRaisesRegex(build_de_pack.TranslationError, "distractor"):
            build_de_pack.build(self.src, self.translated, self.output)

    def test_rejects_too_large_record_without_output(self):
        self.trans["w"][0] = "Berlin" * 100
        self.write()
        with self.assertRaisesRegex(build_de_pack.TranslationError, "record is"):
            build_de_pack.build(self.src, self.translated, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_missing_translations_and_no_english_fallback(self):
        self.translated.write_text("", encoding="utf-8")
        with self.assertRaisesRegex(build_de_pack.TranslationError, "missing German"):
            build_de_pack.build(self.src, self.translated, self.output)

    def test_draft_only_with_explicit_override(self):
        self.trans["reviewed"] = False
        self.write()
        n, _, _ = build_de_pack.build(self.src, self.translated,
                                      self.root / "sample-draft.dat", allow_draft=True)
        self.assertEqual(n, 1)

    def test_english_state_filename_is_not_reused(self):
        # English /trivia/pack.state is not a suitable progress file for the
        # German deck, even if both currently have the same question count.
        self.assertNotEqual(pack_format.state_path("/trivia/pack-de.dat"),
                            pack_format.state_path("/trivia/pack.dat"))


if __name__ == "__main__":
    unittest.main()
