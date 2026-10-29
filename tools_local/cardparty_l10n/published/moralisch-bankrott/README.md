# Moralisch Bankrott — Adult pack v4

Original German CAW cards (74 prompts, 152 answers) from the owner's October 28-29 chat selection.
This deck is a **standalone pack** for `[03] DECK > Download from GitHub`, not a firmware/OTA image.

All content belongs to the `FSK 18` category, **OFF by default**. On the X4, select this deck and explicitly enable FSK 18 under `[04] CATEGORIES` before starting a match. The X4 uses one pack at a time; this does not add cards to `Alles.dat`.

There are 64 Pick-1 prompts and 10 Pick-2 prompts, 152 answers. No imported CAH third-party texts in this deck. License: CC0-1.0 as for the repository owner's existing original `nach-mitternacht` pack.

Build:
```sh
python3 tools_local/cardparty_l10n/build_sd_pack.py \
  --manifest tools_local/cardparty_l10n/published/moralisch-bankrott/deck.json --cards tools_local/cardparty_l10n/published/moralisch-bankrott/deck.jsonl \
  --out /tmp/Moralisch-Bankrott.dat
```

Owner instruction (2026-10-29): put these original cards in the repo so that the game can download them. The owner approved publication through this request. Automated tests establish technical validity, not subjective comedy quality or physical hardware success. No merge of PR #47 requested or performed.
