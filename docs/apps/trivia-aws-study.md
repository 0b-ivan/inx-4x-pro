# Zertifikats-Fragenpakete auf dem X4 Pro

Das Trivia-Lernmodul lädt beliebige Zertifikats-Fragensets im **XAWSDEV-v1**
Binärformat als getrennte SD-Dateien. Bestehendes klassisches TRIVIA (XTRIVIA)
mit englischen/deutschen Allgemeinwissensfragen bleibt unverändert.

## Namensgebung und Ablage

Das Zertifikat bestimmt den Dateinamen. Pro Zertifikat eine Datei:

```text
/trivia/
  aws/
    DOP-C02.dat       # AWS DevOps Engineer Professional
    DOP-C02.topics    # sechs fachliche Themenbezeichnungen
    SAA-C03.dat       # AWS Solutions Architect Associate
    SAA-C03.topics
  decks/
    LINUX.dat         # andere Übungsfragen
    LINUX.topics
```

**Kein `pack.dat` und keine Migration alter Lernstände erforderlich.**
Der AWS-Ordner **/trivia/aws/** bleibt erhalten. AWS-Zertifikate liegen dort
mit ihrem Zertifikatsnamen, nicht unter `pack.dat`. Andere Themen können
weiterhin unter `/trivia/decks/` liegen. Die Firmware durchsucht beide
Verzeichnisse beim Öffnen der Paketauswahl.

Erlaubt sind 1–30 ASCII-Zeichen A–Z, a–z, 0–9, `_` und `-`.
Maximal 12 Pakete werden gleichzeitig in der Auswahlliste angezeigt; je
Seite sechs Einträge. Nicht unterstützte, fehlende oder beschädigte Dateien
dürfen keine fremden Fragen und keine fremden Fortschritte überschreiben.

## Lokalen Fragenkatalog erstellen

Die Quelle ist eine **private**, selbst bereitgestellte JSONL-Datei mit
`q`, `options` (2–6), `correct` (Liste nullbasierter Antwortindizes),
`type` (`single`/`multi`), `explanation` und `category`.
`category` bezeichnet eines von sechs Themen, z. B.
`Domain 1: SDLC Automation`, `Topic 2: Linux Services` oder
`Category 3: Networking`. Pro Nummer muss der Name eindeutig sein.

```sh
python3 tools_local/trivia/build_aws_study.py \
  --src "$HOME/Documents/Private-AWS/aws-devops-english.jsonl" \
  --certificate DOP-C02 \
  --out "$HOME/Documents/Private-AWS/"
```

Der Befehl erzeugt `DOP-C02.dat` und `DOP-C02.topics`. Übertrage beide
Dateien in **`/trivia/aws/`** auf die X4-Pro-SD-Karte.

Nach der Installation: **TRIVIA → STUDY PACKS → DOP-C02**.
Im Gerätemenü kannst du Fragen üben, Fehlerfragen wiederholen, Themen filtern
und dir die Rundenauswertung ansehen. Die Fragen bleiben in der Sprache der
Quelldatei, unabhängig von deutschen Menüs.

## Öffentliche GitHub-Fragenpakete

**Standby / Sitzung fortsetzen:** Der X4 Pro startet nach Deep Sleep den Controller neu. Eine aktive AWS-Fragensession wird deshalb auf der SD-Karte in zwei abwechselnden, jeweils prüfsummengeschützten Dateien `<ZERTIFIKAT>.session0` und `.session1` gesichert. Beim Aufwecken stellt Trivia die letzte Frage, angekreuzte Antworten, Textseite, Lernmodus und bisherigen Rundenscore wieder her. Eine bereits bewertete Antwort wird nicht erneut bewertet. Die Checkpoints werden nur für die passende unveränderte Fragenbank (Fingerprint und Frageanzahl) gelesen. `LERNSTAND LOESCHEN` entfernt auch die zugehörigen Sessiondateien; der ausdrücklich beendete Durchgang verliert beim Verlassen der Zusammenfassung den Wiederaufnahmestand. Ohne SD-Karte ist eine dauerhafte Wiederaufnahme nicht möglich.

Das öffentliche Nightly-OTA enthält **keine** Fragenbank. Im Gerät:

1. **TRIVIA → STUDY PACKS → GITHUB** wählen.
2. WLAN verbinden; der X4 Pro liest `catalog.txt` aus dem öffentlichen
   GitHub-Release `trivia-study-packs`.
3. Unter den angebotenen Zertifikaten eines auswählen; die Firmware lädt
   `<NAME>.dat` und optional `<NAME>.topics` direkt aus diesem Release.
4. **PACKS** öffnen, das installierte Zertifikat auswählen und offline lernen.

Das GitHub-Release enthält zunächst nur `DOP-C02-DEMO` und
`SAA-C03-DEMO` mit jeweils 24 **originalen, synthetischen Testfragen**.
Diese Demonstrationsfragen sind keine Prüfungsvorbereitung.

Der öffentliche Katalog ist ein kleines ASCII-Dokument mit maximal
zwölf Einträgen, jeweils `AWS|DOP-C02-DEMO` oder `DECK|LINUX-DEMO`.
Weder URLs noch unzulässige Dateinamen werden daraus übernommen; die
Download-URL ist fest auf das GitHub-Release begrenzt. Ein heruntergeladenes
Paket wird vor der Installation als XAWSDEV-Datei geprüft. Bereits
installierte Dateien werden bei einem Fehler nach Möglichkeit wiederhergestellt.

Du kannst weiterhin dein persönliches `DOP-C02.dat` mit 150 Fragen direkt
auf die SD-Karte legen. **Privat erworbene Fragen werden nicht über GitHub
hochgeladen oder veröffentlicht.**

## Bedienung per Touch und Offline-Glossar

- **Fragen und Erklärungen:** Hoch-/runterwischen scrollt seitenweise durch
  lange Texte. Die sichtbare Scrollleiste zeigt Seite und Position; sechs
  berührbare Bereiche auf der Leiste erlauben das Springen zu Textabschnitten.
  Das vermeidet flackerndes Pixel-für-Pixel-Scrollen auf dem E-Ink-Display.
- **Antworten:** Links-/rechtswischen wechselt die Antwortkarte. Hoch-/
  runterwischen scrollt nur lange Antworttexte, niemals zur nächsten Antwort.
  Die Bedienelemente am unteren Rand bleiben sichtbar.
- **Wischen gibt nie ab und springt nie weiter:** Auf der letzten Antwort
  bzw. am Ende der Erklärung bleibt ein Hochwischen ohne Wirkung. Bewertet
  wird nur über **PRUEFEN / GRADE** bzw. **JETZT PRUEFEN**, die nächste Frage
  kommt nur über **NAECHSTE / NEXT QUESTION**.
- Schriftgröße: Leseschrift 14 px, wenn der ganze Text auf eine Seite passt,
  sonst 11 px (Untergrenze). Darüber hinaus wird geblättert, nie weiter
  verkleinert. Eine Seite wird mit den echten Glyphenbreiten umbrochen und
  füllt den Textbereich (bis 16 Zeilen) ohne Auslassungszeichen; der Text
  steht oben bündig.
- Einfachauswahl: **WAEHLEN** ersetzt die Markierung, ein erneutes Tippen
  (**ABWAEHLEN**) hebt sie auf. Mehrfachauswahl: jede Antwort einzeln an/aus.
- Bereits ausgewählte Antworten können auf **jeder Antwortseite** über
  **JETZT PRUEFEN / GRADE NOW** bewertet werden. Ohne Markierung wird
  nicht versehentlich abgegeben.
- Auf der **ersten Erklärungsseite** gibt es einen direkten Einstieg zur
  nächsten Frage, selbst wenn die Erklärung länger ist. Die Rundenübersicht
  kann hoch-/runtergewischt werden und bietet ebenfalls **NAECHSTE / NEXT**.
- **Inline-AWS-Glossar:** Erkannte Fachwörter (auch „IAM-Rolle“,
  „S3-Bucket“ und „Lambda“) sind gepunktet unterstrichen. Direktes Antippen
  oder langes Halten auf dem markierten Wort öffnet eine kurze Begriffserklärung
  als **Overlay über der aktuellen Frage/Antwort/Erklärung**, keine eigene
  Navigationsseite. Schließen oder außerhalb tippen kehrt unverändert an dieselbe
  Textstelle und Antwortauswahl zurück. Unbekannte Wörter öffnen kein Glossar.
  Kein WLAN erforderlich; die Erklärungen liegen lokal auf dem Gerät.
- **Lernstand zurücksetzen:** Im Menü des gewählten Zertifikats
  „LERNSTAND LOESCHEN“ auswählen und explizit bestätigen. Nur die gesehen-/
  falsch-Markierungen des aktuellen Pakets werden zurückgesetzt. Die Fragen,
  Bereicheinstellung, Fingerprint und Lernstände anderer Zertifikate bleiben.
- Die vertikale Textnavigation erfolgt weiterhin E-Ink-gerecht in Seiten,
  mit sichtbarem Scrollindikator und Sprungzielen statt animiertem Pixel-Scrolling.
  Nach Schließen des Glossars erzwingt die App einen vollen Refresh gegen Ghosting.

## Lernstand

Es gibt **keine bereits vorhandenen Lernstände** für diese neuen Pakete.
Die Firmware legt die folgenden Dateien bei der ersten Benutzung an:

```text
DOP-C02.state     # gesehen/Fehlerfrage je Datensatz
DOP-C02.state.id  # Fingerprint des konkreten Fragenpakets
DOP-C02.prefs     # aktivierte Themen
SAA-C03.state     # getrenntes Set, nie mit DOP-C02 vermischt
```

Beim Wechseln bleibt der Lernstand pro Paket getrennt. Bei Ersetzung eines
Pakets durch andere Fragen setzt die Firmware dessen Fortschritt anhand
des Inhalts-Fingerprints zurück. Der Fingerprint ist keine Authentifizierung.

## Formatgrenzen und Sicherheit

Das interne Format erlaubt bis zu sechs Themen und sechs Antwortoptionen
sowie maximal 12 KiB je Datensatz. Lange englische Szenarien und Erklärungen
werden über mehrere E-Ink-Seiten angezeigt. Die private JSONL kann HTML
enthalten; in der Firmware wird ausschließlich Klartext gespeichert.
Externe Bilder werden nicht heruntergeladen, sondern durch [IMAGE] markiert.

**Gekaufte Fragen nie in dieses öffentliche Repository oder GitHub Releases
hochladen.** Die Build-Werkzeuge arbeiten offline; gekaufte Inhalte bleiben
in deinem privaten Verzeichnis und auf deiner persönlichen SD-Karte.

## Abnahme

```sh
host-tests/trivia/run.sh
host-tests/ui/run.sh
bash scripts_local/trivia_aws_sim_smoke.sh
bash scripts_local/trivia_aws_sim_ux.sh   # Blättern, Glossar, Wischen ohne Abgabe
```

CI prüft die Paketauswahl, gültige/ungültige Dateinamen, isolierte Zustandsdateien,
das Binärformat sowie Touch-Ablauf und Fehlertraining mit **synthetischen**
Fragen. Der echte X4 Pro muss vor einem OTA-Release separat getestet werden.


## Physische X4-Pro-Abnahme vor Merge (offen)

**Status: nicht auf echter Hardware abgenommen.** Grüne Host-/SDL-/Firmware-CI
belegt keine elektrisch korrekte Deep-Sleep-Wiederaufnahme und keine E-Ink-Qualität.
PR #44 bleibt deshalb Draft; vor einem Merge müssen die folgenden Fälle mit
konkreter Firmware-Version, Commit, SD-Karte und Testergebnis protokolliert werden.

| Hardwarefall | Erwartetes Verhalten / Abnahmekriterium |
| --- | --- |
| OTA-Installation | Veröffentlichter Nightly-Tag zeigt exakt auf den geprüften PR-Commit; ESP32-S3/X4-Pro-Board-Marker, eingebettete Version, \`firmware.bin\` und SHA-256 stimmen überein |
| Neustart vs. Deep Sleep | Normaler Kaltstart ohne Checkpoint öffnet Trivia regulär; Aufwecken nach Power-Taste und Idle-Standby öffnet die letzte aktive AWS-Frage wieder |
| Bereits bewertete Frage | Nach Aufwecken dieselbe Erklärungsseite und Scrollposition, identischer Session-Score; kein zweites Bewerten |
| Unbewertete Einfachauswahl | Frage/Antwortkarte/Markierung bleiben über Power-Taste und Idle erhalten, ohne den Score zu erhöhen |
| Unbewertete Mehrfachauswahl | Mindestens zwei gesetzte Antwort-Bits bleiben auf derselben Antwortkarte; Abwahl nach dem Aufwachen funktioniert |
| Textnavigation und Glossar | Frage, Erklärung und lange Antworten behalten ihre Seitenposition; Glossar-Schließen ändert weder Position noch Auswahl |
| E-Ink und Touch | 11-/14-px-Text real lesbar, gepunktete Begriffe erkennbar, kein unzumutbares Ghosting nach Full-Refresh; Swipes bewerten nie |
| SD-Karte / Strom | Verhalten bei entnommener oder nicht schreibbarer SD-Karte klar erkennbar; kein fälschlich bestätigter Persistenz-Erfolg; nach erneutem Einlegen keine Beschädigung bestehender Dateien |
| Checkpointfehler | Ein defekter Slot führt zum gültigen zweiten Slot; zwei defekte Slots starten ohne alte Sitzungsantworten, vorhandener Lernfortschritt bleibt erhalten |
| Fragenpaket-Austausch | Bei gleichem Dateinamen, aber geändertem Inhalt werden weder alte Antworten noch alte Lernstände auf die neuen Fragen übertragen |
| Zwei Zertifikate | Auswahl, Domains, Lernfortschritte und aktive Sitzungen bleiben zwischen DOP-C02 und SAA-C03 getrennt |
| Lernstand-Reset | Explizite Bestätigung erforderlich; nur ausgewähltes Paket verliert \`.state\` und beide \`.session*\`; \`.dat\`, \`.topics\`, \`.prefs\` und anderes Zertifikat bleiben erhalten |
| Beenden | Verlassen über die Rundenübersicht entfernt nur den Sitzungscheckpoint; ein normales Deep-Sleep-Wakeup darf nicht als bewusstes Beenden gelten |

Die **automatisierte** Simulatorprüfung in
\`scripts_local/trivia_aws_sim_ux.sh\` verwendet ausschließlich selbst erstellte
synthetische Packs. Sie deckt neben UI-Interaktion die Wiederaufnahme einer
bewerteten Erklärung, unbewertete Mehrfachauswahl, defekte/abgeschnittene
Checkpoint-Slots, Fragenbank-Austausch sowie Reset-Isolation ab. Der SDL-Prozess
simuliert den Controller-Neustart, **nicht** die physische Versorgung, FAT-/SD-
Stromausfälle, den Standby-Taster oder E-Ink-Wellenformen.

**Abnahmeprotokoll:** Gerät und Hardware-Revision, OTA-Tag, Commit-SHA,
SD-Medium/Filesystem, Standby-Auslöser (Taste oder Idle), Testfall,
Ist-Ergebnis, Logs/Fotos und Datum erfassen. Keine privaten Frageninhalte,
SD-Images oder gekauften Prüfungsfragen in Issues/CI hochladen.
