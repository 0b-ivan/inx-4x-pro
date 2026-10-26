#!/usr/bin/env python3
"""Offline regression checks for paid German trivia batch safeguards."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import translate_de_pack as tr
from build_de_pack import TranslationError


class BudgetTests(unittest.TestCase):
    def test_no_api_request_when_checkpoint_spent_budget(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "source.jsonl"
            out = Path(d) / "draft.jsonl"
            src.write_text(json.dumps({"id": "x", "q": "Q?", "a": "A", "alt": [], "w": []}) + "\n")
            out.with_suffix(out.suffix + ".cost.json").write_text('{"spent_usd":4.95}')
            with patch.object(tr, "translate_one", side_effect=AssertionError("API called")):
                count, _, _ = tr.generate(src, out, "mock", "secret", limit=1, max_cost_usd=5)
            self.assertEqual(count, 0)

    def test_missing_ledger_refuses_existing_drafts(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "source.jsonl"
            out = Path(d) / "draft.jsonl"
            src.write_text(json.dumps({"id": "x", "q": "Q?", "a": "A", "alt": [], "w": []}) + "\n")
            out.write_text('{"id":"x"}\n')
            with self.assertRaises(TranslationError):
                tr.generate(src, out, "mock", "secret", limit=1, max_cost_usd=5)


if __name__ == "__main__":
    unittest.main()
