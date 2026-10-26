# AWS DevOps Professional study deck for X4 Pro (draft)

This implementation introduces a **separate**, English-language study mode
without changing the existing public TRIVIA XTRIVIA v1 records or the German
translation. Questions and explanations are loaded from the SD card.

**Do not add licensed/purchased practice exams to this public Git repository.**
The tool operates only on a personal JSONL file outside the checkout.
The project has no built-in Udemy authentication, downloads or question bank.

## Generate a personal pack

First export your own English study JSONL privately (see draft PR #42 and
docs/apps/trivia-aws-udemy-import.md there). The input fields are:

- q: full English scenario
- options: 2 to 6 English answer strings
- correct: zero-based indices, one or multiple
- explanation: English plain-text feedback
- type: "single" or "multi"
- category: "Domain N: Name" with N from 1 through 6

From the CrossPlay checkout:

~~~sh
python3 tools_local/trivia/build_aws_study.py \
  --src "$HOME/Documents/Private-AWS/aws-devops-english.jsonl" \
  --dry-run

python3 tools_local/trivia/build_aws_study.py \
  --src "$HOME/Documents/Private-AWS/aws-devops-english.jsonl" \
  --out "$HOME/Documents/Private-AWS/pack.dat"
~~~

Copy the personal output to **/trivia/aws/pack.dat** on the device's SD card.
The source, generated binary, exported HAR and extracted explanations should
remain private; they are never compiled into the publicly distributed firmware.

## Device controls

- Open TRIVIA and select **AWS DEVOPS**.
- **STUDY QUESTIONS**: read scenario pages, then review every option. MARK
  or UNMARK an option, NEXT through all options, and GRADE the final selection.
  Single-answer questions replace an earlier mark; multi-answer questions
  toggle each selected letter.
- Review the **exact correct option letters**, your selection and the English
  explanation. NEXT QUESTION starts the next round.
- **SELECT DOMAINS**: tap any of the six DOP-C02 domains to enable/disable.
  ALL ON and DONE are available at the bottom.
- **REVIEW MISSED**: revisit questions previously answered incorrectly. A correct
  retry removes the question from the missed queue while retaining its seen state.
- **END** opens a session summary with score, accuracy, seen questions and
  still-missed questions in enabled domains. Finishing the last missed retry
  automatically shows a completed-review summary.

Answer choices are shown individually to avoid truncating the often very long
exam options. The question, individual answers and explanation are paginated
to preserve text that previously would not fit in a single e-ink panel.
Images in the imported explanations appear as [IMAGE] placeholders, not
downloaded content. Exam timer and timed final-review mode are not yet present.

Progress lives in /trivia/aws/pack.state, one byte per question. The firmware
writes /trivia/aws/pack.state.id as an identity stamp. **When a different
questions pack is installed, the saved seen/missed state resets**, even when
the two packs contain the same number of questions. The category mask lives
in /trivia/aws/prefs. Existing /trivia/pack.dat, /trivia/pack-de.dat and
their progress files remain unchanged.

## Binary format: XAWSDEV v1

- Header (20 bytes): 8-byte XAWSDEV + NUL magic, uint16 version=1, two
  zero flag bytes, uint32 count, uint32 content fingerprint (first four
  bytes of SHA-256 digest of the concatenated record blob, little-endian).
- uint32 little-endian offset index with count+1 elements.
- Per record: domain [1..6], option count [2..6], exact correct-bitmask,
  reserved zero byte, then NUL-terminated UTF-8 strings in order: question,
  plain explanation, options.
- Whole record must fit in 12 KiB; an oversized/corrupted record is rejected
  and never truncated.
- No resident index and no heap copy. Each record is read into one fixed-size
  buffer owned by the Trivia activity.
- The fingerprint is a progress identity **not** an authentication or
  cryptographic integrity check. Treat SD content as untrusted.

## Verification

~~~sh
host-tests/trivia/run.sh
python3 tools_local/trivia/test_aws_study.py
~~~

CI builds the actual X4 Pro firmware and the SDL simulator. Physical hardware
validation is still required before merging or making OTA releases.

## General knowledge categories

The legacy 50k-question Jeopardy-based binary file does not contain category
identifiers. This PR deliberately does **not** silently assign 50k questions
to fabricated categories. Source-backed metadata plus a version-bound
sidecar category index will be a separate change.
