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
```

CI prüft die Paketauswahl, gültige/ungültige Dateinamen, isolierte Zustandsdateien,
das Binärformat sowie Touch-Ablauf und Fehlertraining mit **synthetischen**
Fragen. Der echte X4 Pro muss vor einem OTA-Release separat getestet werden.
