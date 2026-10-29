"""Offline regressions for the BixelPitch importer and the source merge.
Fixture text is our own."""
from pathlib import Path
import json
import tempfile
import unittest

import build_sd_pack as pack
import card_text
import import_bixelpitch as bixel
import import_prior99 as prior
import merge_cards as merger

def bixel_source(root,black,white,language="de"):
    path=root/"dist/de"
    path.mkdir(parents=True)
    (path/"grundspiel.json").write_text(json.dumps(
        {"name":"Grundspiel","language":language,"black_cards":black,"white_cards":white}),encoding="utf-8")

BLACK=["Ich bin ___. Frag mich.","Warum nur?","Aus ___ wird ___.",
       "___ + ___\\n= ___.","Der Bauchtrainer™ hilft bei ___."]
WHITE=["Ein Stern","Ein Stern.","Kaffee","Ein Plan"]+[f"Antwort {i}" for i in range(40)]

class CardTextTests(unittest.TestCase):
    def test_tidy_blanks_spaces_and_glyph_fallback(self):
        self.assertEqual(card_text.tidy("Ich  bin ___ ."),"Ich bin ____.")
        self.assertEqual(card_text.tidy("A______B"),"A ____ B")
        self.assertEqual(card_text.tidy("Trainer™"),"Trainer(TM)")
        self.assertEqual(card_text.normalized("Ein Stern."),card_text.normalized("ein stern"))
        self.assertNotEqual(card_text.normalized("Ein Stern"),card_text.normalized("Ein Sterne"))

class BixelPitchTests(unittest.TestCase):
    def convert(self,black=BLACK,white=WHITE,**kw):
        with tempfile.TemporaryDirectory() as tmp:
            bixel_source(Path(tmp),black,white,**kw)
            return bixel.convert(tmp)

    def test_picks_from_blanks_and_cleanup(self):
        cards,issues,dups=self.convert()
        self.assertEqual(issues,[])
        prompts={c["text"]:(c["pick"],c["draw"]) for c in cards if c["kind"]=="prompt"}
        self.assertEqual(prompts["Ich bin ____. Frag mich."],(1,0))
        self.assertEqual(prompts["Warum nur?"],(1,0))
        self.assertEqual(prompts["Aus ____ wird ____."],(2,0))
        self.assertEqual(prompts["____ + ____ = ____."],(3,2))
        self.assertIn("Der Bauchtrainer(TM) hilft bei ____.",prompts)
        # "Ein Stern" and "Ein Stern." are one card.
        self.assertEqual(len(dups),1)
        self.assertTrue(all(c["categories"]==["basis"] and not c["reviewed"] for c in cards))

    def test_rejects_wrong_pack_and_reports_bad_cards(self):
        with self.assertRaises(bixel.ImportError_):
            self.convert(language="en")
        cards,issues,_=self.convert(black=["Gut ___."],white=["Mit ___ Lücke",7]+WHITE)
        self.assertEqual(len(issues),2)

    def test_manifest_records_license_but_stays_unreleasable(self):
        cards,_,_=self.convert()
        manifest=bixel.manifest("abc")
        self.assertEqual(manifest["license"],"CC-BY-NC-SA-2.0")
        with self.assertRaises(pack.PackError):
            pack.validate(manifest,cards)
        pack.validate(manifest,cards,draft=True)

class MergeTests(unittest.TestCase):
    def setUp(self):
        self.prior_manifest=prior.manifest("p")
        self.prior_cards=[
            {"id":"p-a-1","kind":"answer","text":"Ein Stern.","categories":["boese"],"reviewed":False,"source":"prior99"},
            {"id":"p-a-2","kind":"answer","text":"Nur Prior.","categories":["fsk-18"],"reviewed":False,"source":"prior99"},
            {"id":"p-p-1","kind":"prompt","text":"Aus ____ wird ____.","pick":2,"draw":0,
             "categories":["basis"],"reviewed":False,"source":"prior99"}]
        with tempfile.TemporaryDirectory() as tmp:
            bixel_source(Path(tmp),BLACK,WHITE)
            self.bixel_cards,_,_=bixel.convert(tmp)
        self.bixel_manifest=bixel.manifest("b")

    def test_duplicates_merge_categories_and_sources(self):
        cards,manifest,report=merger.merge([(self.prior_manifest,self.prior_cards),
                                            (self.bixel_manifest,self.bixel_cards)],"mix-de","Mix")
        star=[c for c in cards if card_text.normalized(c["text"])=="ein stern"]
        self.assertEqual(len(star),1)
        self.assertEqual(star[0]["text"],"Ein Stern.")  # earlier input wins
        self.assertEqual(star[0]["categories"],["boese","basis"])
        self.assertEqual(star[0]["sources"],["prior99-de","bixelpitch-de"])
        self.assertEqual(report["per_source"]["bixelpitch-de"]["shared"],2)
        self.assertEqual(report["pick_conflicts"],[])
        self.assertEqual([c["id"] for c in manifest["categories"]][:2],["basis","boese"])

    def test_license_kept_only_when_all_sources_agree(self):
        _,manifest,_=merger.merge([(self.prior_manifest,self.prior_cards),
                                   (self.bixel_manifest,self.bixel_cards)],"mix-de","Mix")
        self.assertIsNone(manifest["license"])
        self.assertEqual(manifest["rights_status"],"unverified")
        self.assertEqual(len(manifest["sources"]),2)
        _,alone,_=merger.merge([(self.bixel_manifest,self.bixel_cards)],"b-de","B")
        self.assertEqual(alone["license"],"CC-BY-NC-SA-2.0")

    def test_adult_wins_and_pick_conflicts_are_reported(self):
        other=dict(self.bixel_manifest)
        other["categories"]=[{"id":"fsk-18","name":"FSK 18","default_on":True,"adult":False}]
        conflict=[{"id":"x","kind":"prompt","text":"Aus ____ wird ____!","pick":1,"draw":0,
                   "categories":["fsk-18"],"reviewed":False}]
        cards,manifest,report=merger.merge([(self.prior_manifest,self.prior_cards),(other,conflict)],"m","M")
        fsk=[c for c in manifest["categories"] if c["id"]=="fsk-18"][0]
        self.assertTrue(fsk["adult"]);self.assertFalse(fsk["default_on"])
        self.assertEqual(len(report["pick_conflicts"]),1)
        self.assertEqual([c["pick"] for c in cards if c["kind"]=="prompt"],[2])

    def test_merged_deck_compiles_as_draft(self):
        cards,manifest,_=merger.merge([(self.prior_manifest,self.prior_cards),
                                       (self.bixel_manifest,self.bixel_cards)],"mix-de","Mix")
        _,receipt=pack.validate(manifest,cards,draft=True)
        self.assertEqual(receipt["prompts"],5)

    def test_too_many_categories_is_an_error(self):
        wide=dict(self.bixel_manifest)
        wide["categories"]=[{"id":f"extra-{i}","name":f"E{i}","default_on":True,"adult":False} for i in range(6)]
        with self.assertRaises(merger.MergeError):
            merger.merge([(self.prior_manifest,[]),(wide,[])],"m","M")

if __name__=="__main__":
    unittest.main()
