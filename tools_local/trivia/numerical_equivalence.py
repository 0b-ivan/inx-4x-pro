#!/usr/bin/env python3
"""Conservative bilingual numeric facts for English/German trivia QA.

Canonicalises common digits and spelled-out 0-99 numbers (including
'twenty-five' and 'fünfundzwanzig'). Does NOT interpret indefinite German
articles 'ein/eine' as numeral one. An unmatched form is not evidence of
semantic equivalence, so additional audits still matter.
"""
import re
from collections import Counter

EN_U=("zero one two three four five six seven eight nine ten eleven twelve "
      "thirteen fourteen fifteen sixteen seventeen eighteen nineteen").split()
EN_T={20:"twenty",30:"thirty",40:"forty",50:"fifty",60:"sixty",
      70:"seventy",80:"eighty",90:"ninety"}
DE_U=("null eins zwei drei vier fünf sechs sieben acht neun zehn elf zwölf "
      "dreizehn vierzehn fünfzehn sechzehn siebzehn achtzehn neunzehn").split()
DE_T={20:"zwanzig",30:"dreißig",40:"vierzig",50:"fünfzig",60:"sechzig",
      70:"siebzig",80:"achtzig",90:"neunzig"}
WORDS={}
for i in range(20):
    WORDS[EN_U[i]]=str(i)
    WORDS[DE_U[i]]=str(i)
for base,word in EN_T.items():
    WORDS[word]=str(base)
    for unit in range(1,10):
        WORDS[word+" "+EN_U[unit]]=str(base+unit)
for base,word in DE_T.items():
    WORDS[word]=str(base)
    for unit in range(1,10):
        first="ein" if unit==1 else DE_U[unit]
        WORDS[first+"und"+word]=str(base+unit)
WORDS.update({"hundred":"100","one hundred":"100","hundert":"100",
              "einhundert":"100","thousand":"1000","one thousand":"1000",
              "tausend":"1000","eintausend":"1000"})
# Treat 'ein' (German indefinite article) as an unknown, not an automatic 1.
NUMBER_WORDS=re.compile(r"(?<!\w)(?:"+ "|".join(
    re.escape(w) for w in sorted(WORDS,key=len,reverse=True))+r")(?!\w)",re.I)
ARABIC=re.compile(r"(?<![\w])\d+(?:[.,]\d+)?(?!\w)")

def numeric_facts(value):
    if isinstance(value,list):
        result=Counter()
        for part in value:
            result.update(numeric_facts(part))
        return result
    if not isinstance(value,str):
        return Counter()
    normalized=value.lower().replace("-"," ")
    # Work on a residual string so neither 'twenty one' nor a digit is
    # double-counted as several numerals.
    facts=Counter()
    for match in ARABIC.finditer(normalized):
        facts[match.group()]+=1
    normalized=ARABIC.sub(" ",normalized)
    for match in NUMBER_WORDS.finditer(normalized):
        facts[WORDS[match.group()]]+=1
    return facts

def numeric_warnings(original,translated,fields=("q","a","alt","w")):
    errors=[]
    for field in fields:
        expected=numeric_facts(original.get(field,[] if field in ("alt","w") else ""))
        actual=numeric_facts(translated.get(field,[] if field in ("alt","w") else ""))
        if expected!=actual:
            errors.append({"field":field,"english_numbers":dict(expected),
                           "german_numbers":dict(actual)})
    return errors
