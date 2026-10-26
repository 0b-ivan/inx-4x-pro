#!/usr/bin/env python3
"""Conservative bilingual editorial *proposals* for the 500 Qwen pilot.

This script creates a parallel proposal file. Original drafts remain unchanged.
All 500 questions keep reviewed=False; no merge, publication or sign-off.
"""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path

from build_de_pack import source_hash,validate_record
from numeric_semantics import audit_record

FIELDS=("q","a","alt","w")
EDITS = {
  "054ed3eaa4413aac": {
    "q": "Sie spielte in \"Liebe hat zwei Gesichter\" die Hauptrolle, führte Regie und komponierte das Liebesthema. Wer?",
    "reason": "Offizieller deutscher Filmtitel; Komposition des Liebesthemas durch Streisand belegt.",
    "evidence": [
      "https://www.fernsehserien.de/filme/liebe-hat-zwei-gesichter",
      "https://www.barbrastreisand.com/music/the-mirror-has-two-faces-music-from-the-motion-picture/"
    ]
  },
  "64e965842eb8ac76": {
    "q": "Mit 3,85 Tassen Tee pro Person und Tag liegt dieses Land beim Teekonsum außerhalb des Orients auf Platz 1. Welches?",
    "reason": "Die Ordnungszahl #1 wird explizit als Platz 1 erhalten; historische Zahlenangabe nicht neu verifiziert."
  },
  "91129e73fb3434f0": {
    "q": "Der Informant weiß, wer die Claymore-Mine kaufte, und kann Jonas AK-47-Gewehre besorgen. In welchem Land wurden diese erstmals entwickelt?",
    "reason": "Ersetzt holprige Formulierung durch explizite Frage und erhält die Herkunft als Hinweis."
  },
  "da1fc2b6081eec3a": {
    "q": "In welchem Roman von Yann Martel ist Richard Parker ein etwa 204 Kilogramm schwerer Bengalischer Tiger?",
    "reason": "450 englische Pfund entsprechen rund 204,1 kg; kein Gleichsetzen mit deutschem Pfund."
  },
  "170f7318ff686ce1": {
    "q": "Die Flagge dieses Landes gehört zu den bekanntesten der Welt und zeigt ein einzelnes Ahornblatt. Welches Land ist gemeint?",
    "reason": "Grammatik und Satzbau geglättet, Ahornblatt-Hinweis erhalten."
  },
  "27746493300d8ab9": {
    "q": "Welcher Frucht wird im Englischen ein \"polisher\" zugeordnet, wenn man einen Schleimer meint?",
    "reason": "Der englische Ausdruck apple-polisher bleibt als Hinweis erkennbar statt der falschen Übersetzung 'Pfleger'."
  },
  "373fcc4d14b785f3": {
    "q": "Leka I., der im Exil lebende König welches Balkanlandes, war ein Cousin 9. Grades von Richard Nixon?",
    "reason": "Behält Exil und Verwandtschaftsgrad statt 'ehemaliger König' bei."
  },
  "59bb7548caff2f6d": {
    "q": "Von 1864 bis 1869 war er Oberbefehlshaber der US-Armee; 1867 bis 1868 amtierte er als kommissarischer Kriegsminister. Wer?",
    "reason": "General-in-Chief sachlich genauer als Oberbefehlshaber statt Generaloberst."
  },
  "5b0372b66038d212": {
    "q": "Welcher General verkündete am 6.6.44 per Rundfunk: \"Menschen Westeuropas, heute Morgen ist eine Landung erfolgt\"?",
    "reason": "Verständlicher historischer Rundfunkhinweis mit Datum."
  },
  "6600ca616a423088": {
    "q": "Welcher US-Präsident durchbrach den \"Tecumseh-Fluch\", dem zufolge die seit 1840 in Abständen von 20 Jahren Gewählten im Amt starben?",
    "reason": "Klarer Fragesatz statt holpriger Relativsatz; Jahreszahl und Zyklus erhalten."
  },
  "9db8272a3109ca29": {
    "q": "Wenn man Vivaldis \"Vier Jahreszeiten\" der Reihe nach spielt, welches Konzert erklingt als Erstes?",
    "reason": "Klarer Quiz-Hinweis statt unidiomatischer Ellipse."
  },
  "a273e2ba014e7a09": {
    "q": "Welches Hormon der Bauchspeicheldrüse wirkt entgegengesetzt zu Glucagon?",
    "reason": "Grammatik und Bezug korrekt; gesuchtes Insulin wird nicht vorweggenommen."
  },
  "c1b6bef14d7b4029": {
    "q": "Zu welcher Tiergruppe gehört der Quetzal auf dem Wappen Guatemalas?",
    "reason": "Präzisere, natürlich formulierte Tierfrage ohne Antwort vorwegzunehmen."
  },
  "ca010a2ce44420fa": {
    "q": "Das um die Jahrhundertwende erbaute Hotel Metropol am Teatralny-Prospekt gehört zu den bekanntesten Hotels welcher Stadt?",
    "reason": "Straßenname als Eigenname statt frei erfundenem 'Theaterweg'."
  },
  "eeb6dc78558be1cb": {
    "q": "Womit fuhr Charlton Heston als \"Ben Hur\" bei dem berühmten Rennen?",
    "a": "Streitwagen",
    "alt": [
      "Ein Streitwagen"
    ],
    "reason": "Ben-Hur-Wagenrennen idiomatisch formuliert; Antwort Streitwagen statt mehrdeutigem Wagen."
  },
  "2802747e79195896": {
    "q": "Welches zentralamerikanische Land ist für politische Stabilität bekannt und hat seit 1949 keine Armee mehr?",
    "reason": "Satz und Frageform idiomatisch und grammatikalisch vollständig."
  },
  "3fb216fca0e9f8f0": {
    "q": "Welcher Star aus \"Pretty Woman\" war mit Kiefer Sutherland verlobt?",
    "reason": "Zeitlosere, eindeutigere Formulierung zur früheren Verlobung."
  },
  "88d7340876f78345": {
    "q": "In welchem Gebirge liegt der Giovi-Pass zwischen Genua und Mailand?",
    "w": [
      "Himalaya",
      "Pyrenäen",
      "Kaskadenkette"
    ],
    "reason": "Eindeutige Geografiefrage; korrektes deutsches Äquivalent für Cascades."
  },
  "c7a58d162a0b6969": {
    "q": "Welcher PLO-Führer erhielt 1994 gemeinsam mit Schimon Peres und Jitzchak Rabin den Friedensnobelpreis?",
    "reason": "Frage vollständig und sachlich belegt.",
    "evidence": [
      "https://www.nobelprize.org/prizes/peace/1994/summary/"
    ]
  },
  "117c306ce903a148": {
    "q": "Ein französisch-kanadischer Holzfäller aus dem 19. Jahrhundert soll Legenden über welchen riesenhaften Holzfäller inspiriert haben?",
    "reason": "French-Canadian idiomatisch und präzise wiedergegeben."
  },
  "0228fa6144593af6": {
    "q": "In der Lobby des Waldorf Astoria steht eine 2,74 Meter hohe Uhr, die für die Weltausstellung 1893 in welcher Stadt im US-Mittelwesten geschaffen wurde?",
    "reason": "QA: 9 Fuß entsprechen 2,7432 Metern, nicht 2,70 Metern; auf zwei Nachkommastellen gerundet."
  },
  "b326216f49e428a8": {
    "q": "Wessen Reisen nach Osten im 13. Jahrhundert hielt Rustichello da Pisa schriftlich fest?",
    "reason": "Nutzerfeedback E8: 'nach Osten' statt 'nach dem Osten'; grammatisch und als Quizfrage formuliert."
  },
  "c6d50209cfa9459e": {
    "a": "Rocky Mountains",
    "alt": ["Rockies"],
    "reason": "Nutzerfeedback H15: vollständiger und korrekt geschriebener deutscher Gebirgsname 'Rocky Mountains'; 'Rockies' als Synonym bleibt gültig."
  },
  "79942ab2044834cc": {
    "q": "Bei allen Arten dieses Vogels wiegen ausgewachsene Tiere weniger als etwa 28,35 Gramm",
    "reason": "Nutzerfeedback E13: metrische statt angloamerikanische Masseeinheit. Eine gewöhnliche Unze (avoirdupois) sind exakt 28,349523125 Gramm.",
    "evidence": ["https://www.nist.gov/document/2026-hb-133-appendix-e"]
  },
  "e4ec556b1977a1eb": {
    "q": "Am 3. Dezember 1973 näherte sich Pioneer 10 den Wolkenobergrenzen dieses Planeten auf 81.000 Meilen. Welcher Planet war es?",
    "reason": "Deutliche Pronomenbezüge; 'mostly liquid' aus alter Quellenfrage separat zu prüfen.",
    "needs_fact_check": True
  }
}

