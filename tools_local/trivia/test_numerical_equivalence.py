#!/usr/bin/env python3
"""Regression tests for English/German numeric equivalence."""
import unittest
from numerical_equivalence import numeric_facts,numeric_warnings

class Numbers(unittest.TestCase):
    def test_digit_to_german_written_number(self):
        self.assertEqual(numeric_facts("5 years and 25 people"),
                         numeric_facts("fünf Jahre und fünfundzwanzig Personen"))
    def test_english_written_to_german_written(self):
        self.assertEqual(numeric_facts("twenty-five and three"),
                         numeric_facts("fünfundzwanzig und drei"))
    def test_german_indefinite_article_not_one(self):
        self.assertEqual(numeric_facts("Ein Krieg endete 1950"),
                         numeric_facts("A war ended in 1950"))
    def test_detects_numeric_drift(self):
        self.assertNotEqual(numeric_facts("A war ended 1950"),
                            numeric_facts("Ein Krieg endete 1951"))
    def test_detects_kon_tiki_regression(self):
        self.assertTrue(numeric_warnings(
            {"q":"The voyage ended 10 days later in 1947"},
            {"q":"Die Reise endete 101 Tage später im Jahr 1947"}))
    def test_accepted_spelling_variants(self):
        self.assertEqual(numeric_facts("One hundred people and seven"),
                         numeric_facts("Einhundert Menschen und sieben"))
    def test_nested_list(self):
        self.assertEqual(numeric_facts(["twenty two","fünf"]),numeric_facts(["22","5"]))
    def test_no_false_metadata_approval(self):
        self.assertEqual(numeric_warnings(
            {"q":"How many years?","a":"Five","alt":[],"w":["Six"]},
            {"q":"Wie viele Jahre?","a":"Fünf","alt":[],"w":["Sechs"]}),[])
    def test_detects_translated_distractor_change(self):
        self.assertEqual([w["field"] for w in numeric_warnings(
            {"q":"Choose","a":"Five","alt":[],"w":["Six","Seven"]},
            {"q":"Wähle","a":"fünf","alt":[],"w":["sechs","acht"]})],["w"])
if __name__=="__main__":
    unittest.main()
