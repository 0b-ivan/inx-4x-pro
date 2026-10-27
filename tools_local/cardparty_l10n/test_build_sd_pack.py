"""Offline safety regressions for immutable CAW SD pack compiler."""
import copy
import hashlib
import struct
import unittest
import zlib
import build_sd_pack as pack

def fixture():
    manifest={"deck_id":"original-qa","version":2,"source_locale":"de",
              "license":"CC0-1.0","rights_status":"verified",
              "editorial_status":"approved","rights_holder":"Fixture Author",
              "source_url":"https://example.org","license_url":"https://creativecommons.org/publicdomain/zero/1.0/"}
    cards=[{"id":"prompt-1","kind":"prompt","text":"Ohne Lücke?","pick":1,"draw":0,"reviewed":True},
           {"id":"prompt-2","kind":"prompt","text":"____ und ____","pick":2,"draw":0,"reviewed":True}]
    cards.extend({"id":f"answer-{i}","kind":"answer","text":f"Eine bürokratische Akte {i}","reviewed":True}
                 for i in range(100))
    return manifest,cards

class SdPackTests(unittest.TestCase):
    def test_valid_payload_crc_sha_and_slots(self):
        manifest,cards=fixture()
        data,receipt=pack.validate(manifest,cards)
        magic,v,flags,prompts,answers,n,crc,deckver,reserved=pack.HEADER.unpack_from(data)
        self.assertEqual((magic,v,flags,prompts,answers,deckver,reserved),
                         (pack.MAGIC,1,1,2,100,2,0))
        self.assertEqual(n,len(data)-pack.HEADER.size)
        self.assertEqual(crc,zlib.crc32(data[pack.HEADER.size:]))
        self.assertEqual(receipt["sha256"],hashlib.sha256(data).hexdigest())
        self.assertIn("Ohne Lücke?".encode(),data)
        self.assertFalse(receipt["draft_only"])

    def test_rights_editorial_and_approval_are_required(self):
        manifest,cards=fixture()
        for change in ({"license":None},{"rights_status":"unverified"},
                       {"editorial_status":"unapproved"},{"rights_holder":None}):
            with self.subTest(change=change),self.assertRaises(pack.PackError):
                pack.validate({**manifest,**change},cards)
        cards[-1]["reviewed"]=False
        with self.assertRaises(pack.PackError):pack.validate(manifest,cards)
        data,receipt=pack.validate(manifest,cards,draft=True)
        self.assertEqual(pack.HEADER.unpack_from(data)[2],0)
        self.assertTrue(receipt["draft_only"])

    def test_question_without_slot_and_bad_pick_are_distinguished(self):
        manifest,cards=fixture()
        pack.validate(manifest,cards)
        cards[1]["text"]="____ ____ ____"
        with self.assertRaisesRegex(pack.PackError,"placeholder"):pack.validate(manifest,cards)
        cards[1]["text"]="____ und ____"
        cards[0]["pick"]=4
        with self.assertRaises(pack.PackError):pack.validate(manifest,cards)

    def test_tamper_and_size_controls(self):
        manifest,cards=fixture()
        for text in ("x"*385,"Bad\x00Text","bad\nline"):
            altered=copy.deepcopy(cards);altered[0]["text"]=text
            with self.assertRaises(pack.PackError):pack.validate(manifest,altered)
        altered=copy.deepcopy(cards);altered[-1]["text"]="ä"*100
        with self.assertRaises(pack.PackError):pack.validate(manifest,altered)
        altered=copy.deepcopy(cards);altered[-1]["id"]=altered[-2]["id"]
        with self.assertRaises(pack.PackError):pack.validate(manifest,altered)
        with self.assertRaises(pack.PackError):
            pack.validate({**manifest,"deck_id":"../secrets"},cards)
        altered=copy.deepcopy(cards);altered[0]["pick"]=True
        with self.assertRaises(pack.PackError):pack.validate(manifest,altered)

if __name__=="__main__":
    unittest.main()
