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
                self.assertRegex(source["commit"],r"^[0-9a-f]{40}$")
            self.assertTrue(pack.web_url(entry["license_url"]))

if __name__=="__main__":
    unittest.main()
