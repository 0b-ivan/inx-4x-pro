#!/usr/bin/env python3
"""Run the real SDL Activity against isolated, synthetic SD cards."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools_local/cardparty_l10n"))
from build_sd_pack import MAX_BYTES, validate
from test_build_sd_pack import fixture

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--all-states",action="store_true")
    args=parser.parse_args()
    binary=args.binary.resolve();out=args.out.resolve()
    out.mkdir(parents=True,exist_ok=True)
    manifest,cards=fixture()
    cards=cards[:1]+cards[2:]
    cards[0]["text"]="SD Prüfung: Grüße aus Köln, Öl und süße Bürokratie?"
    released,_=validate(manifest,cards)
    cards[0]["text"]="ä"*189+" ENDE"
    long_word,_=validate(manifest,cards)
    draft,_=validate(manifest,cards,draft=True)
    # Category screen: some answers Böse, some FSK 18 (off until switched on).
    tagged_cards=[dict(card) for card in cards]
    for i,card in enumerate(c for c in tagged_cards if c["kind"]=="answer"):
        card["categories"]=["boese"] if i%3==1 else ["fsk-18"] if i%3==2 else []
    tagged_cards[0]["text"]="SD Prüfung: Grüße aus Köln, Öl und süße Bürokratie?"
    tagged,_=validate(manifest,tagged_cards)
    corrupt=bytearray(released);corrupt[-2]^=1
    cases={"missing":None,"released":released,"released-long-word":long_word,"draft":draft,
           "crc":bytes(corrupt),"truncated":released[:-1],"oversize":b"x"*(MAX_BYTES+1),
           # Same pack from the packs folder, no active.dat: picked up on its own.
           "released-packdir":released}
    if args.all_states:
        cases.update({"released-"+phase:released for phase in ("setup","lobby","judge","reveal","finished")})
        cases["released-categories"]=tagged
        cases["released-decks"]=released
        cases["released-remote"]=released
    hashes={}
    for case,data in cases.items():
        scenario=case.removeprefix("released-") if case in (
            "released-setup","released-lobby","released-judge","released-reveal","released-finished",
            "released-categories","released-decks","released-remote") else "collect"
        sd=out/(case+"-sd")
        target=sd/("games/cardparty/packs/qa.dat" if case in ("released-packdir","released-decks")
                   else "games/cardparty/active.dat")
        target.parent.mkdir(parents=True,exist_ok=True)
        if data is None:
            target.unlink(missing_ok=True)
        else:
            target.write_bytes(data)
        screenshot=out/(case+".bmp")
        screenshot.unlink(missing_ok=True)
        env={**os.environ,"CROSSPOINT_SIM_SD":str(sd),"CROSSPLAY_AUTOSTART":"CARDS WHATEVER",
             "SDL_VIDEO_HIGHDPI_DISABLED":"1",
             "CROSSPLAY_SEED":"11","CROSSPLAY_SIM_CARDPARTY_STATE":scenario,
             "CROSSPLAY_SIM_EPOCH":"1704067200","CROSSPOINT_SIM_INPUT_SCRIPT":"7200:QUIT",
             "CROSSPOINT_SIM_SCREENSHOTS":"5700:"+str(screenshot)}
        with (out/(case+".log")).open("w") as log:
            result=subprocess.run([str(binary)],env=env,cwd=ROOT,stdout=log,
                                  stderr=subprocess.STDOUT,timeout=25)
        text=(out/(case+".log")).read_text(errors="replace")
        assert result.returncode==0,(case,result.returncode,text[-3000:])
        assert "Autostart into CARDS WHATEVER" in text,(case,text[-3000:])
        if scenario not in ("setup","categories","decks","remote"):
            assert "CARDPARTY_SIM_STATE="+scenario+" " in text,(case,text[-3000:])
        assert "No glyph for codepoint" not in text,(case,"unsupported glyph")
        if case.startswith("released"):
            assert "Loaded released SD deck v2 (1 prompts, 100 answers, 3 categories)" in text,text[-3000:]
        else:
            assert "Loaded released SD deck" not in text,case
            if case!="missing":
                assert "using original demo" in text,case
        data=screenshot.read_bytes()
        assert data[:2]==b"BM",case
        assert tuple(map(abs,struct.unpack_from("<ii",data,18)))==(480,800),case
        pixels=data[struct.unpack_from("<I",data,10)[0]:]
        assert len(set(pixels))>1,(case,"blank bitmap")
        hashes[case]=hashlib.sha256(pixels).hexdigest()
        print("PASS SD Activity",case,hashes[case][:14],flush=True)
    assert hashes["released"]!=hashes["missing"],"SD and demo render identically"
    assert hashes["released-packdir"]==hashes["released"],"packs folder plays differently from active.dat"
    assert all(hashes[key]==hashes["missing"] for key in cases if not key.startswith("released")),"fallback differs from demo"
    assert len({hashes[key] for key in ("missing","released","released-long-word")})==3
    if args.all_states:
        assert len({hashes[key] for key in ("released","released-setup","released-lobby",
                                          "released-judge","released-reveal","released-finished")})==6
    print("PASS released SD selection, long-word UTF-8 rendering and five identical demo fallbacks")

if __name__=="__main__":
    main()
