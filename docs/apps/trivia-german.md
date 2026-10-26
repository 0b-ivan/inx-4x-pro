# German TRIVIA question pack

The firmware UI translations and the **question contents** are separate.
The original 50,000-question pack is English, derived from Jeopardy! clues.
Nothing in `german.yaml` translates the 50,000 question/answer records.

## On-device behavior

| Firmware UI language | Installed pack | What TRIVIA opens |
| --- | --- | --- |
| English or other | `/trivia/pack.dat` | English questions |
| German | `/trivia/pack-de.dat` | German questions |
| German, without usable DE pack | `/trivia/pack.dat` | English fallback, **clearly labeled** |
| German, neither pack | none | Notice that the current Wi-Fi download is English |

The legacy URL still downloads **English only** from
[the upstream `trivia-pack` release](https://github.com/ma-r-s/crossplay/releases/tag/trivia-pack).
It does not claim to download a German pack that has not been published.
The files `pack.state` and `pack-de.state` store independent progress and
flags; changing UI languages never reuses the other pack's state.
Both languages use XTRIVIA format version 1. The header explicitly marks German data with flag bit 0; legacy English packs have flags 0. Firmware rejects a file whose header language does not match its selected file path, preventing an English pack renamed to `pack-de.dat` from being presented as German.

## Start a 20-question pilot from a phone (GitHub Actions)

No Mac or local terminal is required. Use Safari/Chrome on your phone;
GitHub's mobile app may not expose repository-secret settings, so request
the **desktop website** if necessary.

1. Revoke the OpenRouter key that was previously pasted into a chat.
   Make a fresh, temporary key with a low spending limit and short expiry.
2. Open [Actions secrets](https://github.com/0b-ivan/inx-4x-pro/settings/secrets/actions)
   and create a **repository secret** named `OPENROUTER_API_KEY`.
   Paste the fresh key there, **not into a GitHub issue, PR, chat or file**.
3. Open the existing [CrossPlay workflow](https://github.com/0b-ivan/inx-4x-pro/actions/workflows/crossplay-ci.yml).
   Choose **Run workflow**, select branch
   `fix/i18n-translation-quality-audit`, leave `publish_nightly` off,
   and select **Run workflow** again.
4. The `German quiz translation pilot (20 questions)` job downloads the
   **SHA-256 pinned** upstream English pack, extracts its original questions,
   selects at most 20 deterministically distributed questions with distinct answers across the full English pack and sends them to OpenRouter (`anthropic/claude-sonnet-4.6`), validates
   and exports `trivia-german-pilot-unreviewed` as a GitHub Actions artifact.
   The artifact is retained for **one day** and must be reviewed before use.
5. Afterwards delete the Actions secret and revoke the temporary OpenRouter
   key. A finished pilot does not produce or publish a release package.

The workflow job only runs on an **explicit manual dispatch of this exact
branch**. Regular pull requests, pushes and release jobs will **not** use the
OpenRouter key. The existing GitHub Actions workflow file is on the default
branch; the extra pilot job is only defined on this pull-request branch.
Each full workflow dispatch may also run the repository's ordinary CI jobs.

## Build from the publicly released English pack

The original, enormous seasons 1–42 TSV is **not required**. Export the
English `pack.dat` currently offered by the firmware:

```sh
curl -fL https://github.com/ma-r-s/crossplay/releases/download/trivia-pack/pack.dat \
  -o pack.dat

python3 tools_local/trivia/export_pack.py \
  --pack pack.dat --out /tmp/trivia-source-en.jsonl
```

The exporter derives a stable per-record ID from its original position and
English clue. That ID is specific to the source binary; it is **not** an
upstream Jeopardy question ID. When the upstream pack changes, re-export and
reconcile the translations by their source hashes.

The optional draft translator calls OpenRouter. Only send source questions
you are permitted to process through the chosen provider; check the dataset's
usage rights and model provider terms. **Pricing and tokens are not capped by
the script**; start with a small pilot:

```sh
export OPENROUTER_API_KEY='your-own-api-key'

python3 tools_local/trivia/translate_de_pack.py \
  --src /tmp/trivia-source-en.jsonl \
  --out /tmp/trivia-draft-de.jsonl \
  --model 'your/openrouter-model' --limit 20
```

It appends one validated record at a time and resumes by ID. A translation is
always marked `"reviewed": false`, regardless of the language model's output.
Repeat the command to translate further; `--all` explicitly requests all
remaining items and may incur **substantial** token costs. No job starts
automatically or runs in the background.

### Review and build

Review the German clue, correct answer, accepted alternates, all six
distractors where present, historical context and locale-sensitive wording.
Reviewing only the question is insufficient: translating a distractor may make
it synonymous with the answer. Set `"reviewed": true` **individually after
review**.

The two JSONL files must have complete matching IDs and record counts.
Every translation stores `source_hash`, calculated from the original clue,
answer, alternates and distractors. This rejects stale translations after any
source change.

```sh
python3 tools_local/trivia/build_de_pack.py \
  --src /tmp/trivia-source-en.jsonl \
  --translations /tmp/trivia-draft-de.jsonl \
  --out pack-de.dat
```

By default this rejects unreviewed or missing questions, mismatched choices,
a distractor equal to the right answer, unsupported display glyphs, clues
that exceed the real 448×583-pixel text box, and records above 448 bytes. It also measures the actual Quizmaster-answer and multiple-choice-button fonts, so translated labels cannot silently overflow their display rectangles.
It writes `pack-de.dat.part` first and replaces the final output only on
complete success. **No English fallback is inserted into a German pack.**

For internal smoke tests only, `--allow-draft` permits unreviewed entries.
Such files are *not* production-ready; do not distribute them as reviewed.

Copy the successfully built `pack-de.dat` to
`/trivia/pack-de.dat` on the X4 Pro's SD card. Leave the original
`/trivia/pack.dat` untouched. The application automatically selects the DE
pack when its UI language is German.

## Verification and release gate

```sh
host-tests/trivia/run.sh
python3 tools_local/trivia/test_de_pack.py
```

`host-tests/trivia/run.sh` already includes the Python suite. It checks
real binary round-trips with German UTF-8, review gating, source-drift
detection, choice counts, duplicates, maximum record length, and English/DE
progress separation. All tests run without an API key or internet access.

Before publishing a full pack, additionally sample complete quizzes on the
**real e-ink device** and review correctness and cultural/geographical
relevance. Pixel fit and binary validity do not prove that 50,000 machine
translations are factually right. Do not publish a supposedly German pack with
untouched English questions silently mixed in.
