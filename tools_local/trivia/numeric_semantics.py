#!/usr/bin/env python3
"""Bilingual numerical equivalence audit, including locale and unit conversions.

This *triages* translated trivia drafts; it does not assert that a complete
translation is correct or grant publication approval. Unsupported/ambiguous
numeric changes remain warnings for human review.
"""
import re
from collections import Counter
from decimal import Decimal, InvalidOperation

from numerical_equivalence import WORDS
from numeric_notation import pre_normalize

# Exclude identifiers (AK-47, X-15) from quantity comparisons; these are not
# measurements. The separate semantic editorial review still sees the text.
CODES = re.compile(r"(?<!\w)(?:ak|x|f|sr|mig)-\d+(?!\w)", re.I)
DIGITS = re.compile(r"(?<!\w)\d+(?:[.,]\d+)*(?!\w)")
NUM_WORDS = re.compile(r"(?<!\w)(?:" + "|".join(
    re.escape(s) for s in sorted(WORDS, key=len, reverse=True)
) + r")(?!\w)", re.I)
ARTICLE = re.compile(r"\b(?:ein|eine|einen|einem|eines|einer)\b", re.I)

# Canonical length, mass and temperature quantities.
UNIT_RX = {
    "en": re.compile(
        r"(?<!\w)(?P<num>\d+(?:[.,]\d+)*)\s*(?:-| )?\s*"
        r"(?P<unit>feet|foot|ft|inches|inch|miles|mile|mi|kilometers|kilometres|"
        r"km|centimeters|centimetres|centimeter|centimetre|cm|meters|metres|meter|metre|m|pounds|pound|lbs|lb|kg|kilograms|"
        r"kilogram|°f|°c|fahrenheit|celsius)\b", re.I),
    "de": re.compile(
        r"(?<!\w)(?P<num>\d+(?:[.,]\d+)*)\s*(?:-| )?\s*"
        r"(?P<unit>fuß|fuss|feet|ft|zoll|meilen|meile|kilometer|kilometern|"
        r"km|zentimeter|zentimetern|cm|meter|metern|m|pfund|kilogramm|kilogrammen|kg|"
        r"°f|°c|fahrenheit|celsius)\b", re.I)
}
UNITS = {
    "ft": ("length", Decimal("0.3048")),
    "foot": ("length", Decimal("0.3048")),
    "feet": ("length", Decimal("0.3048")),
    "fuß": ("length", Decimal("0.3048")),
    "fuss": ("length", Decimal("0.3048")),
    "m": ("length", Decimal("1")),
    "meter": ("length", Decimal("1")),
    "meters": ("length", Decimal("1")),
    "metre": ("length", Decimal("1")),
    "metres": ("length", Decimal("1")),
    "metern": ("length", Decimal("1")),
    "mi": ("length", Decimal("1609.344")),
    "mile": ("length", Decimal("1609.344")),
    "miles": ("length", Decimal("1609.344")),
    "meile": ("length", Decimal("1609.344")),
    "meilen": ("length", Decimal("1609.344")),
    "km": ("length", Decimal("1000")),
    "kilometer": ("length", Decimal("1000")),
    "kilometern": ("length", Decimal("1000")),
    "kilometers": ("length", Decimal("1000")),
    "kilometres": ("length", Decimal("1000")),
    "cm": ("length", Decimal("0.01")),
    "centimeter": ("length", Decimal("0.01")),
    "centimeters": ("length", Decimal("0.01")),
    "centimetre": ("length", Decimal("0.01")),
    "centimetres": ("length", Decimal("0.01")),
    "zentimeter": ("length", Decimal("0.01")),
    "zentimetern": ("length", Decimal("0.01")),
    "inch": ("length", Decimal("0.0254")),
    "inches": ("length", Decimal("0.0254")),
    "zoll": ("length", Decimal("0.0254")),
    "pound": ("mass", Decimal("0.45359237")),
    "pounds": ("mass", Decimal("0.45359237")),
    "lb": ("mass", Decimal("0.45359237")),
    "lbs": ("mass", Decimal("0.45359237")),
    "pfund": ("mass", Decimal("0.5")), # German Pfund is 500 g; context ambiguous!
    "kg": ("mass", Decimal("1")),
    "kilogram": ("mass", Decimal("1")),
    "kilograms": ("mass", Decimal("1")),
    "kilogramm": ("mass", Decimal("1")),
    "kilogrammen": ("mass", Decimal("1")),
    "°c": ("temperature_c", Decimal("1")),
    "celsius": ("temperature_c", Decimal("1")),
    "°f": ("temperature_f", Decimal("1")),
    "fahrenheit": ("temperature_f", Decimal("1"))
}
# "pound" -> "Pfund" is culturally ambiguous, so never automatically clear.
AMBIGUOUS_PAIRS = {("pound", "pfund"), ("pounds", "pfund"), ("lb", "pfund"), ("lbs", "pfund")}

def number_value(text, language):
    """Return (numeric value, rounding resolution) or None.

    Locale-aware separators: EN 14,433; DE 14.433; EN 0.01; DE 0,01.
    """
    token = text.strip()
    group = "," if language == "en" else "."
    decimal = "." if language == "en" else ","
    if token.count(group) and re.fullmatch(r"\d{1,3}(?:" + re.escape(group) + r"\d{3})+", token):
        val = token.replace(group, "")
        return Decimal(val), Decimal(1)
    if token.count(group) and token.count(decimal):
        token = token.replace(group, "")
    elif token.count(group):
        # Single unfamiliar separator: do not guess thousands vs decimal.
        return None
    try:
        normalized = token.replace(decimal, ".")
        value = Decimal(normalized)
        decimals = len(normalized.split(".")[1]) if "." in normalized else 0
        return value, Decimal(1).scaleb(-decimals)
    except InvalidOperation:
        return None

