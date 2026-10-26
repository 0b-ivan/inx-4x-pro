# Independent AI editorial audit — German TRIVIA pilot

Date: 2026-10-26. **Not a German release pack. No human approval.**

- Source: existing 20-question Claude Sonnet 4.6 pilot
  ([run 40300699483](https://github.com/0b-ivan/inx-4x-pro/actions/runs/40300699483))
- Independent bilingual editing: `openai/gpt-5.4`
  ([successful run 40303848778](https://github.com/0b-ivan/inx-4x-pro/actions/runs/40303848778)).
- Model output: **11 accept**, **8 rewrite**, **1 reject**; all still
  `reviewed: false`. An offline post-audit check below identifies one
  additionally unsafe rewrite. This changes the **effective** human-review
  queue, not the immutable counts of that historical GitHub run.

## High-priority findings

**`b7905c820c174775` — Lesotho / South Africa: reject.**
The source asks *which country surrounds Lesotho*, with correct English
answer **South Africa**. The first German translation reverses the clue and
answer, asking for the land surrounded by South Africa and giving Lesotho.
GPT-5.4 detected the reversal and refused to accept it. A source-faithful
proposed replacement (for human verification, not automatically applied):

- Question: `Welches Land umgibt Lesotho vollständig?`
- Correct answer: `Südafrika`
- Wrong-option count: unchanged (none in the original)
- Editorial status: **not reviewed**.

**`4361045fe3c5888e` — Kon-Tiki duration: source-data factual error.**
The original English clue says the journey ended **10 days** after leaving
Callao in 1947; the earlier German draft said **101 days**. GPT-5.4 changed
the German draft back to **10**, matching the source but making it **factually
wrong**. The [Kon-Tiki Museum](https://www.kon-tiki.no/en/heyerdahls-expeditions/kon-tiki)
confirms the expedition lasted **101 days**. Treat the English source clue
as erroneous and quarantine the record; neither model's automatic acceptance
is a substitute for fact-checking. Regression guard now blocks Arabic-number
differences from source or introduced by the reviewer. Correct source data
and recompute `source_hash` before any eventual publication.

**`91dc3e63ed26111d` — Connecticut / Interstate directions.**
The original German `nimm die I-95 ... bis zur I-395 nord` was not idiomatic.
GPT-5.4 repaired the preposition but left an imperative that still reads like
navigation instructions. Suggested source-faithful edit for human approval:
`In welchem US-Bundesstaat führt die Fahrt von Norwalk nach Norwich erst über
die I-95 ostwärts und dann über die I-395 nordwärts?` Answer: Connecticut.

**`4738d68c98caead6` — Turkey meat: English wordplay.**
Source answer `White` refers to white meat versus dark meat. German `Hell`
is a potential contextual localization, but the clue about an `Antonym`
is needlessly awkward, and the GPT-5.4 rewrite did not fully resolve it.
Suggested wording to review: `Wie lautet bei Truthahnfleisch das Gegenstück
zu "dunkel"?` Answer: `Hell`. Check that the English distractors still
make sense in German.

**`ccddc63ea1f1b7ae` — English flower-name pun.**
The model's revised version still requires decoding `painted`, `beard`
and `adder's` in English, and returns `Tongue`. That is a bilingual
question, not a fully translated German question. Either label it
explicitly as an English-language word puzzle with an understandable
German answer `Zunge`, or exclude it from a German-only pack. The
automated reviewer did not reliably detect the remaining localization
defect.

**`7a13594f5a3619ca` — Schloss Nymphenburg: improved.**
The editor correctly replaced the nonidiomatic `Nymphenburger Palast` with
`Schloss Nymphenburg` while preserving the intended answer München.

## Release policy

Automated `accept` or `rewrite` **never** writes `reviewed: true`.
Edits require binary-size, real-font, orthography and answer-count validation.
Numerical facts now also require source/translation consistency and explicit
human inspection if a model proposes changes. For specialized historical
facts, use an independent primary source rather than automatically copying
any suspect number from the source dataset.

The original GitHub audit JSONL is kept intact for traceability. The
regression implemented **after** that run does not retroactively change the
old stored artifact. A fresh offline revalidation should be done before
using the amended drafts for a release.
