#!/usr/bin/env python3
"""No-network numeric formatting and semantic QA regressions."""
import unittest
from numeric_semantics import audit_field,audit_record,number_value,numeric_tokens

class NumericSemanticsTests(unittest.TestCase):
    def assertClean(self,en,de):
        self.assertEqual(audit_field(en,de)["warnings"],[],(en,de))
    def assertFlagged(self,en,de):
        self.assertTrue(audit_field(en,de)["warnings"],(en,de))
    def test_locale_decimals(self):
        self.assertClean("0.01","0,01")
        self.assertClean("6.5","6,5")
    def test_thousands(self):
        self.assertClean("14,433","14.433")
        self.assertClean("50,000 people","50.000 Menschen")
        self.assertClean("100,000","100.000")
    def test_written_numbers(self):
        self.assertClean("five years","fünf Jahre")
        self.assertClean("twenty five years","fünfundzwanzig Jahre")
        self.assertClean("one hundred","einhundert")
    def test_indefinite_article(self):
        self.assertClean("one of the books in 1825","eines der Bücher 1825")
    def test_verified_length_conversion(self):
        result=audit_field("a height of 14,433 feet","eine Höhe von 4.399 Metern")
        self.assertEqual(result["warnings"],[])
        self.assertEqual(len(result["notes"]),1)
    def test_wrong_conversion(self):
        self.assertFlagged("14,433 feet","5.399 Meter")
    def test_imperial_metric_distance(self):
        self.assertClean("10 miles","16,09 Kilometer")
    def test_inch_to_centimeter(self):
        self.assertClean("12 inches","30,48 Zentimeter")
    def test_pounds_to_kilos(self):
        self.assertClean("10 pounds","4,536 Kilogramm")
    def test_pound_ambiguous(self):
        self.assertFlagged("10 pounds","5 Pfund")
    def test_temperature(self):
        self.assertClean("68 °F","20 °C")
        self.assertClean("0 °C","32 °F")
    def test_actual_change(self):
        self.assertFlagged("10 days in 1947","101 Tage im Jahr 1947")
        self.assertFlagged("in 1950","im Jahr 1951")
    def test_units_without_matching(self):
        self.assertFlagged("10 miles","10 Kilometer")
    def test_ignores_model_labels(self):
        self.assertClean("An assault rifle","Ein AK-47-Gewehr")
    def test_option_slot_separation(self):
        self.assertFlagged(["6","7"],["sieben","sechs"])
    def test_can_not_hide_other_number(self):
        self.assertFlagged("14,433 feet in 1990","4.399 Meter im Jahr 1991")
    def test_unfamiliar_punctuation_conservative(self):
        self.assertIsNone(number_value("1.23.45","de"))

if __name__=="__main__":
    unittest.main()