def read_jsonl(path):
    return [json.loads(x) for x in Path(path).read_text(encoding="utf-8").splitlines() if x.strip()]

def make_proposals(source,drafts,prior):
    if len(drafts)!=500 or len(prior)!=500:
        raise ValueError("Expected exactly 500 source-aligned drafts and 500 prior review entries")
    if len({r["id"] for r in drafts})!=500 or len({r["id"] for r in prior})!=500:
        raise ValueError("Duplicate source identifiers")
    by_id={s["id"]:s for s in source}
    prior_index={row["id"]:row for row in prior}
    if set(prior_index)!={row["id"] for row in drafts} or not set(EDITS).issubset(by_id):
        raise ValueError("Audit/proposals/source IDs mismatch")
    if not set(EDITS).issubset({x["id"] for x in drafts}):
        raise ValueError("One or more proposed records are absent from 500-question checkpoint")
    output,audit=[],[]
    for row in drafts:
        src=by_id[row["id"]]
        if row.get("source_hash")!=source_hash(src) or row.get("reviewed") is not False:
            raise ValueError("Source changed or human-reviewed flag is set")
        validate_record(src,row,allow_draft=True)
        old=audit_record(src,row)
        change=EDITS.get(row["id"])
        proposed=dict(row)
        for field in FIELDS:
            if change and field in change:
                proposed[field]=change[field]
        proposed["reviewed"]=False
        validate_record(src,proposed,allow_draft=True)
        new=audit_record(src,proposed)
        prior_record=prior_index[row["id"]]
        previous_ai=prior_record.get("prior_ai_verdict")
        if change:
            outcome="rewrite_proposed_fact_check_pending" if change.get("needs_fact_check") else "rewrite_proposed_human_approval_pending"
        elif new["warnings"]:
            outcome="numeric_or_unit_fact_check_required"
        elif previous_ai=="reject":
            outcome="previous_ai_rejection_unresolved"
        elif "potential_wordplay" in prior_record.get("risk_flags",[]):
            outcome="wordplay_approval_pending"
        elif previous_ai=="rewrite":
            outcome="earlier_ai_rewrite_manual_comparison"
        else:
            outcome="regular_unreviewed"
        audit.append({
            "id":row["id"],
            "original_english":{k:src[k] for k in FIELDS},
            "existing_german":{k:row[k] for k in FIELDS},
            "suggested_german":{k:proposed[k] for k in FIELDS},
            "proposal_changed":bool(change),
            "proposal_reason":change.get("reason","") if change else "",
            "sources":change.get("evidence",[]) if change else [],
            "prior_ai_verdict":previous_ai,
            "prior_ai_reason":prior_record.get("prior_ai_reason",""),
            "numeric_warnings_before":old["warnings"],
            "numeric_warnings_after":new["warnings"],
            "classification":outcome,
            "needs_human_review":True,
            "human_approved":False,
            "firmware_eligible":False
        })
        output.append(proposed)
    return output,audit

