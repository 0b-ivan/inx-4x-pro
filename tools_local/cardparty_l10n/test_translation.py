#!/usr/bin/env python3
"""Offline card-localization contract suite: never calls a network provider."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from translate_cards import (
    CardError, generate, load_drafts, load_source, source_hash, validate_draft,
)

HERE = Path(__file__).resolve().parent


class TranslationTests(unittest.TestCase):
    def setUp(self):
        self.manifest, self.source = load_source(
            HERE / "examples/deck.json", HERE / "examples/cards-en.jsonl")
        self.prompt = self.source["prompt-004"]
        self.answer = self.source["answer-001"]

    def row(self, source=None, translated=None, reviewed=False):
        source = source or self.prompt
        text = translated or source["text"].replace("The security audit discovered",
                                                      "Das Sicherheits-Audit fand")
        return {"id": source["id"], "source_hash": source_hash(self.manifest, source),
                "locale": "de", "text": text, "reviewed": reviewed}

    def test_original_rights_and_source_counts(self):
        self.assertEqual(len(self.source), 20)
        self.assertEqual(len([v for v in self.source.values() if v["kind"]=="prompt"]), 6)
        self.assertEqual(self.manifest["source_locale"], "en")
        self.assertEqual(self.manifest["license"], "CC0-1.0")

    def test_placeholder_and_rules_are_immutable(self):
        self.assertIsNone(validate_draft(self.manifest, self.prompt, self.row()))
        for bad in ["Ein Test mit ____ und ____", "Nur eine Lücke ____",
                    "Ein Test ohne Platzhalter"]:
            with self.assertRaisesRegex(CardError, "placeholder"):
                validate_draft(self.manifest, self.prompt, self.row(translated=bad))
        with self.assertRaisesRegex(CardError, "placeholder"):
            validate_draft(self.manifest, self.answer,
                           self.row(self.answer, "____"))

    def test_source_hash_changes_on_rules_rights_and_text(self):
        stale = self.row()
        changed = dict(self.prompt, pick=2)
        self.assertNotEqual(source_hash(self.manifest, self.prompt),
                            source_hash(self.manifest, changed))
        with self.assertRaisesRegex(CardError, "source or rights changed"):
            validate_draft(self.manifest, changed, stale)
        modified_manifest = dict(self.manifest, license="MIT")
        with self.assertRaisesRegex(CardError, "source or rights changed"):
            validate_draft(modified_manifest, self.prompt, stale)
        modified_source = dict(self.prompt, text="New meaning ____")
        with self.assertRaisesRegex(CardError, "source or rights changed"):
            validate_draft(self.manifest, modified_source, stale)

    def test_release_never_accepts_unreviewed_or_partial(self):
        with tempfile.TemporaryDirectory() as d:
            file = Path(d) / "drafts.jsonl"
            file.write_text(json.dumps(self.row(), ensure_ascii=False)+"\n",encoding="utf-8")
            self.assertEqual(len(load_drafts(file, self.manifest, self.source)), 1)
            with self.assertRaisesRegex(CardError, "unreviewed"):
                load_drafts(file, self.manifest, self.source, release=True)
            reviewed = self.row(reviewed=True)
            file.write_text(json.dumps(reviewed, ensure_ascii=False)+"\n",encoding="utf-8")
            with self.assertRaisesRegex(CardError, "incomplete"):
                load_drafts(file, self.manifest, self.source, release=True)

    def test_duplicate_unknown_and_tampered_drafts_fail_closed(self):
        with tempfile.TemporaryDirectory() as d:
            file = Path(d) / "drafts.jsonl"
            record = json.dumps(self.row(),ensure_ascii=False)
            file.write_text(record+"\n"+record+"\n",encoding="utf-8")
            with self.assertRaisesRegex(CardError, "duplicate"):
                load_drafts(file,self.manifest,self.source)
            rogue = self.row()
            rogue["id"] = "answer-does-not-exist"
            file.write_text(json.dumps(rogue)+"\n",encoding="utf-8")
            with self.assertRaisesRegex(CardError, "unknown"):
                load_drafts(file,self.manifest,self.source)
            extra = self.row()
            extra["approved"] = True
            file.write_text(json.dumps(extra)+"\n",encoding="utf-8")
            with self.assertRaisesRegex(CardError, "missing/unexpected"):
                load_drafts(file,self.manifest,self.source)

    def test_incremental_checkpoint_and_review_not_automatic(self):
        with tempfile.TemporaryDirectory() as d:
            file=Path(d)/"drafts.jsonl"
            def fake_translate(manifest, card, model, token):
                return card["text"].replace("The ", "Die ")
            with patch("translate_cards.translate_one", side_effect=fake_translate) as fake:
                self.assertEqual(generate(self.manifest,self.source,{},file,"offline-mock",
                                          "not-a-real-api-key",3),3)
                saved=load_drafts(file,self.manifest,self.source)
                self.assertEqual(len(saved),3)
                self.assertTrue(all(item["reviewed"] is False for item in saved.values()))
                self.assertEqual(generate(self.manifest,self.source,saved,file,"offline-mock",
                                          "not-a-real-api-key",2),2)
                self.assertEqual(len(load_drafts(file,self.manifest,self.source)),5)
                self.assertEqual(fake.call_count,5)
            with self.assertRaisesRegex(CardError,"limit"):
                generate(self.manifest,self.source,{},file,"x","never-used",21)

    def test_source_rejects_invalid_license_and_duplicate_id(self):
        with tempfile.TemporaryDirectory() as d:
            manifest=Path(d)/"deck.json"
            cards=Path(d)/"cards.jsonl"
            lines=(HERE/"examples/cards-en.jsonl").read_text(encoding="utf-8")
            cards.write_text(lines,encoding="utf-8")
            bad=dict(self.manifest,license="proprietary-no-rights")
            manifest.write_text(json.dumps(bad),encoding="utf-8")
            with self.assertRaisesRegex(CardError,"license"):
                load_source(manifest,cards)
            manifest.write_text(json.dumps(self.manifest),encoding="utf-8")
            cards.write_text(lines+lines.splitlines()[0]+"\n",encoding="utf-8")
            with self.assertRaisesRegex(CardError,"duplicate"):
                load_source(manifest,cards)


if __name__ == "__main__":
    unittest.main(verbosity=2)
