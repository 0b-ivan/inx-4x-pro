"""Offline regressions for the public pack publisher. Fixture text is our own."""
from pathlib import Path
import copy
import hashlib
import tempfile
import unittest

import build_sd_pack as pack
import publish_packs as publisher
import test_community_import as community
import test_import_prior99 as prior_fixture

ENTRY={
    "file":"Mix.dat","deck_id":"mix","title":"Mix - alle Karten","version":1,
    "rights_holder":"Fixture authors","license":"CC-BY-NC-SA-2.0",
    "license_url":"https://creativecommons.org/licenses/by-nc-sa/2.0/",
    "decision":"Test decision.",
    "sources":[{"importer":"prior99","url":"https://example.org/p","commit":"p1"},
               {"importer":"bixelpitch","url":"https://example.org/b","commit":"b1"}],
    "sha256":None,
}

class PublishTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        root=Path(self.tmp.name)
        prior_fixture.write_source(root/"prior",{"basis":prior_fixture.BASIS})
        community.bixel_source(root/"bixel",community.BLACK,community.WHITE)
        self.sources={"prior99":str(root/"prior"),"bixelpitch":str(root/"bixel")}
        self.out=root/"out"

    def tearDown(self):
        self.tmp.cleanup()

    def publish(self,*entries):
        return publisher.publish({"packs":list(entries)},self.sources,self.out)

    def test_released_pack_catalog_and_readme(self):
        built=self.publish(ENTRY)
        receipt=built[0][1]
        self.assertFalse(receipt["draft_only"])
        self.assertEqual(receipt["rights_status"],"verified")
        data=(self.out/"Mix.dat").read_bytes()
        self.assertEqual(data[:8],b"XCAWPK2\0")
        self.assertEqual((self.out/"catalog.txt").read_text(),f"Mix.dat|Mix - alle Karten|{len(data)}\n")
        readme=(self.out/"README.txt").read_text()
        for needle in ("Fixture authors","CC-BY-NC-SA-2.0","https://example.org/p at p1","Test decision."):
            self.assertIn(needle,readme)

    def test_same_input_same_bytes_and_pinned_hash(self):
        self.publish(ENTRY)
        first=(self.out/"Mix.dat").read_bytes()
        pinned=dict(ENTRY,sha256=hashlib.sha256(first).hexdigest())
        self.publish(pinned)
        self.assertEqual((self.out/"Mix.dat").read_bytes(),first)
        with self.assertRaises(publisher.PublishError):
            self.publish(dict(ENTRY,sha256="0"*64))

    def test_rejects_what_the_x4_would_not_list(self):
        for change in ({"file":"../Mix.dat"},{"file":"mix.txt"},{"file":".Mix.dat"},{"file":"M ix.dat"},
                       {"title":"A|B"},{"title":"x"*48},{"title":""}):
            with self.subTest(change=change), self.assertRaises(publisher.PublishError):
                self.publish(dict(ENTRY,**change))
        with self.assertRaises(publisher.PublishError):
            self.publish(ENTRY,dict(ENTRY))  # duplicate file
        with self.assertRaises(publisher.PublishError):
            self.publish(*[dict(ENTRY,file=f"m{i}.dat") for i in range(9)])

    def test_missing_or_unknown_source(self):
        broken=copy.deepcopy(ENTRY)
        broken["sources"][1]["importer"]="nope"
        with self.assertRaises(publisher.PublishError):
            self.publish(broken)
        del self.sources["bixelpitch"]
        with self.assertRaises(publisher.PublishError):
            self.publish(ENTRY)

    def test_shipped_config_is_well_formed(self):
        import json
        config=json.loads((Path(publisher.__file__).parent/"public_packs.json").read_text(encoding="utf-8"))
        self.assertEqual(config["release_tag"],"cardparty-packs")
        for entry in config["packs"]:
            publisher.check_entry(entry)
            self.assertRegex(entry["sha256"],r"^[0-9a-f]{64}$")
            for source in entry["sources"]:
                self.assertIn(source["importer"],publisher.IMPORTERS)
                if source["importer"]=="local":
                    self.assertRegex(source["name"],r"^[a-z0-9-]+$")
                    self.assertTrue(source["commit"])
                else:
                    self.assertRegex(source["commit"],r"^[0-9a-f]{40}$")
            self.assertTrue(pack.web_url(entry["license_url"]))

