# CAW Deutsch-Pilot – redaktionelle Validierung 2026-10-27

Status: Redaktioneller Korrekturvorschlag, **KEINE Release-Freigabe**. Alle Einträge bleiben reviewed=false.

Quelle: tools_local/cardparty_l10n/examples/cards-en.jsonl
Erstentwürfe: tools_local/cardparty_l10n/examples/cards-de-unreviewed.jsonl
Vorschläge: tools_local/cardparty_l10n/examples/cards-de-editorial-proposed.jsonl

## Befunde
- 20/20 Karten besitzen gültige IDs, Quell-Hashes und Übersetzungsfelder.
- Die sechs Fragekarten behalten Pick 1/2/3 sowie Draw 0/2. Alle 20 Entwürfe wurden textlich überarbeitet (teilweise nur Interpunktion).
- Der ursprüngliche KI-Entwurf hat mindestens 19 Fehlerhinweise: vier fallabhängige Satzlücken, vierzehn Antwortkarten mit abschließendem Satzpunkt und eine alarm/Wecker-Fehlübersetzung. Hinzu kommen Hinweise zu uneindeutiger Mehrfachkombination.
- Nach der Überarbeitung verwenden die Mehrfachfragen grammatisch unabhängige Slot-Bereiche; Antworten enden ohne Punkt.
- Ein deterministischer Test verknüpft 2.590 geordnete Kombinationen und prüft Anzahl der Slots, Interpunktionskollisionen sowie das 512-Byte-Textbudget.
- Kein pixelgenauer E-Ink-Font-Test, keine Smartphone-Viewport-Abnahme und keine automatische redaktionelle Freigabe.

## Einzelbeurteilung

| Karte | Befund / Verbesserung |
| --- | --- |
| prompt-001 | Das Original „wegen ____“ benötigt gebeugte Artikel. Vorschlag: „Das Deployment ist fehlgeschlagen. Der Grund: ____“ |
| prompt-002 | „Als Hauptursache ... steht ____ drin“ ist holprig und hat Satzzeichenrisiko. Vorschlag: „Im Postmortem steht als Hauptursache: ____“ |
| prompt-003 | „beinhaltet ____“ erzwingt Akkusativ. Vorschlag: zwei separat nummerierte Slots |
| prompt-004 | „hat ____, ____ und ____ entdeckt“ benötigt Akkusativ. Vorschlag: drei separat nummerierte Slots |
| prompt-005 | Satzzeichen nach eingesetzter Antwort führen zu Doppelpunkt-Punkt-Kollisionen; abschließender Satzpunkt nach dem Blank entfernt |
| prompt-006 | „haben ____ durch ____ ersetzt“ benötigt Akkusativ; Vorschlag mit Vorher/Nachher trennt beide Slots ohne Deklinationszwang |
| answer-001 | „Kaffeemaschine, die Kubernetes betreibt“ wurde zu idiomatischerem „auf der Kubernetes läuft“ angepasst |
| answer-002 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-003 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-004 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-005 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-006 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-007 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-008 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-009 | Sinnverschiebung: Englisch „alarm“ bedeutet hier Alarm/Alarmmeldung, nicht zwingend „Wecker“. Vorschlag „Ein Alarm, der nur an Feiertagen losgeht“ |
| answer-010 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-011 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-012 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-013 | Bedeutung passend, abschließenden Punkt entfernt |
| answer-014 | Bedeutung passend, abschließenden Punkt entfernt |

## Grenzen und Rest-Risiko

1. Ein Regex-Lint garantiert weder Humor noch idiomatische Grammatik beliebiger Formulierungen. Ein humaner zweisprachiger Review bleibt notwendig.
2. Die Nummerierung und senkrechten Trennstriche innerhalb der Pick-2/3-Fragen müssen auf echter 800×480-E-Ink-Hardware und in Smartphone-Browsern geprüft werden.
3. UTF-8-Umlaute brauchen passende tatsächliche Firmware-Glyphen. Byte-Länge garantiert keinen passenden Textumbruch.
4. Die CC0-Lizenz des eigenen Demo-Decks ist eine Inhalts-/Rechteerklärung, kein Nachweis für beliebige Drittdecks.
5. Die vorgeschlagenen Übersetzungen bleiben reviewed=false, dürfen nicht automatisch in .dat-Decks und nicht in Releases übernommen werden.

## Reproduzieren
- Offline Unit Tests: python3 -m unittest discover -s tools_local/cardparty_l10n -p 'test_*.py' -v
- Structural QA: python3 tools_local/cardparty_l10n/translate_cards.py validate --manifest tools_local/cardparty_l10n/examples/deck.json --source tools_local/cardparty_l10n/examples/cards-en.jsonl --drafts tools_local/cardparty_l10n/examples/cards-de-editorial-proposed.jsonl
- Redaktioneller Lint: python3 tools_local/cardparty_l10n/editorial_checks.py --manifest tools_local/cardparty_l10n/examples/deck.json --source tools_local/cardparty_l10n/examples/cards-en.jsonl --drafts tools_local/cardparty_l10n/examples/cards-de-editorial-proposed.jsonl

Der Workflow CardParty Translation QA führt diese Prüfungen bei Änderungen erneut durch.
