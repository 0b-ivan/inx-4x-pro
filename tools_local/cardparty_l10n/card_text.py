"""Text rules shared by the CAW card importers."""
import re
import unicodedata

BLANK="____"
# CAH convention: a pick-3 prompt lets everyone draw 2 extra cards first.
DRAW_FOR_PICK={1:0,2:0,3:2}
# Characters the X4's UI font has no glyph for, with what the card shows instead.
X4_GLYPH_FALLBACK={"™":"(TM)"}

def tidy(text):
    """Blanks to ____, one space between words, NFC, X4-drawable characters."""
    text=re.sub(r"_{3,}",f" {BLANK} ",text)
    text=re.sub(r"\s+"," ",text).strip()
    text=re.sub(rf"{BLANK} (?=[.,!?:;“\"'])",BLANK,text)
    for char,fallback in X4_GLYPH_FALLBACK.items():
        text=text.replace(char,fallback)
    return unicodedata.normalize("NFC",text)

def normalized(text):
    """Key for duplicate detection: case, punctuation and blanks ignored."""
    text=unicodedata.normalize("NFKC",text).casefold().replace(BLANK," ")
    return " ".join(re.sub(r"[^\w]+"," ",text).split())
