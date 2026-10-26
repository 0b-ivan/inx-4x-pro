#!/usr/bin/env python3
"""Offline regression tests for validated German question packs (no API calls)."""
import json
import io
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

import build_de_pack
import export_pack
import pack_format
import translate_de_pack


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
        self.assertEqual(pack["flags"], pack_format.GERMAN_FLAG)
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

    def test_export_published_binary_can_be_translation_source(self):
        original_pack = self.root / "pack.dat"
        pack_format.write([self.original], original_pack)
        exported = self.root / "exported.jsonl"
        self.assertEqual(export_pack.export(original_pack, exported), 1)
        rows = list(build_de_pack.load_jsonl(exported))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["q"], self.original["q"])
        self.assertEqual(rows[0]["a"], self.original["a"])
        self.assertEqual(rows[0]["w"], self.original["w"])
        self.assertEqual(len(rows[0]["id"]), 16)
        # Stable export: same inputs produce the same ID.
        again = self.root / "again.jsonl"
        export_pack.export(original_pack, again)
        self.assertEqual(exported.read_bytes(), again.read_bytes())

    def test_export_rejects_german_binary_as_english_source(self):
        pack_format.write([self.original], self.root / "de.dat",
                          flags=pack_format.GERMAN_FLAG)
        with self.assertRaisesRegex(ValueError, "English pack"):
            export_pack.export(self.root / "de.dat", self.root / "bad.jsonl")

    def test_diverse_pilot_samples_across_full_corpus(self):
        source = [
            {"id": f"q-{i}", "a": f"Answer {i // 10}", "q": f"Question {i}"}
            for i in range(200)
        ]
        selected = translate_de_pack.pilot_candidates(source, 20)
        self.assertEqual(len(selected), 20)
        self.assertEqual(len({item["a"] for item in selected}), 20)
        self.assertNotEqual([x["id"] for x in selected],
                            [x["id"] for x in source[:20]])
        self.assertEqual(selected, translate_de_pack.pilot_candidates(source, 20))
        further = translate_de_pack.pilot_candidates(source, 10,
                            {item["id"] for item in selected})
        self.assertEqual(len(further), 10)
        self.assertFalse({item["id"] for item in selected} &
                         {item["id"] for item in further})

    def test_pilot_handles_duplicate_answers_in_small_source(self):
        source = [{"id": str(i), "a": "SAME"} for i in range(5)]
        self.assertEqual(len(translate_de_pack.pilot_candidates(source, 4)), 4)
        self.assertEqual(translate_de_pack.pilot_candidates(source, 2, {"0","1","2","3","4"}), [])

    def test_normalize_unsupported_smart_punctuation(self):
        text = '„München“ – die Stadt … 12\u202fkm'
        self.assertEqual(translate_de_pack.normalize_display_punctuation(text),
                         '"München" - die Stadt ... 12 km')
        self.assertEqual(translate_de_pack.normalize_display_punctuation("für"), "für")
        self.assertEqual(translate_de_pack.normalize_display_punctuation("Österreich"), "Österreich")

    def test_openrouter_requests_strict_structured_json(self):
        supplied = {k: self.trans[k] for k in ("q", "a", "alt", "w")}
        fake_response = {"choices": [{"message": {"content":
                         json.dumps(supplied, ensure_ascii=False)}}]}
        captured = []
        def fake_open(request, timeout):
            captured.append(json.loads(request.data.decode("utf-8")))
            return io.BytesIO(json.dumps(fake_response, ensure_ascii=False).encode("utf-8"))

        with patch.object(translate_de_pack.urllib.request, "urlopen", fake_open):
            returned = translate_de_pack.translate_one(
                self.original, "anthropic/claude-sonnet-4.6", "not-a-real-key")
        self.assertEqual(returned, supplied)
        self.assertEqual(len(captured), 1)
        body = captured[0]
        self.assertEqual(body["model"], "anthropic/claude-sonnet-4.6")
        self.assertTrue(body["provider"]["require_parameters"])
        self.assertEqual(body["response_format"]["type"], "json_schema")
        schema = body["response_format"]["json_schema"]
        self.assertTrue(schema["strict"])
        self.assertEqual(set(schema["schema"]["required"]), {"q", "a", "alt", "w"})
        self.assertFalse(schema["schema"]["additionalProperties"])
        self.assertGreaterEqual(body["max_tokens"], 1024)

    def test_pilot_resumes_and_replaces_a_display_invalid_question(self):
        source = []
        for i in range(12):
            row = dict(self.original)
            row["id"] = f"q{i}"
            row["a"] = f"Correct {i}"
            row["q"] = f"Which city is number {i}?"
            source.append(row)
        self.src.write_text("".join(json.dumps(item) + "\\n" for item in source),
                            encoding="utf-8")
        first = source[0]
        checkpoint = {"id": first["id"], "source_hash": build_de_pack.source_hash(first),
                      "q": first["q"], "a": first["a"],
                      "alt": [], "w": first["w"], "reviewed": False}
        self.translated.write_text(json.dumps(checkpoint) + "\\n", encoding="utf-8")
        sample = translate_de_pack.pilot_candidates(source, 2 * 3 + 10, {first["id"]})
        rejected_id = sample[0]["id"]
        attempts = []
        def fake_translator(item, model, key):
            attempts.append(item["id"])
            return {"q": item["q"], "a": ("X" * 500 if item["id"] == rejected_id else item["a"]),
                    "alt": [], "w": item["w"]}
        with patch.object(translate_de_pack, "translate_one", fake_translator), \\
                patch.object(translate_de_pack.time, "sleep", lambda _: None):
            new_count, total_count, count = translate_de_pack.generate(
                self.src, self.translated, "fake-model", "unused", limit=2)
        self.assertEqual((new_count, total_count, count), (2, 3, 12))
        self.assertEqual(attempts.count(rejected_id), 2)
        output = list(build_de_pack.load_jsonl(self.translated))
        self.assertEqual(len(output), 3)
        self.assertEqual(len({row["id"] for row in output}), 3)
        self.assertNotIn(rejected_id, {row["id"] for row in output})

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
