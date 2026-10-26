# Private English AWS practice-test import (Udemy JSON)

This is an **offline intake tool**, not yet an installable Trivia pack. It does
not call Udemy, automate a browser, request credentials, or bypass access
controls. It accepts a JSON response manually saved from a course you can
legitimately view in your own browser. The accepted shape is based on an
older, publicly documented Udemy quiz response; **Udemy can change it**.

## Why a separate format?

The existing XTRIVIA v1 stores one short question and one correct answer,
with 0–6 distractors, in at most 448 bytes. It cannot represent long DOP-C02
scenarios, multiple correct choices or answer explanations. Producing
pack.dat from those questions would silently discard essential information.

This importer creates an intermediate JSONL file for a future dedicated AWS
study-pack builder. Firmware loading, category filters, multi-select UI,
explanations and study progress are **not part of this change**.

The content stays in **English**. UI localization can remain German.

## Export the review data yourself

1. Open the purchased Udemy practice test in your browser.
2. Finish the practice test and open **Review questions**.
3. Open Chrome DevTools (Cmd + Option + I) → **Network** → **Fetch/XHR**.
4. Reload Review and look for an assessment/question response; inspect the
   **Response** tab and check whether it has the fields shown below.
5. Save *only the response JSON* to a private directory outside the git clone,
   e.g. ~/Documents/Private-AWS/assessments.json. Do not export HAR files,
   copy request headers, share access tokens, cookies or personal data.
6. If the response has next pagination, save each page. Never assume the
   first result page contains every question.

Use this procedure only for course material you are authorized to view.
Respect Udemy's terms and copyright; the raw questions and imported output
must **not** be committed to this public repository, uploaded as Actions
artifacts or distributed as public releases.

## Run locally on macOS or Linux

From the CrossPlay repository root:

```sh
python3 tools_local/trivia/import_udemy_practice.py \
  --src "$HOME/Documents/Private-AWS/assessments.json" \
  --dry-run

python3 tools_local/trivia/import_udemy_practice.py \
  --src "$HOME/Documents/Private-AWS/assessments.json" \
  --out "$HOME/Documents/Private-AWS/aws-devops-english.jsonl"
```

If the data spans multiple saved pages, pass **all** paths after --src
and add --allow-incomplete; that option does **not** verify you actually
captured the last page. Duplicate questions cause the importer to fail.
The importer refuses to overwrite an existing output without --overwrite.
It also refuses to write any output inside the git repository.

The parser fails closed when answer keys cannot be matched to answer text.
There is no silent omission of invalid questions. Confirm the detected
question count against your completed Udemy practice test.

## Known input shape (illustrative, original sample only)

```json
{
  "results": [
    {
      "prompt": {
        "question": "Which AWS service stores objects?",
        "answers": ["Amazon S3", "Amazon EBS", "Amazon EFS", "Amazon FSx"],
        "explanation": "Amazon S3 offers object storage."
      },
      "correct_response": ["Amazon S3"],
      "section": "Example"
    }
  ],
  "next": null
}
```

A multi-answer question has two or more matching strings in
correct_response. The tool also recognizes single-letter keys A–F when
those keys are not themselves answer texts, but rejects ambiguous or
unsupported formats. All answer choices must be unique.

If the course's actual JSON differs, provide a **redacted, minimal shape**
(field names/types, with dummy question/answer text and no sensitive metadata)
before extending the parser. Do not add guessed mappings.

## Output

One JSON object per line with only these allowlisted fields:

```json
{
  "id": "stable-content-hash",
  "type": "single",
  "q": "English question text",
  "options": ["Option A", "Option B"],
  "correct": [0],
  "explanation": "English explanation",
  "source_section": "Section heading",
  "category": "Uncategorized"
}
```

The correct list holds **zero-based indexes into options**. type is
single or multi. category is intentionally unassigned: a Udemy
section is not necessarily one of the six DOP-C02 exam domains. Categorization
must be reviewed separately.

For privacy the importer intentionally drops Udemy user IDs, attempt data,
cookies, result URLs and any unknown fields. It writes atomically using a
user-readable-only temporary file. The output is still purchased course
content and remains private.

## Tests

```sh
python3 tools_local/trivia/test_import_udemy_practice.py
host-tests/trivia/run.sh
```

All fixtures use invented questions; no Udemy content or account is needed.

## Next independent workstreams

- Validate the parser against one sanitized, real Review JSON response.
- Design a study-pack **v2 or sidecar** for long scenario text, multiple
  correct responses and explanations, with memory/display constraints.
- Add separate selectable decks and per-deck progress without disturbing
  /trivia/pack.dat, pack-de.dat or their state files.
- Implement category toggles and DOP-C02 domain mapping, then test on the
  SDL simulator and the physical X4 Pro.
- Keep authored, redistributable AWS questions distinct from privately
  imported licensed material.
