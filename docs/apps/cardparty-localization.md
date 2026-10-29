# Cards Against Whatever – Übersetzungspipeline (Milestone 2, getrennte Spur)

Dieser Arbeitsbereich berührt ausschließlich tools_local/cardparty_l10n/,
docs/apps/cardparty-localization.md und die beiden dedizierten GitHub-Actions
Workflows. Claude kann die Firmware im selben Draft-PR #47 entwickeln.
Keine Karten aus CAH oder fremde proprietäre Decks werden übernommen.

## Konzept

1. Versionierte Originalquelle: deck.json (ID, Version, Rechteinhaber,
   Inhaltslizenz, Quellsprache Englisch) + cards-en.jsonl (stabile IDs).
2. Jeder prompt hat Text, pick=1..3 und draw=0..2. Jeder prompt muss exakt
   pick viele literale Platzhalter ____ enthalten; Antworten haben keine.
3. Jede Übersetzung bindet ID + SHA-256 der Originalkarte inkl. Lizenz,
   Regeln und Deckversion. Bei Quellenänderung wird die Übersetzung ungültig.
4. OpenRouter generiert nur Entwürfe; JSONL wird nach jeder erfolgreich
   geprüften Karte gespeichert, um lokale Fortsetzung zu erlauben.
5. Die Entwürfe tragen IMMER reviewed=false. Human-Review prüft Witz,
   Grammatik, Lücken, Umlaute, Renderbarkeit und Inhaltslizenz.
6. Die Release-Prüfung verlangt vollständige reviewed=true Einträge mit
   unveränderten Quellen-Hashes. Sie erzeugt bewusst KEIN .dat.
7. Später: kompilierter versionierter Indexed-SD-Pack in
   /games/cardparty/*.dat; GitHub Deck-Releases, SHA-256, atomare
   Installation und Katalog. Kein Teil dieses Milestone 2 ist
   automatisch in der aktuellen X4-Firmware aktiv.

## Offline-Tests ohne API-Kosten

    python3 -m unittest discover -s tools_local/cardparty_l10n -p 'test_*.py' -v
    python3 tools_local/cardparty_l10n/translate_cards.py validate \
      --manifest tools_local/cardparty_l10n/examples/deck.json \
      --source tools_local/cardparty_l10n/examples/cards-en.jsonl \
      --drafts /tmp/caw-de-proposals.jsonl

QA workflow: .github/workflows/cardparty-translation-qa.yml.
Die 20 englischen Originalkarten in examples/ wurden neu für dieses
Pilotvorhaben verfasst. Lizenzangaben fremder Quellen nicht ungeprüft
übernehmen. Aktuell sind CC0-1.0, CC-BY-4.0 und MIT technisch erlaubt,
was keinen Rechtsnachweis ersetzt.

## Bezahlter OpenRouter-Pilot (explizit, max. 20 Karten)

Nur ein Push auf feature/cards-against-whatever mit einer Änderung an
.github/cardparty-translation-trigger.txt löst den Pilot-Workflow aus.
Dateiinhalt muss GENAU sein:

    TRANSLATE-ORIGINAL-DE-PILOT-20

Der Workflow .github/workflows/cardparty-translation-pilot.yml liest das
Repository-Secret OPENROUTER_API_KEY und nutzt strukturierte JSON-Antworten
eines OpenRouter-Modells. Er übersetzt nur die eigenen 20 englischen
Testkarten, startet niemals automatisch durch normale Firmware-Commits,
wird nicht periodisch ausgeführt und schreibt weder Code-Commits noch
Firmware-/Deck-Releases.

Das einzige Ergebnis ist das GitHub-Actions-Artefakt
cardparty-original-de-unreviewed (2 Tage Aufbewahrung). Bei Fehlern
wird ein bereits existierendes JSONL-Checkpoint soweit vorhanden
ebenfalls zur Diagnose hinterlegt. Modelloutputs sind UNREVIEWED.
Unterbrochene lokale Läufe können mit derselben --drafts Datei weiter
übersetzen. Ein späterer Workflow zur Wiederherstellung früherer
CI-Artefakte ist noch NICHT implementiert.

Lokaler CLI-Aufruf:

    OPENROUTER_API_KEY=... python3 tools_local/cardparty_l10n/translate_cards.py translate \
      --manifest tools_local/cardparty_l10n/examples/deck.json \
      --source tools_local/cardparty_l10n/examples/cards-en.jsonl \
      --drafts ./cards-de-unreviewed.jsonl --limit 20 \
      --model anthropic/claude-sonnet-4.6

Freigabeprüfung nach separater manueller redaktioneller Entscheidung:

    python3 tools_local/cardparty_l10n/translate_cards.py validate \
      --manifest DECK/deck.json --source DECK/cards-en.jsonl \
      --drafts DECK/cards-de-reviewed.jsonl --for-release

## Prüfkriterien vor einer Deck-Freigabe

- Humor, kulturelle Bedeutung, Wortwitz, Grammatik, Satzbau,
  Gender/Kasus und sinnvolle Kombinationen mit beliebigen Antworten.
- Exakte Anzahl/Position von ____, Pick-Reihenfolge und Draw-Regel.
- Natürliche deutsche Umlaute, Zeichenunterstützung der tatsächlichen
  X4-Schrift, Textlängen für E-Ink und mobile Browser.
- Keine Rechtsverletzungen durch Quellen, Übersetzungen oder API-Uploads.
- Keine fehlenden, doppelten oder nach Quellenänderung veralteten Karten.
- Menschlich geprüfte und einzeln genehmigte Lokalisierungsentwürfe.

Vorlage aus dem existierenden Trivia-Projekt:
tools_local/trivia/translate_de_pack.py, build_de_pack.py,
pack_format.py und docs/apps/trivia-pack-format.md.
Trivia-spezifische Fakten-/Distraktor-Logik ist bewusst NICHT die
gleiche Validierungslogik wie CAW-Humorkarten.
