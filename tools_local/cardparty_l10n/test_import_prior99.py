"""Offline regressions for the Prior99 LaTeX importer. Fixture text is our own."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

import build_sd_pack as pack
import import_prior99 as prior

def write_source(root,files):
    (root/"categories").mkdir(parents=True)
    for stem,_,_,_ in prior.CATEGORIES:
        (root/"categories"/f"{stem}.tex").write_text(files.get(stem,"% empty\n"),encoding="utf-8")

BASIS=r"""%
% comment lines and title cards are skipped
\titlecard{Basis}%
\whitecard{Basis}{Ein Sonnen- untergang am Bahnhof.}%
% engl. "a note"
\whitecard{Basis}{Eine Volleyball- Montage.}%
\whitecard{Basis}{``Hallo Welt'' sagen.}%
\whitecard{Basis}{Ein Super Soaker\SymbReg.}%
\whitecard{Basis}{110\% geben}%
\blackcard{1}{Basis}{Ich trinke, um \underl \newline zu vergessen.}%
\blackcard{2}{Basis}{Erst \underl, dann \underl.}%
\blackcard{3}{Basis}{\underl plus \underl gibt \underl.}%
\blackcard{1}{Basis}{Warum bin ich hier?}%
\blackcard{1}{Basis}{\underl? Gibt's ne App für.}%.
"""

class Prior99ImportTests(unittest.TestCase):
    def convert(self,files):
        with tempfile.TemporaryDirectory() as tmp:
            write_source(Path(tmp),files)
            return prior.convert(tmp)

    def test_latex_is_cleaned(self):
        cards,issues,dups=self.convert({"basis":BASIS})
        self.assertEqual(issues,[]);self.assertEqual(dups,[])
        texts=[c["text"] for c in cards]
        for expected in ("Ein Sonnenuntergang am Bahnhof.","Eine Volleyball-Montage.",
                         "„Hallo Welt“ sagen.","Ein Super Soaker®.","110% geben",
                         "Ich trinke, um ____ zu vergessen.","Erst ____, dann ____.",
                         "Warum bin ich hier?","____? Gibt's ne App für."):
            self.assertIn(expected,texts)
        for card in cards:
            self.assertNotIn("\\",card["text"]);self.assertNotIn("  ",card["text"])
            self.assertFalse(card["reviewed"])
            self.assertEqual(card["categories"],["basis"])

    def test_pick_draw_and_blanks(self):
        cards,_,_=self.convert({"basis":BASIS})
        prompts={c["text"]:(c["pick"],c["draw"]) for c in cards if c["kind"]=="prompt"}
        self.assertEqual(prompts["Erst ____, dann ____."],(2,0))
        self.assertEqual(prompts["____ plus ____ gibt ____."],(3,2))
        self.assertEqual(prompts["Warum bin ich hier?"],(1,0))

    def test_categories_dedup_and_stable_ids(self):
        files={"basis":BASIS,"fsk18":"\\whitecard{FSK 18}{Eine Volleyball- Montage.}%\n"
                                      "\\whitecard{FSK 18}{Nur hier.}%\n"}
        cards,_,dups=self.convert(files)
        merged=[c for c in cards if c["text"]=="Eine Volleyball-Montage."]
        self.assertEqual(len(merged),1)
        self.assertEqual(merged[0]["categories"],["basis","fsk-18"])
        self.assertEqual(len(dups),1)
        again,_,_=self.convert(files)
        self.assertEqual([c["id"] for c in cards],[c["id"] for c in again])
        self.assertEqual(len({c["id"] for c in cards}),len(cards))

    def test_broken_cards_are_reported_not_guessed(self):
        bad="\\blackcard{2}{Basis}{Nur \\underl eine Lücke.}%\n\\whitecard{Basis}{\\textbf{fett}}%\n"
        cards,issues,_=self.convert({"basis":bad})
        self.assertEqual(cards,[])
        self.assertEqual(len(issues),2)

    def test_manifest_is_unreleasable_until_a_human_decides(self):
        manifest=prior.manifest("abc")
        self.assertEqual(manifest["rights_status"],"unverified")
        adult=[c for c in manifest["categories"] if c["adult"]]
        self.assertEqual([c["id"] for c in adult],["fsk-18"])
        self.assertFalse(adult[0]["default_on"])
        cards,_,_=self.convert({"basis":BASIS+"".join(
            f"\\whitecard{{Basis}}{{Antwort {i}}}%\n" for i in range(40))})
        with self.assertRaises(pack.PackError):
            pack.validate(manifest,cards)
        data,receipt=pack.validate(manifest,cards,draft=True)
        self.assertTrue(receipt["draft_only"])
        self.assertEqual(len(receipt["categories"]),11)

    def test_cli_writes_cards_manifest_and_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            write_source(root/"src",{"basis":BASIS})
            run=subprocess.run([sys.executable,prior.__file__,"--source",str(root/"src"),
                                "--cards",str(root/"c.jsonl"),"--manifest",str(root/"m.json"),
                                "--report",str(root/"r.json")],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            report=json.loads((root/"r.json").read_text(encoding="utf-8"))
            self.assertEqual((report["prompts"],report["answers"]),(5,5))
            self.assertEqual(report["without_blank"],1)

if __name__=="__main__":
    unittest.main()