def numeric_tokens(value, language):
    """Numerical multiset without article-only one or hyphenated model IDs."""
    result = Counter()
    if isinstance(value, list):
        for part in value:
            result.update(numeric_tokens(part, language))
        return result
    # Preserve and canonicalize ordinals, century dates, fractions, and common
    # EN/DE word-number morphology before comparing quantities.
    text, special = pre_normalize(value, language)
    # 'One' as a complete proper-noun answer/distractor is not quantity 1.
    if text.strip() == "one":
        return Counter()
    text = CODES.sub(" ", text)
    occupied = [False] * len(text)
    result.update(special)
    for match in DIGITS.finditer(text):
        pair = number_value(match.group(), language)
        if pair:
            result[format(pair[0].normalize(), "f")] += 1
        for offset in range(match.start(), match.end()):
            occupied[offset] = True
    for match in NUM_WORDS.finditer(text):
        if any(occupied[match.start():match.end()]):
            continue
        word = match.group()
        # English 'one' often functions as a pronoun; German 'ein' as an
        # indefinite article. Do not flag alone as changed quantity.
        if word == "one" and language == "en":
            continue
        result[WORDS[word]] += 1
    return result

def _units(value, language):
    if not isinstance(value, str):
        return []
    found = []
    for match in UNIT_RX[language].finditer(value.casefold()):
        num = number_value(match.group("num"), language)
        key = match.group("unit")
        if num and key in UNITS:
            found.append((num[0], num[1], key, UNITS[key][0], UNITS[key][1]))
    return found

def _conversion_notes(original, translated):
    """Verify explicit quantity/unit conversions, keeping all other digits."""
    src = numeric_tokens(original, "en")
    dst = numeric_tokens(translated, "de")
    converted = []
    used_de = set()
    for english in _units(original, "en"):
        a, aincrement, au, dim, afactor = english
        for index, german in enumerate(_units(translated, "de")):
            if index in used_de:
                continue
            b, bincrement, bu, bdim, bfactor = german
            if (au, bu) in AMBIGUOUS_PAIRS:
                continue
            if dim == bdim == "length" or dim == bdim == "mass":
                expected = a * afactor / bfactor
            elif dim == bdim:
                expected = a
            elif dim == "temperature_f" and bdim == "temperature_c":
                expected = (a - Decimal(32)) * Decimal(5) / Decimal(9)
            elif dim == "temperature_c" and bdim == "temperature_f":
                expected = a * Decimal(9) / Decimal(5) + Decimal(32)
            else:
                continue
            # Include rounding uncertainty in BOTH source and translated quantities.
            source_tolerance = (aincrement * afactor / bfactor / 2) if dim == bdim and dim not in ("temperature_c","temperature_f") else aincrement / 2
            tolerance = (bincrement / 2) + source_tolerance
            if abs(expected - b) <= tolerance:
                av, bv = str(a.normalize()), str(b.normalize())
                if src[av] and dst[bv] and (av != bv or au != bu):
                    src[av] -= 1
                    dst[bv] -= 1
                    converted.append({"english":f"{a} {au}","german":f"{b} {bu}",
                                      "expected_german":str(expected.normalize())})
                    used_de.add(index)
                    break
    return +src, +dst, converted

def audit_field(original, translation):
    """Separate numeric disagreements from proven safe locale/unit variations."""
    if isinstance(original, list) and isinstance(translation, list):
        if len(original) != len(translation):
            return {"warnings":[{"kind":"option_count_changed"}],"notes":[]}
        warnings,notes=[],[]
        for i,(en,de) in enumerate(zip(original, translation)):
            one=audit_field(en,de)
            for issue in one["warnings"]:
                warnings.append({"option":i,**issue})
            for n in one["notes"]:
                notes.append({"option":i,**n})
        return {"warnings":warnings,"notes":notes}
    before=numeric_tokens(original,"en")
    after=numeric_tokens(translation,"de")
    if before==after:
        # Equal digits are not evidence that an explicitly changed unit is
        # equivalent (10 miles != 10 kilometres). Compare recognized units.
        original_units = _units(original, "en")
        german_units = _units(translation, "de")
        if len(original_units) == len(german_units) == 1:
            english, german = original_units[0], german_units[0]
            same_unit = (english[3] == german[3] and english[4] == german[4]
                         and (english[2], german[2]) not in AMBIGUOUS_PAIRS)
            if not same_unit:
                remaining_en,remaining_de,converted = _conversion_notes(original, translation)
                if converted and remaining_en == remaining_de:
                    return {"warnings":[],"notes":[
                        {"kind":"verified_unit_conversion",**row} for row in converted]}
                return {"warnings":[{"kind":"unit_mismatch",
                                    "english_unit":english[2],
                                    "german_unit":german[2]}],"notes":[]}
        return {"warnings":[],"notes":[]}
    remaining_en,remaining_de,converted=_conversion_notes(original,translation)
    notes=[{"kind":"verified_unit_conversion",**row} for row in converted]
    if remaining_en==remaining_de:
        return {"warnings":[],"notes":notes}
    return {"warnings":[{"kind":"numeric_mismatch",
                         "english_numbers":dict(remaining_en),
                         "german_numbers":dict(remaining_de)}],"notes":notes}

def audit_record(original, draft):
    warnings,notes=[],[]
    for field in ("q","a","alt","w"):
        details=audit_field(original[field],draft[field])
        for issue in details["warnings"]:
            warnings.append({"field":field,**issue})
        for note in details["notes"]:
            notes.append({"field":field,**note})
    return {"warnings":warnings,"notes":notes}