def write_local(folder,cards,adult_fsk=True):
    """An own deck as packs.local/ holds it. Fixture text is our own."""
    import json
    folder.mkdir(parents=True,exist_ok=True)
    manifest={"deck_id":"own-deck","version":3,"title":"Own","source_url":"https://example.org/own",
              "rights_holder":"Fixture owner","license":"CC0-1.0",
              "categories":[{"id":"schwarz","name":"Schwarz"},
                            {"id":"fsk-18","name":"FSK 18","default_on":False,"adult":adult_fsk}]}
    (folder/"own.json").write_text(json.dumps(manifest),encoding="utf-8")
    (folder/"own.jsonl").write_text("".join(json.dumps(c,ensure_ascii=False)+"\n" for c in cards),encoding="utf-8")
    return str(folder/"own.json")

OWN_CARDS=[{"id":"own-p1","kind":"prompt","text":"Mein Testwort: ____.","pick":1,"categories":["fsk-18"],"reviewed":False},
           {"id":"own-p2","kind":"prompt","text":"Erst ____, dann ____.","pick":2,"categories":["schwarz"],"reviewed":False}]
OWN_CARDS+=[{"id":f"own-a{i}","kind":"answer","text":f"Eigene Antwort {i}",
             "categories":["fsk-18"] if i%2 else ["schwarz"],"reviewed":False} for i in range(40)]
LOCAL={"importer":"local","name":"own","url":"https://example.org/own","commit":"v3"}

class LocalSourceTests(PublishTests):
    def setUp(self):
        super().setUp()
        self.sources["own"]=write_local(Path(self.tmp.name)/"packs.local",OWN_CARDS)

    def test_own_deck_alone_and_merged_with_community(self):
        own=dict(ENTRY,file="Own.dat",deck_id="own",title="Own",license="CC0-1.0",
                 license_url="https://creativecommons.org/publicdomain/zero/1.0/",sources=[LOCAL])
        mix=dict(ENTRY,sources=ENTRY["sources"]+[LOCAL])
        built=self.publish(own,mix)
        own_receipt,mix_receipt=built[0][1],built[1][1]
        self.assertEqual((own_receipt["prompts"],own_receipt["answers"]),(2,40))
        self.assertFalse(own_receipt["draft_only"])
        ids=[c["id"] for c in mix_receipt["categories"]]
        # fsk-18 from Prior99 and the own deck is one adult category, off by default.
        self.assertEqual(ids.count("fsk-18"),1)
        self.assertIn("schwarz",ids)
        fsk=[c for c in mix_receipt["categories"] if c["id"]=="fsk-18"][0]
        self.assertTrue(fsk["adult"])
        self.assertFalse(fsk["default_on"])
        readme=(self.out/"README.txt").read_text()
        self.assertIn("own deck own, v3",readme)
        community=self.publish(ENTRY)[0][1]
        self.assertEqual(mix_receipt["answers"],community["answers"]+40)
        catalog=(self.out/"catalog.txt").read_text().splitlines()
        self.assertEqual(len(catalog),1)  # last publish wrote only ENTRY

    def test_reviewed_flag_does_not_change_the_pack(self):
        own=dict(ENTRY,file="Own.dat",deck_id="own",title="Own",sources=[LOCAL])
        self.publish(own)
        first=(self.out/"Own.dat").read_bytes()
        flipped=[dict(c,reviewed=True) for c in OWN_CARDS]
        self.sources["own"]=write_local(Path(self.tmp.name)/"flipped",flipped)
        self.publish(own)
        self.assertEqual((self.out/"Own.dat").read_bytes(),first)

    def test_bad_own_cards_are_refused(self):
        own=dict(ENTRY,file="Own.dat",deck_id="own",title="Own",sources=[LOCAL])
        for bad in ({"id":"x1","kind":"prompt","text":"Zwei ____ und ____.","pick":1,"categories":[]},
                    {"id":"x2","kind":"answer","text":"Mit ____","categories":[]},
                    {"id":"x3","kind":"answer","text":"Unbekannt","categories":["nope"]},
                    {"id":"x4","kind":"answer","text":"zu  viele Leerzeichen","categories":[]}):
            with self.subTest(bad=bad):
                self.sources["own"]=write_local(Path(self.tmp.name)/bad["id"],OWN_CARDS+[bad])
                with self.assertRaises(publisher.PublishError):
                    self.publish(own)
        del self.sources["own"]
        with self.assertRaises(publisher.PublishError):
            self.publish(own)

if __name__=="__main__":
    unittest.main()
