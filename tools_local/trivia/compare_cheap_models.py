#!/usr/bin/env python3
"""Twenty matched German quiz questions: Qwen vs 4o-mini vs 100-batch baseline."""
import json, os, random, urllib.request
from pathlib import Path
from build_de_pack import source_hash, validate_record
from review_de_pack import numeric_facts
from translate_de_pack import SYSTEM, TRANSLATION_RESPONSE_FORMAT, normalize_display_punctuation

root=Path(os.environ["TRIVIA_COMPARE_DIR"])
models={"qwen":"qwen/qwen3-235b-a22b-2507","mini":"openai/gpt-4o-mini"}
fields=("q","a","alt","w")
spend=0.0
usage={x:{"calls":0,"input_tokens":0,"output_tokens":0,"usd":0.} for x in ("qwen","mini","judge")}
def load(path):
    return [json.loads(line) for line in (root/path).read_text(encoding="utf-8").splitlines() if line.strip()]
sources={r["id"]:r for r in load("source-en.jsonl")}
baseline=load("questions-de-editorial-100.jsonl")
if len(baseline)!=100 or len({r["id"] for r in baseline})!=100:
    raise SystemExit("Need exactly 100 baseline drafts")
sample=[baseline[5*i] for i in range(20)]
for row in sample:
    original=sources[row["id"]]
    if row["source_hash"]!=source_hash(original) or row.get("reviewed") is not False:
        raise SystemExit("Stale source or unexpected approval flag")
    validate_record(original,row,allow_draft=True)

judge_schema={"type":"json_schema","json_schema":{"name":"german_quiz_blind_test","strict":True,
    "schema":{"type":"object","properties":{
    "A":{"type":"integer"},"B":{"type":"integer"},"C":{"type":"integer"},
    "best":{"type":"string","enum":["A","B","C","tie"]},
    "reason":{"type":"string"}},
    "required":["A","B","C","best","reason"],"additionalProperties":False}}}
judge_system=("Act as a meticulous independent bilingual German trivia editor. "
    "Blindly evaluate 3 anonymized German translations against the English original. "
    "Give A/B/C integer scores 0..5 based on factual faithfulness, answer and "
    "distractor integrity, German fluency, numbers and solution leakage. "
    "Missing/invalid candidates score 0. Choose best or tie. Respond strict JSON.")
def api(bucket,model,messages,schema,max_tokens):
    global spend
    if spend>=0.80:
        raise RuntimeError("Safety budget stop: consumed >= 0.80 USD")
    body={"model":model,"temperature":0,"max_tokens":max_tokens,
          "provider":{"require_parameters":True},"response_format":schema,
          "usage":{"include":True},"messages":messages}
    req=urllib.request.Request("https://openrouter.ai/api/v1/chat/completions",
        data=json.dumps(body,ensure_ascii=False).encode("utf-8"),method="POST",
        headers={"Authorization":"Bearer "+os.environ["OPENROUTER_API_KEY"],
                 "Content-Type":"application/json",
                 "HTTP-Referer":"https://github.com/0b-ivan/inx-4x-pro"})
    with urllib.request.urlopen(req,timeout=90) as resp:
        answer=json.load(resp)
    u=answer.get("usage") or {}
    pt,ct=int(u.get("prompt_tokens") or 0),int(u.get("completion_tokens") or 0)
    costs={"qwen":(.30,1.),"mini":(.20,.80),"judge":(3.50,20.)}
    cost=float(u["cost"]) if u.get("cost") is not None else max(.0002,(pt*costs[bucket][0]+ct*costs[bucket][1])/1e6)
    spend+=cost
    usage[bucket]["calls"]+=1
    usage[bucket]["input_tokens"]+=pt
    usage[bucket]["output_tokens"]+=ct
    usage[bucket]["usd"]+=cost
    raw=answer["choices"][0]["message"]["content"]
    if isinstance(raw,list):
        raw="".join(e.get("text","") for e in raw if isinstance(e,dict))
    return json.loads(raw)

