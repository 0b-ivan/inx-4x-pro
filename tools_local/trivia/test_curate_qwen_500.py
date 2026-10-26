#!/usr/bin/env python3
"""Offline assertions around the human editorial proposal catalog."""
import unittest
from curate_qwen_500 import EDITS,FIELDS

class ProposedEdits(unittest.TestCase):
    def test_all_keys_distinct_and_nonempty(self):
        self.assertEqual(len(EDITS),len(set(EDITS)))
        self.assertGreaterEqual(len(EDITS),15)
        self.assertTrue(all(len(key)==16 for key in EDITS))
    def test_no_approval_or_release_fields(self):
        for value in EDITS.values():
            self.assertNotIn("reviewed",value)
            self.assertNotIn("human_approved",value)
            self.assertNotIn("firmware_eligible",value)
            self.assertNotIn("source_hash",value)
            self.assertTrue(set(value).issubset(set(FIELDS)|{"reason","evidence","needs_fact_check"}))
    def test_evidence_for_film_and_nobel(self):
        self.assertGreater(len(EDITS["054ed3eaa4413aac"]["evidence"]),0)
        self.assertGreater(len(EDITS["c7a58d162a0b6969"]["evidence"]),0)
    def test_translate_450_lb_to_kg_not_german_pfund(self):
        text=EDITS["da1fc2b6081eec3a"]["q"]
        self.assertIn("204 Kilogramm",text)
        self.assertNotIn("450 Pfund",text)
    def test_ben_hur_answer_not_ambiguous(self):
        self.assertEqual(EDITS["eeb6dc78558be1cb"]["a"],"Streitwagen")
        self.assertEqual(EDITS["eeb6dc78558be1cb"]["alt"],["Ein Streitwagen"])
    def test_nobel_does_not_leak_answer(self):
        q=EDITS["c7a58d162a0b6969"]["q"]
        self.assertNotIn("Arafat",q)
        self.assertIn("1994",q)

    def test_comment_marco_polo_e8(self):
        q=EDITS["b326216f49e428a8"]["q"]
        self.assertIn("Reisen nach Osten",q)
        self.assertNotIn("nach dem Osten",q)
        self.assertTrue(q.endswith("?"))
    def test_comment_rocky_mountains_h15(self):
        proposal=EDITS["c6d50209cfa9459e"]
        self.assertEqual(proposal["a"],"Rocky Mountains")
        self.assertIn("Rockies",proposal["alt"])
    def test_comment_metric_hummingbird_e13(self):
        proposal=EDITS["79942ab2044834cc"]
        self.assertIn("28,35 Gramm",proposal["q"])
        self.assertNotIn("Unze",proposal["q"])

if __name__=="__main__":
    unittest.main()
