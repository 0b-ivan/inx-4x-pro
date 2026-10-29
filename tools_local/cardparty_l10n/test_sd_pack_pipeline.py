"""Independent JSON assertions on the real server using CLI-built SD packages."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import build_sd_pack as pack
from test_build_sd_pack import fixture

ROOT=Path(__file__).resolve().parents[2]

def boundary_text(prefix,size):
    remaining=size-len(prefix.encode("utf-8"))
    return prefix+'\\"'*(remaining//2)+'x'*(remaining%2)

class SdPackPipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root=Path(cls.temp.name)
        (cls.root/"CardPartyPageHtml.generated.h").write_text(
            '#pragma once\nconstexpr char CardPartyPageHtml[]="";\n'
            'constexpr size_t CardPartyPageHtmlCompressedSize=0;\n',encoding="utf-8")
        cls.binary=cls.root/"pipeline"
        result=subprocess.run(["c++","-std=c++17","-funsigned-char","-Wall","-Wextra","-Werror","-pedantic","-O2",
                        "-I"+str(ROOT/"host-tests/battleship/stubs"),"-I"+str(cls.root),
                        str(ROOT/"src/apps_local/cardparty/web/CardPartyBrowserServer.cpp"),
                        str(ROOT/"host-tests/cardparty/test_sd_pack_pipeline.cpp"),
                        "-o",str(cls.binary)],capture_output=True,text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def test_cli_pack_reaches_all_eight_players_without_text_loss(self):
        for pick,draw,slots in ((1,0,"ohne Lücke?"),(2,1,"____ und ____"),
                                (3,2,"____ / ____ / ____")):
            with self.subTest(pick=pick,draw=draw):
                manifest,cards=fixture()
                cards=cards[:1]+cards[2:]
                cards[0].update(text=boundary_text("SD Prüfung: "+slots+" ",pack.MAX_PROMPT_BYTES),
                                pick=pick,draw=draw)
                for i,card in enumerate(cards[1:]):
                    card["text"]=boundary_text(f"SD Antwort äöüß {i}: ",pack.MAX_ANSWER_BYTES)
                mp=self.root/"deck.json";cp=self.root/"cards.jsonl";out=self.root/"active.dat"
                mp.write_text(json.dumps(manifest),encoding="utf-8")
                cp.write_text("\n".join(map(json.dumps,cards)),encoding="utf-8")
                subprocess.run([sys.executable,pack.__file__,"--manifest",str(mp),"--cards",str(cp),
                                "--out",str(out)],check=True,capture_output=True,text=True)
                result=subprocess.run([str(self.binary),str(out)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                snapshots=[json.loads(line.removeprefix("SNAPSHOT "))
                           for line in result.stdout.splitlines() if line.startswith("SNAPSHOT ")]
                self.assertEqual(len(snapshots),24)
                expected_answers={card["text"] for card in cards[1:]}
                for phase,group in zip(("Playing","Judging","Reveal"),
                                       (snapshots[:8],snapshots[8:16],snapshots[16:])):
                    for state in group:
                        game=state["game"]
                        self.assertEqual(game["phase"],phase)
                        self.assertEqual(game["promptText"],cards[0]["text"])
                        self.assertEqual((game["pick"],game["draw"]),(pick,draw))
                        for card in state["private"]["hand"]:
                            self.assertIn(card["text"],expected_answers)
                        if phase!="Playing":
                            self.assertEqual(len(game["submissions"]),8)
                            for submission in game["submissions"]:
                                self.assertEqual(len(submission["cards"]),pick)
                                self.assertTrue(set(submission["cards"])<=expected_answers)
                        if phase=="Reveal":
                            self.assertEqual(game["winningText"]," / ".join(game["submissions"][0]["cards"]))

if __name__=="__main__":
    unittest.main()
