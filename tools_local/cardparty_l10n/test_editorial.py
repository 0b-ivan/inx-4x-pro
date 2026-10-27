"""Editorial heuristic regression; manual semantic sign-off is still required."""
import unittest
from itertools import permutations
from pathlib import Path

from editorial_checks import audit, lint_card
from translate_cards import load_source, load_drafts, CardError

HERE = Path(__file__).resolve().parent


class EditorialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest, cls.source = load_source(
            HERE / "examples/deck.json", HERE / "examples/cards-en.jsonl")
        cls.raw = load_drafts(HERE / "examples/cards-de-unreviewed.jsonl",
                              cls.manifest, cls.source)
        cls.edited = load_drafts(HERE / "examples/cards-de-editorial-proposed.jsonl",
                                 cls.manifest, cls.source)

    def test_original_ai_drafts_have_known_grammar_failures(self):
        problems = audit(self.manifest, self.source, self.raw)
        errors = {(p["id"], p["code"]) for p in problems}
        for ident in ("prompt-001", "prompt-003", "prompt-004", "prompt-006"):
            self.assertIn((ident, "case_governing_blank"), errors)
        for i in range(1,15):
            self.assertIn((f"answer-{i:03}", "answer_terminal_punctuation"), errors)
        self.assertIn(("answer-009", "alarm_not_clock"), errors)

    def test_proposals_have_no_known_lints(self):
        self.assertEqual(len(self.edited), 20)
        self.assertEqual(audit(self.manifest, self.source, self.edited), [])
        self.assertTrue(all(r["reviewed"] is False for r in self.edited.values()))

    def test_each_prompt_retains_original_pick_draw_and_hash(self):
        for key, original in self.source.items():
            translated = self.edited[key]
            self.assertEqual(translated["source_hash"], self.raw[key]["source_hash"])
            self.assertEqual(translated["id"], key)
            if original["kind"] == "prompt":
                self.assertEqual(translated["text"].count("____"), original["pick"])
                if original["pick"] > 1:
                    self.assertEqual(translated["text"].count(" | "), original["pick"] - 1)
                self.assertNotIn(".", translated["text"][-4:])
            else:
                self.assertNotIn("____", translated["text"])

    def test_all_ordered_english_deck_combinations_render_without_punctuation_collisions(self):
        # 3 Pick-1 questions * 14, 2 Pick-2 * 14P2,
        # and 1 Pick-3 * 14P3 = 2,590 complete combinations.
        answers = [self.edited[key]["text"] for key in self.source
                   if self.source[key]["kind"] == "answer"]
        total = 0
        for key, original in self.source.items():
            if original["kind"] != "prompt":
                continue
            question = self.edited[key]["text"]
            for chosen in permutations(answers, original["pick"]):
                rendered = question
                for answer in chosen:
                    rendered = rendered.replace("____", answer, 1)
                self.assertNotIn("____", rendered)
                self.assertNotIn(". und", rendered)
                self.assertNotIn("..", rendered)
                self.assertNotIn(". |", rendered)
                self.assertNotIn("?.", rendered)
                self.assertLessEqual(len(rendered.encode("utf-8")), 512)
                total += 1
        self.assertEqual(total, 2590)

    def test_lint_catches_regressions_without_automatic_approval(self):
        original = self.source["answer-009"]
        proposal = self.edited["answer-009"]
        self.assertEqual(lint_card(original, proposal), [])
        broken = dict(proposal, text="Ein Wecker, der nur an Feiertagen klingelt.")
        codes = {x["code"] for x in lint_card(original, broken)}
        self.assertIn("alarm_not_clock", codes)
        self.assertIn("answer_terminal_punctuation", codes)


if __name__ == "__main__":
    unittest.main(verbosity=2)