with (root/"comparison-per-question.jsonl").open("w",encoding="utf-8") as out:
    for n,base in enumerate(sample,1):
        original=sources[base["id"]]
        candidates,issues={},{}
        for name,model in models.items():
            try:
                raw=api(name,model,[{"role":"system","content":SYSTEM},
                    {"role":"user","content":json.dumps({f:original[f] for f in fields},ensure_ascii=False)}],
                    TRANSLATION_RESPONSE_FORMAT,800)
                if not isinstance(raw,dict) or set(raw)!=set(fields):
                    raise ValueError("Wrong response schema")
                row={f:([normalize_display_punctuation(x) for x in v] if isinstance(v,list)
                         else normalize_display_punctuation(v)) for f,v in raw.items()}
                validate_record(original,{"id":original["id"],"source_hash":source_hash(original),
                                          "reviewed":False,**row},allow_draft=True)
                candidates[name]=row
                issues[name]=[f for f in fields if numeric_facts(original[f])!=numeric_facts(row[f])]
            except Exception as ex:
                candidates[name]=None
                issues[name]=[type(ex).__name__+": "+str(ex)[:160]]
        shuffled=["baseline","qwen","mini"]
        random.Random(20261026+n).shuffle(shuffled)
        blind={letter:({f:(base if name=="baseline" else candidates[name])[f] for f in fields}
                       if name=="baseline" or candidates[name] else None)
               for letter,name in zip("ABC",shuffled)}
        judge=None
        try:
            judge=api("judge","openai/gpt-5.4",
                [{"role":"system","content":judge_system},
                 {"role":"user","content":json.dumps({"english":{f:original[f] for f in fields},
                                                       "versions":blind},ensure_ascii=False)}],
                judge_schema,650)
            if any(not isinstance(judge.get(c),int) or not 0<=judge[c]<=5 for c in "ABC"):
                raise ValueError("Judge scores invalid")
        except Exception as ex:
            issues["judge"]=[type(ex).__name__+": "+str(ex)[:160]]
        scores={name:judge[letter] for letter,name in zip("ABC",shuffled)} if judge else {}
        winner=shuffled["ABC".index(judge["best"])] if judge and judge.get("best") in ("A","B","C") else "tie"
        result={"id":original["id"],"english":{f:original[f] for f in fields},
                "baseline":{f:base[f] for f in fields},"translations":candidates,
                "issues":issues,"judge_scores":scores,"judge_winner":winner,
                "judge_reason":judge.get("reason") if judge else None,"spend_usd":round(spend,6)}
        out.write(json.dumps(result,ensure_ascii=False)+"\n")
        out.flush()
        print("COMPARISON_PROGRESS",n,"/20","qwen_ok",candidates["qwen"] is not None,
              "mini_ok",candidates["mini"] is not None,"judge_ok",judge is not None,
              "usd",round(spend,5),flush=True)
results=load("comparison-per-question.jsonl")
summary={"questions":len(results),"total_cost_usd":round(spend,6),"usage":usage,"models":{}}
for m in ("qwen","mini","baseline"):
    valid=len(results) if m=="baseline" else sum(x["translations"][m] is not None for x in results)
    grades=[x["judge_scores"][m] for x in results if m in x["judge_scores"]]
    summary["models"][m]={"valid":valid,"numeric_warnings":sum(
        bool(x["translations"].get(m)) and bool(x["issues"].get(m)) for x in results),
        "score_average":round(sum(grades)/len(grades),2) if grades else None,
        "judge_wins":sum(x["judge_winner"]==m for x in results)}
(root/"comparison-summary.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding="utf-8")
print("COMPARISON_SUMMARY",json.dumps(summary,separators=(",",":")),flush=True)
with open(os.environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as md:
    md.write("## Low-cost German trivia comparison\n\n"
             "20 identical English source questions; previous Claude/GPT editorial baseline; "
             "blind machine judgement, not human review.\n\n")
    md.write("| Model | Valid | Numeric warnings | Avg score /5 | Judge wins | USD API cost |\n"
             "|---|---:|---:|---:|---:|---:|\n")
    for m in ("qwen","mini","baseline"):
        stat=summary["models"][m]
        md.write("| {} | {}/20 | {} | {} | {} | {:.5f} |\n".format(
            m,stat["valid"],stat["numeric_warnings"],stat["score_average"],
            stat["judge_wins"],usage.get(m,{}).get("usd",0)))
    md.write("\nTotal reported API cost: {:.5f} USD. No release or approval.\n".format(spend))
