"""Quality gate for the user-approved, first-party adult pack.

Ensures the real SD compiler produces a firmware-accepted v2 release pack,
with no duplicated texts, unsafe category defaults, or impossible slots.
"""
import itertools
import json
from pathlib import Path
import unittest

from build_sd_pack import load_cards, load_json, validate
from card_text import normalized
import publish_packs

DIR = Path(__file__).resolve().parent / "published" / "moralisch-bankrott"


class MoralischBankrottPublishedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = load_json(DIR / "deck.json")
        cls.cards = load_cards(DIR / "deck.jsonl")
        cls.prompts = [x for x in cls.cards if x["kind"] == "prompt"]
        cls.answers = [x for x in cls.cards if x["kind"] == "answer"]

    def test_released_pack_is_accepted_by_production_compiler(self):
        blob, receipt = validate(self.manifest, self.cards)
        self.assertEqual(blob[:8], b"XCAWPK2\0")
        self.assertFalse(receipt["draft_only"])
        self.assertEqual((receipt["prompts"], receipt["answers"]), (74, 152))
        self.assertLessEqual(len(blob), 192 * 1024)

    def test_protection_and_attribution(self):
        self.assertEqual(self.manifest["license"], "CC0-1.0")
        self.assertEqual(self.manifest["rights_status"], "verified")
        self.assertEqual(self.manifest["editorial_status"], "approved")
        adult = [x for x in self.manifest["categories"] if x["id"] == "fsk-18"]
        self.assertEqual(len(adult), 1)
        self.assertTrue(adult[0]["adult"])
        self.assertFalse(adult[0]["default_on"])
        self.assertTrue(all("fsk-18" in x["categories"] for x in self.cards))

    def test_unique_cards_normalized_german_and_no_stray_punctuation(self):
        self.assertEqual(len({x["id"] for x in self.cards}), len(self.cards))
        self.assertEqual(len({(x["kind"], normalized(x["text"])) for x in self.cards}), len(self.cards))
        for x in self.cards:
            self.assertTrue(x["reviewed"])
            self.assertEqual(x["text"].strip(), x["text"])
            self.assertEqual(x["text"].count("____"), x.get("pick", 0))
            self.assertLessEqual(len(x["text"].encode("utf-8")), 384 if x["kind"] == "prompt" else 160)
            if x["kind"] == "answer":
                self.assertFalse(x["text"].endswith((".", "!", "?")))

    def test_public_config_rebuilds_identical_pinned_pack(self):
        config = json.loads((DIR.parent.parent / "public_packs.json").read_text(encoding="utf-8"))
        entry = next(p for p in config["packs"] if p["file"] == "Moralisch-Bankrott.dat")
        data, receipt = publish_packs.build(
            entry, {"moralisch-bankrott": str(DIR / "deck.json")}
        )
        self.assertEqual(receipt["sha256"], entry["sha256"])
        self.assertEqual(len(data), receipt["bytes"])
        self.assertEqual((receipt["prompts"], receipt["answers"]), (74, 152))

    def test_all_approved_slot_pairings_resolve(self):
        singles = [x for x in self.prompts if x["pick"] == 1]
        doubles = [x for x in self.prompts if x["pick"] == 2]
        self.assertEqual((len(singles), len(doubles)), (64, 10))
        tested = 0
        for prompt in singles:
            for answer in self.answers:
                combined = prompt["text"].replace("____", answer["text"], 1)
                self.assertNotIn("____", combined)
                tested += 1
        for prompt in doubles:
            for a,b in itertools.permutations(self.answers, 2):
                combined = prompt["text"].replace("____", a["text"], 1).replace("____", b["text"], 1)
                self.assertNotIn("____", combined)
                tested += 1
        self.assertEqual(tested, 239248)


if __name__ == "__main__":
    unittest.main()
