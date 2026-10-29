# Moralisch Bankrott — unveröffentlichter CAW-Entwurf v3

24 schwarze Karten (20× Pick 1, 4× Pick 2) und 80 weiße Karten. Deutsch, 18+.

## Redaktioneller Stand
* Alle Kartentexte sind neu geschrieben. Makabrer Satirehumor über Täter, Vorurteile, Kriegsverbrechen, Religion und absurde Sexklischees.
* Keine Freigabe: `reviewed:false`, `rights_status:unverified`, `editorial_status:unapproved`. Vor Veröffentlichung menschliche Redaktion, Rechteklärung und Lizenzentscheidung erforderlich.
* Keine Änderung am öffentlichen Pack-Katalog, keine OTA-Veröffentlichung, keine Übernahme auf main.
* Alle Karten sind als `ab-18` kategorisiert. Diese Kategorie ist standardmäßig deaktiviert.
* `--draft` erzeugt nur testbare Entwurfsdaten. Die Firmware akzeptiert keine Draft-Packs.

## Test-Build
```sh
python3 tools_local/cardparty_l10n/build_sd_pack.py \
  --manifest tools_local/cardparty_l10n/drafts/moralisch-bankrott-v3/deck.json \
  --cards tools_local/cardparty_l10n/drafts/moralisch-bankrott-v3/cards.jsonl \
  --out /tmp/moralisch-bankrott-v3.dat --draft
```

## Strukturelle Prüfung
* 24 Fragen, 80 Antworten, 4 Pick-2.
* Einzigartige IDs und Textkörper, gültige Kategorien.
* `____`-Anzahl = Pick, Antworten ohne Platzhalter und ohne Satzzeichen am Ende.
* Maximale UTF-8-Länge eingehalten (Fragen 384 / Antworten 160 Bytes).
* 1600 mögliche Pick-1-Paarungen und 25280 geordnete Pick-2-Paarungen mit verschiedenen Karten (mathematische Zahl, keine Humorgarantie).
* Mechanisch geprüft, kein vollständiger nativer Firmware- oder Simulator-Test und keine abgeschlossene menschliche Redaktion.
