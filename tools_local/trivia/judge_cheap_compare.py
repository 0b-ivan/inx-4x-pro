#!/usr/bin/env python3
"""Blindly judge existing translations, without paying to retranslate."""
import json, os, random, urllib.error, urllib.request
from pathlib import Path
root=Path(os.environ["TRIVIA_JUDGE_DIR"])
source=root/"comparison-per-question.jsonl"
rows=[json.loads(l) for l in source.read_text(encoding="utf-8").splitlines() if l.strip()]
if len(rows)!=20 or len({r["id"] for r in rows})!=20:
    raise SystemExit("Expected 20 distinct existing comparisons")
keys=("q","a","alt","w")
if any(set(r["translations"])!={"qwen","mini"} or
       not all(r["translations"][m] is not None for m in ("qwen","mini")) for r in rows):
    raise SystemExit("Missing translation, refuse incomplete comparison")
schema={"type":"json_schema","json_schema":{"name":"blind_german_quiz_review",
    "strict":True,"schema":{"type":"object","properties":{
    "A":{"type":"integer"},"B":{"type":"integer"},"C":{"type":"integer"},
    "best":{"type":"string","enum":["A","B","C","tie"]},
    "reason":{"type":"string"}},
    "required":["A","B","C","best","reason"],"additionalProperties":False}}}
model="openai/gpt-4.1-mini"
base_prompt=("You are an independent bilingual editor of quiz questions. "
             "Compare anonymous German versions A,B,C to English original. "
             "Give integer scores 0..5; prioritize factual fidelity, preserving "
             "the correct answer, all distractors, idiomatic German, numeric facts, "
             "wordplay preservation and avoiding revealing the answer. "
             "Quote a specific issue when possible. Zero means misleading or "
             "unplayable. Return strict JSON with scores, best and a short reason.")
spent=0.0
usage={"calls":0,"input_tokens":0,"output_tokens":0,"usd":0.0}
out=root/"independent-scores.jsonl"
with out.open("w",encoding="utf-8") as f:
    for i,row in enumerate(rows):
        if spent>=0.80:
            raise SystemExit("Budget guard exceeded; review remains incomplete")
        order=["qwen","mini","baseline"]
        random.Random(20261026+i).shuffle(order)
        versions={}
        for letter,name in zip("ABC",order):
            variant=row["baseline"] if name=="baseline" else row["translations"][name]
            versions[letter]={k:variant[k] for k in keys}
        prompt={"english":row["english"],"versions":versions}
        payload={"model":model,"temperature":0,"max_tokens":620,
                 "response_format":schema,"provider":{"require_parameters":True},
                 "usage":{"include":True},
                 "messages":[{"role":"system","content":base_prompt},
                             {"role":"user","content":json.dumps(prompt,ensure_ascii=False)}]}
        req=urllib.request.Request("https://openrouter.ai/api/v1/chat/completions",
            data=json.dumps(payload,ensure_ascii=False).encode("utf-8"),method="POST",
            headers={"Authorization":"Bearer "+os.environ["OPENROUTER_API_KEY"],
                     "Content-Type":"application/json",
                     "HTTP-Referer":"https://github.com/0b-ivan/inx-4x-pro"})
        try:
            with urllib.request.urlopen(req,timeout=75) as resp:
                response=json.load(resp)
        except urllib.error.HTTPError as e:
            detail=e.read(500).decode("utf-8","replace")
            raise SystemExit("Judge request failed; no score invented. HTTP "+
                             str(e.code)+" "+detail[:400])
        tokens=response.get("usage") or {}
        inp=int(tokens.get("prompt_tokens") or 0)
        output_tokens=int(tokens.get("completion_tokens") or 0)
        # Conservative fallback; cost uses provider-reported billing if supplied.
        cost=float(tokens["cost"]) if tokens.get("cost") is not None else max(
            0.0002,(inp*0.80+output_tokens*3.20)/1e6)
        spent+=cost
        usage["calls"]+=1
        usage["input_tokens"]+=inp
        usage["output_tokens"]+=output_tokens
        usage["usd"]+=cost
        raw=response["choices"][0]["message"]["content"]
        if isinstance(raw,list):
            raw="".join(x.get("text","") for x in raw if isinstance(x,dict))
        decision=json.loads(raw)
        if any(not isinstance(decision.get(k),int) or not 0<=decision[k]<=5 for k in "ABC"):
            raise SystemExit("Invalid judge score schema; no invented scores")
        if decision.get("best") not in ("A","B","C","tie"):
            raise SystemExit("Invalid judge winner")
        scores={name:decision[letter] for letter,name in zip("ABC",order)}
        winner=order["ABC".index(decision["best"])] if decision["best"]!="tie" else "tie"
        f.write(json.dumps({"id":row["id"],"scores":scores,"winner":winner,
                            "reason":decision["reason"],"numeric_flags":row["issues"]},
                           ensure_ascii=False)+"\n")
        f.flush()
        print("JUDGED {}/20 winner={} usd={:.5f}".format(i+1,winner,spent),flush=True)
judgments=[json.loads(l) for l in out.read_text(encoding="utf-8").splitlines()]
stat={}
for key in ("qwen","mini","baseline"):
    grades=[j["scores"][key] for j in judgments]
    stat[key]={"average_score":round(sum(grades)/len(grades),2),
               "wins":sum(j["winner"]==key for j in judgments),
               "scores":grades}
summary={"judged":len(judgments),"judge_model":model,
         "judge_spend_usd":round(spent,6),"token_usage":usage,
         "scores":stat,"ties":sum(j["winner"]=="tie" for j in judgments),
         "method":"20-source matched, three anonymous versions, one independent model judge; machine ratings only"}
(root/"judge-summary.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
print("JUDGE_SUMMARY "+json.dumps(summary,ensure_ascii=False,separators=(",",":")),flush=True)
with open(os.environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as md:
    md.write("## Blind quality comparison (20 matched quiz questions)\n\n")
    md.write("Judge: "+model+". Machine evaluation, not human approval.\n\n")
    md.write("| Model | Mean score / 5 | Wins |\n|---|---:|---:|\n")
    for k in ("qwen","mini","baseline"):
        md.write("| {} | {} | {} |\n".format(k,stat[k]["average_score"],stat[k]["wins"]))
    md.write("\nTies: {}, judge API cost: \${:.5f}.\n".format(summary["ties"],spent))
