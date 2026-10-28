"""Regression checks for the intentionally UNAPPROVED Moralisch Bankrott draft.

This is a test-only content bundle. Never turn it into an OTA/public release
without editorial review, rights/licence sign-off and human approval.
"""
from itertools import permutations
from pathlib import Path
import re
import unittest

from build_sd_pack import PackError, load_cards, load_json, validate
from card_text import normalized

ROOT = Path(__file__).resolve().parent
DECK_DIR = ROOT / "drafts" / "moralisch-bankrott-v3"


class MoralischBankrottDraftTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = load_json(DECK_DIR / "deck.json")
        cls.cards = load_cards(DECK_DIR / "cards.jsonl")
        cls.prompts = [c for c in cls.cards if c["kind"] == "prompt"]
        cls.answers = [c for c in cls.cards if c["kind"] == "answer"]

    def test_draft_pack_compiles_with_real_format_2_compiler(self):
        blob, receipt = validate(self.manifest, self.cards, draft=True)
        self.assertEqual(blob[:8], b"XCAWPK2\\0")
        self.assertTrue(receipt["draft_only"])
        self.assertEqual(receipt["prompts"], 24)
        self.assertEqual(receipt["answers"], 80)
        self.assertLessEqual(len(blob), 192 * 1024)

    def test_publishing_is_explicitly_blocked(self):
        self.assertEqual(self.manifest["editorial_status"], "unapproved")
        self.assertEqual(self.manifest["rights_status"], "unverified")
        self.assertTrue(all(c["reviewed"] is False for c in self.cards))
        with self.assertRaises(PackError):
            validate(self.manifest, self.cards, draft=False)

    def test_adult_category_defaults_off(self):
        categories = {c["id"]: c for c in self.manifest["categories"]}
        self.assertTrue(categories["ab-18"]["adult"])
        self.assertFalse(categories["ab-18"]["default_on"])
        self.assertTrue(all("ab-18" in c["categories"] for c in self.cards))

    def test_editorial_uniqueness_and_lengths(self):
        self.assertEqual(len(self.prompts), 24)
        self.assertEqual(len(self.answers), 80)
        self.assertEqual(len({c["id"] for c in self.cards}), len(self.cards))
        self.assertEqual(len({normalized(c["text"]) for c in self.cards}), len(self.cards))
        for answer in self.answers:
            self.assertLessEqual(len(answer["text"].encode("utf-8")), 160)
            self.assertNotRegex(answer["text"], r"[.!?]$")
            self.assertNotIn("____", answer["text"])
        for prompt in self.prompts:
            self.assertLessEqual(len(prompt["text"].encode("utf-8")), 384)
            self.assertEqual(prompt["text"].count("____"), prompt["pick"])
            self.assertNotRegex(prompt["text"], r"(?i)\\b(?:wegen|durch|ohne|anstelle von)\\s+____")

    def test_every_pick_one_and_pick_two_combination_is_structurally_valid(self):
        pick1 = [c for c in self.prompts if c["pick"] == 1]
        pick2 = [c for c in self.prompts if c["pick"] == 2]
        self.assertEqual((len(pick1), len(pick2)), (20, 4))
        tested = 0
        for prompt in pick1:
            for answer in self.answers:
                composed = prompt["text"].replace("____", answer["text"], 1)
                self.assertNotIn("____", composed)
                tested += 1
        for prompt in pick2:
            for first, second in permutations(self.answers, 2):
                composed = prompt["text"].replace("____", first["text"], 1).replace(
                    "____", second["text"], 1
                )
                self.assertNotIn("____", composed)
                tested += 1
        self.assertEqual(tested, 26880)


if __name__ == "__main__":
    unittest.main()
