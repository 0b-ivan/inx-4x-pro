"""Conservative German orthography fixes for X4 Pro trivia content.

Only fix individually verified spelling mistakes. Never generically replace
ASCII 'ae/oe/ue' or plain vowels; these also occur in valid names/words.
The e-ink reading_serif_14 font supports ä ö ü Ä Ö Ü ß.
"""
import re

# Canonical spellings for observed translation-model errors and unambiguous
# common transcriptions. These spellings are German, not English distractors.
_CANONICAL = {
    "munchen": "München",
    "muenchen": "München",
    "konig": "König",
    "koenig": "König",
    "konige": "Könige",
    "koenige": "Könige",
    "danemark": "Dänemark",
    "daenemark": "Dänemark",
    "nobelpreistrager": "Nobelpreisträger",
    "nobelpreistraeger": "Nobelpreisträger",
    "fruh": "früh",
    "frueh": "früh",
    "spater": "später",
    "spaeter": "später",
}
_PATTERN = re.compile(r"\b(?:" + "|".join(map(re.escape, sorted(_CANONICAL, key=len, reverse=True))) + r")\b", re.IGNORECASE)


def _replace(match):
    source = match.group(0)
    canonical = _CANONICAL[source.lower()]
    if source.isupper():
        return canonical.upper()
    if source[0].isupper():
        return canonical[0].upper() + canonical[1:]
    # Proper noun examples (München, Dänemark) retain German capitalization.
    return canonical


def normalize_orthography(value):
    """Repair *known*, unambiguous mistakes without changing source semantics."""
    return _PATTERN.sub(_replace, value) if isinstance(value, str) else value


def remaining_orthography_errors(record):
    """Return exact uncorrected tokens in q/a/alt/w for the release QA gate."""
    results = []
    for field in ("q", "a", "alt", "w"):
        value = record.get(field, [] if field in ("alt", "w") else "")
        for item in (value if isinstance(value, list) else [value]):
            if isinstance(item, str):
                results.extend(match.group(0) for match in _PATTERN.finditer(item))
    return results