def main():
    parser=argparse.ArgumentParser()
    for name in ("source","drafts","prior-review","proposals","audit","csv","summary"):
        parser.add_argument("--"+name,required=True)
    a=parser.parse_args()
    src=read_jsonl(a.source)
    proposed,audit=make_proposals(src,read_jsonl(a.drafts),read_jsonl(a.prior_review))
    for path,rows in ((a.proposals,proposed),(a.audit,audit)):
        with open(path,"w",encoding="utf-8") as f:
            for row in rows:
                f.write(json.dumps(row,ensure_ascii=False,separators=(",",":"))+"\n")
    with open(a.csv,"w",encoding="utf-8",newline="") as f:
        cols=["id","classification","english_question","existing_german","suggested_german",
              "english_answer","old_answer","suggested_answer","reason","numeric_warnings_after",
              "sources","human_approved"]
        w=csv.DictWriter(f,fieldnames=cols)
        w.writeheader()
        for r in audit:
            if r["proposal_changed"] or r["classification"]!="regular_unreviewed":
                w.writerow({
                    "id":r["id"],"classification":r["classification"],
                    "english_question":r["original_english"]["q"],
                    "existing_german":r["existing_german"]["q"],
                    "suggested_german":r["suggested_german"]["q"],
                    "english_answer":r["original_english"]["a"],
                    "old_answer":r["existing_german"]["a"],
                    "suggested_answer":r["suggested_german"]["a"],
                    "reason":r["proposal_reason"] or r.get("prior_ai_reason",""),
                    "numeric_warnings_after":json.dumps(r["numeric_warnings_after"],ensure_ascii=False),
                    "sources":"; ".join(r["sources"]),
                    "human_approved":False})
    stats=Counter(r["classification"] for r in audit)
    result={"total_questions":500,"proposed_rewrites":sum(x["proposal_changed"] for x in audit),
            "numeric_warnings_before":sum(bool(x["numeric_warnings_before"]) for x in audit),
            "numeric_warnings_after":sum(bool(x["numeric_warnings_after"]) for x in audit),
            "old_ai_rejections":sum(x["prior_ai_verdict"]=="reject" for x in audit),
            "still_unapproved":500,"new_api_calls":0,"new_api_cost_usd":0,
            "classes":dict(stats),
            "note":"Parallel proposals only; original 500 checkpoint preserved. No human sign-off."}
    Path(a.summary).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("TRIVIA_EDITORIAL_PROPOSALS "+json.dumps(result,ensure_ascii=False),flush=True)
    if __import__("os").environ.get("GITHUB_STEP_SUMMARY"):
        with open(__import__("os").environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as f:
            f.write("## German Trivia 500: human-facing editorial proposals\n\n")
            f.write("| Measure | Count |\n|---|---:|\n")
            for k in ("proposed_rewrites","numeric_warnings_before","numeric_warnings_after",
                      "old_ai_rejections","still_unapproved","new_api_calls"):
                f.write(f"| {k} | {result[k]} |\n")
            f.write("\nNo paid model calls. No approved or published question pack.\n")
if __name__=="__main__":
    main()
