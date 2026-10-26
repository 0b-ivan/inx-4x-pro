#!/usr/bin/env python3
"""Conservative EN/DE numeric notation normalizer for quiz QA.

Standardize dates, year-range contractions, ordinals, common German
number-inflected nouns and explicit vulgar fractions. Preserve unknown
expressions for review; never auto-approve translated question records.
"""
import re
from collections import Counter

YEAR_RANGE = re.compile(r"(?<!\w)(\d{4})\s*[-–]\s*(\d{2})(?!\w)")
SLASH_DATE = re.compile(r"(?<!\w)(\d{1,2})[./](\d{1,2})[./](\d{2,4})(?!\w)")
EN_ORDINAL = re.compile(r"(?<!\w)(\d+)(?:st|nd|rd|th)\b", re.I)
DE_DECADE = re.compile(r"(?<!\w)(\d{4})er(?:n)?\b", re.I)
EN_DECADE = re.compile(r"(?<!\w)(\d{4})s\b", re.I)
EN_RANK = re.compile(r"\b(first|second|third|fourth|fifth|sixth|seventh|eighth|ninth|tenth)\b",re.I)
DE_RANK = re.compile(r"\b(erste|erster|erstes|ersten|erstem|zweite|zweiter|zweites|zweiten|zweitem|dritte|dritter|drittes|dritten|drittem|vierte|vierter|viertes|vierten|viertem|fünfte|fünfter|fünftes|fünften|fünftem)\b",re.I)
EN_RANKS = {x:str(i) for i,x in enumerate(
    "first second third fourth fifth sixth seventh eighth ninth tenth".split(),1)}
DE_RANKS = {x:str(i) for i,forms in enumerate([
    "erste erster erstes ersten erstem",
    "zweite zweiter zweites zweiten zweitem",
    "dritte dritter drittes dritten drittem",
    "vierte vierter viertes vierten viertem",
    "fünfte fünfter fünftes fünften fünftem"
],1) for x in forms.split()}
DE_COMPOUNDS = (
    (re.compile(r"(?<!\w)(?:zwei)(?:stellig(?:e|en|er|es)?)(?!\w)",re.I),"2"),
    (re.compile(r"(?<!\w)(?:drei)(?:bändig(?:e|en|er|es)?)(?!\w)",re.I),"3"),
    (re.compile(r"(?<!\w)(?:hundert)(?:jährig(?:e|en|er|es)?)(?!\w)",re.I),"100"),
    (re.compile(r"(?<!\w)(?:eine|einer|einen)\s+unze(?!\w)",re.I),"1 Unze"),
)
DE_MIXED = re.compile(r"(?<!\w)(ein|zwei|drei|vier|fünf|sechs|sieben|acht|neun)einhalb(?!\w)",re.I)
EN_MIXED = re.compile(r"(?<!\w)(\d+)\s+(\d+)\s*/\s*(\d+)(?!\w)")
FRACTION = re.compile(r"(?<!\w)(\d+)\s*/\s*(\d+)(?!\w)")
DE_FRAC = re.compile(r"(?<!\w)(ein|eine|einen|zwei|drei|vier|fünf|sechs|sieben|acht|neun)\s+(Drittel|Viertel|Fünftel|Halb)(?!\w)",re.I)
NUMWORDS = {"ein":1,"eine":1,"einen":1,"zwei":2,"drei":3,"vier":4,"fünf":5,"sechs":6,"sieben":7,"acht":8,"neun":9}
DENOMS = {"halb":2,"drittel":3,"viertel":4,"fünftel":5}

def pre_normalize(value, language):
    """Return (residual text, Counter of exact fractional values).

    Fraction tokens are kept separate so 2/3 isn't misread as two unrelated
    integers; English and German fractions share the same canonical form.
    """
    text = value.casefold()
    specials = Counter()
    def expand_year(m):
        start=int(m.group(1))
        finish=int(m.group(2))
        year=start//100*100 + finish
        if year < start:
            year += 100
        return f"{start} {year}"
    text=YEAR_RANGE.sub(expand_year,text)
    text=SLASH_DATE.sub(lambda m:" ".join(m.groups()),text)
    if language=="en":
        text=EN_DECADE.sub(lambda m:m.group(1),text)
        text=EN_ORDINAL.sub(lambda m:m.group(1),text)
        text=EN_RANK.sub(lambda m:EN_RANKS[m.group().casefold()],text)
        def em(m):
            # Exact half-integer, not an ambiguous pair of integers.
            if m.group(2)=="1" and m.group(3)=="2":
                return m.group(1)+".5"
            return m.group(0)
        text=EN_MIXED.sub(em,text)
    elif language=="de":
        text=DE_DECADE.sub(lambda m:m.group(1),text)
        text=DE_RANK.sub(lambda m:DE_RANKS[m.group().casefold()],text)
        for pattern,replace in DE_COMPOUNDS:
            text=pattern.sub(replace,text)
        text=DE_MIXED.sub(lambda m: str(NUMWORDS[m.group(1)])+",5",text)
        def german_fraction(m):
            n=NUMWORDS[m.group(1)]
            d=DENOMS[m.group(2)]
            specials[f"fraction:{n}/{d}"]+=1
            return " "
        text=DE_FRAC.sub(german_fraction,text)
    else:
        raise ValueError("Unsupported language code")
    def fraction(m):
        n,d=int(m.group(1)),int(m.group(2))
        if d == 0:
            return m.group(0)
        from math import gcd
        g=gcd(n,d)
        specials[f"fraction:{n//g}/{d//g}"]+=1
        return " "
    text=FRACTION.sub(fraction,text)
    return text,specials
