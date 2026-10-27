# Cards Against Whatever – SD Pack Importer

Status: merged into the Cards Against Whatever branch (PR 47). Format 2 adds
categories, an adult flag and attribution. Native host, browser and SDL SD
checks pass; physical X4 hardware is NOT tested. See
[the QA record](cardparty-sd-pack-qa-2026-10-27.md) for the importer branch's
own evidence (written for format 1).

## Implemented

* Immutable versioned binary SD .dat format (2). Format 1 packs are rejected;
  none were ever published.
* Python compiler in tools_local/cardparty_l10n/build_sd_pack.py.
* Read-only C++ loader in src/apps_local/cardparty/CardPartySdPack.h.
* CAW Activity selects /games/cardparty/active.dat on entry and uses exactly
  one selected Deck for engine, browser and e-ink renderer.
* Missing/bad/unapproved file retains original built-in demoDeck.
* Prompts without any blank are valid; ones containing blanks must contain
  exactly PICK 1, 2 or 3 placeholders. Browsers already display question
  and submitted answers in separate fields.
* Supports UTF-8 including German umlauts. Rejects corrupt CRC, invalid
  UTF-8, non-terminated strings, oversize, extra bytes, bad counts or flags.
* Strict draft / release separation. User's dark humor v2 is NOT approved.
* No network endpoint to overwrite SD content and no download workflow.
* Up to 16 categories per pack. The host switches them on the X4 under
  `[03] CATEGORIES`; the choice is saved in `/games/cardparty/categories.bin`
  and keyed to the pack's CRC, so a new pack starts from its own defaults.
* A card plays only if none of its categories is off, so "Politik + FSK 18"
  stays out while FSK 18 is off. Untagged cards always play. Adult categories
  start off on every device, whatever the pack says.
* If the switches leave fewer answers than a full table needs
  (8 × 5 + 7 × largest draw + 3 for Rando), setup refuses to start and says so.
* The setup screen shows the pack's title, rights holder and license.

## Binary layout

Little endian. Header 32 bytes: magic XCAWPK2 + NUL (8), format uint16 (2),
flags uint16 (1=released, 0=draft), promptCount uint16, answerCount uint16,
blobSize uint32, CRC32 uint32 over the body, deckVersion uint32,
categoryCount uint8 (0-16), 3 zero bytes.

Body, each string as UTF-8 length uint16 + bytes + NUL:

1. Attribution: title, license, license URL, rights holder, source URL
   (each at most 200 bytes; all required in a released pack).
2. Categories: flags uint8 (1 = on by default, 2 = adult) + name
   (1-24 bytes).
3. Prompts: length uint16, pick uint8, draw uint8, category mask uint16,
   text.
4. Answers: length uint16, category mask uint16, text.

A mask bit beyond categoryCount rejects the pack. Total pack no larger than
192 KiB; max question 384 UTF-8 bytes, max answer 160 UTF-8 bytes;
1-4096 questions, 32-4096 answers.

## Building a pack

Only original or properly licensed, individually human-reviewed cards should
be marked eligible for production. Manifest must include:

    {
      "deck_id": "my-approved-deck",
      "version": 1,
      "source_locale": "de",
      "license": "CC0-1.0",
      "license_url": "https://creativecommons.org/publicdomain/zero/1.0/",
      "source_url": "https://example.org/my-deck",
      "rights_holder": "The Actual Rights Holder",
      "rights_status": "verified",
      "editorial_status": "approved",
      "title": "Grundspiel",
      "categories": [
        {"id": "boese", "name": "Böse"},
        {"id": "fsk-18", "name": "FSK 18", "default_on": false, "adult": true}
      ]
    }

Card rows are JSONL, with unique id, kind prompt or answer, text and
reviewed=true. Prompts also include pick and draw. A card may list category
ids, e.g. `"categories": ["boese", "fsk-18"]`. `default_on` defaults to true;
an adult category must set it to false or the compiler refuses. Run:

    python3 tools_local/cardparty_l10n/build_sd_pack.py \
      --manifest ./my-deck.json --cards ./cards.jsonl --out ./active.dat

Receipt active.receipt.json holds SHA256, version and licensing metadata.
Copy approved active.dat to /games/cardparty/active.dat on the SD card
while CAW is not running. A --draft compile marks the file as unapproved,
which the production C++ reader refuses.

CRITICAL: Boolean approval fields are only an operator workflow guard.
They are not an authenticity signature. The runtime currently verifies CRC32
against corruption, NOT SHA256 against a trusted publisher. Do not accept
untrusted network-delivered packages until a signed/catalog-trusted
hash, HTTPS download, temporary file installation, on-device verification
and atomic activation have been implemented and reviewed.

## Test coverage

Run Python offline unit suite and host C++ SD loader tests. An isolated
GitHub Actions workflow, CardParty isolated SD pack QA, runs exactly those
offline/compiler checks and a full x4pro firmware build. Its branch/path
filters include importer source and tests, so source-only fixes run QA too.
It does not restart firmware CrossPlay CI or publish OTA.

The offline suite also builds synthetic packages through the CLI, reads them
with the production C++ loader and independently decodes the real eight-seat
server's JSON. Pick 1-3, Draw 0-2, full-length escaped UTF-8 cards, private
hands, judging and complete winner text are checked. Run the real Activity
with isolated synthetic SD cards after building the simulator:

    python3 scripts_local/cardparty_sd_pack_smoke.py \
      --binary .pio/build/simulator_x4_pro/program \
      --out qa-artifacts/cardparty-sd/sdl --all-states

This verifies SD selection, a long UTF-8 word, missing/draft/corrupt/truncated/
oversize fallbacks and six distinct application phases. The simulator uses
fake seats; it does not exercise physical SD or Wi-Fi clients.

Hardware tasks still open: SD read/memory
measurements, mobile/e-ink glyph and overflow checks, actual 3-8 player
sessions, power-loss recovery. No OTA or production release is warranted.

## Copyright / license

Prior99, BixelPitch and Cards vs Humanity source catalogs are NOT repackaged
in this feature. Prior99's GPL label does not establish independent rights
to derivative CAH card text, and NC/SA materials need individual review,
proper attribution and license notices. The importer ships no third-party
card contents. The draft dark-humor deck needs separate human/legal approval.
