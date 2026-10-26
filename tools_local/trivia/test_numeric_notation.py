#!/usr/bin/env python3
"""Offline regression tests for real 500-question German numeric edge cases."""
import unittest
from numeric_semantics import audit_field,numeric_tokens

class HistoricalTriviaNumerals(unittest.TestCase):
    def ok(self,en,de):
        result=audit_field(en,de)
        self.assertEqual(result["warnings"],[],str(result))
    def flagged(self,en,de):
        self.assertTrue(audit_field(en,de)["warnings"],(en,de))
    def test_century_ordinals(self):
        self.ok("named for a 16th century king","nach einem König des 16. Jahrhunderts")
        self.ok("real 19th c. logger","Holzfäller aus dem 19. Jahrhundert")
        self.ok("the 4th c.","im 4. Jahrhundert")
    def test_time_century_and_birthday(self):
        self.ok("celebrated 57th birthday March 2, 1988","feierte am 2. März 1988 den 57. Geburtstag")
    def test_ordinals_not_free_corrections(self):
        self.flagged("in the 13th century","im 14. Jahrhundert")
    def test_year_abbreviation_expansion(self):
        self.ok("1864-69 and 1867-68","1864 bis 1869 und 1867 bis 1868")
        self.flagged("1864-69","1864 bis 1868")
    def test_decimal_and_centennial(self):
        self.ok("100th anniversary in 1876","100. Jahrestag im Jahr 1876")
        self.ok("325th anniversary and Canada's centennial","325. Jubiläum und hundertjähriges Bestehen Kanadas")
    def test_date_separators(self):
        self.ok("Broadcast on 6/6/44","Am 6.6.44 gesendet")
        self.flagged("Broadcast on 6/6/44","Am 6.6.45 gesendet")
    def test_written_fractions(self):
        self.ok("about 3/4 of the imports","etwa drei Viertel der Importe")
        self.ok("about 2/3 of all traffic","etwa zwei Drittel des Verkehrs")
        self.flagged("about 3/4","etwa zwei Drittel")
    def test_mixed_fraction(self):
        self.ok("within 3 1/2 years","innerhalb von dreieinhalb Jahren")
        self.flagged("within 3 1/2 years","innerhalb von zweieinhalb Jahren")
    def test_german_number_inflection(self):
        self.ok("a 3-volume history","eine dreibändige Geschichte")
        self.ok("this 2-digit number","diese zweistellige Zahl")
        self.ok("the 1st of 4 seasons","der erste der vier Jahreszeiten")
    def test_numeric_in_scientific_notation(self):
        self.ok("100th anniversary","100. Jubiläum")
        self.assertIn("100",numeric_tokens("100. Jubiläum","de"))
    def test_same_value_wrong_unit(self):
        self.flagged("10 miles","10 Kilometer")
        self.ok("10 miles","16,09 Kilometer")
    def test_imperial_pound_ambiguous(self):
        self.flagged("a 450-pound tiger","ein 450 Pfund schwerer Tiger")
    def test_big_eight_not_model_code(self):
        self.ok("Big 8 university","Big-8-Universität")
    def test_proper_noun_one_not_an_added_number(self):
        self.ok("One","One")
    def test_english_and_de_title_digit(self):
        self.ok("A Tale of Two Cities","Eine Geschichte zweier Städte")
        self.flagged("The Mirror Has Two Faces","Das Geheimnis der Gefühle")
    def test_one_ounce_to_eine_unze(self):
        self.ok("less than 1 ounce","weniger als eine Unze")
    def test_ordinals_dont_hide_actual_changes(self):
        self.flagged("58th birthday","57. Geburtstag")
if __name__=="__main__":
    unittest.main()
