# CardParty / CAW SD importer: QA, 2026-10-27

Branch: `feature/cardparty-sd-pack-importer-20261027`.
Ausgangspunkt: `09b55920137efa9a06cb78fec1cfed11de1d13e9`.
Dieser Bericht dokumentiert die nachfolgenden Korrekturen und ihre lokalen
Prüfungen. Commit und Push auf diesen Featurebranch sind ausdrücklich freigegeben.
Hardwarefreigabe und Mergefreigabe stehen aus.

## GitHub-Stand, über das verbundene Plugin geprüft

| Run | Revision | Tatsächliches Ergebnis |
| --- | --- | --- |
| [40593405340](https://github.com/0b-ivan/inx-4x-pro/actions/runs/40593405340) | `30d2c6e` | Erfolgreich: Python-Offlineprüfung und nativer SD-Leser. Kein Firmwarebeleg in diesem Run. |
| [40593598581](https://github.com/0b-ivan/inx-4x-pro/actions/runs/40593598581) | `09b5592` | Erfolgreich: Offline-/Leserprüfung und vollständiger x4pro-Firmware-Build. |

Bei der Eingangskontrolle gab es keine neueren Runs auf diesem Branch.
Im Firmware-Job 122503838247 fanden sich keine `error:`, `fatal error:` oder
`[FAILED]`-Einträge; vorhandene Deprecation-Warnungen sind kein fehlerfreier
Warnungsbericht. Der Run meldete 7.702.042 / 8.323.072 Bytes Flash und
129.432 / 327.680 Bytes statisches RAM.
Diese grünen Runs gelten für den Ausgangsstand. Die Korrekturen sind unten
lokal geprüft; der Push startet die isolierte CI erneut. Aktuelle Ergebnisse
stehen in den [Actions-Runs dieses Branches](https://github.com/0b-ivan/inx-4x-pro/actions?query=branch%3Afeature%2Fcardparty-sd-pack-importer-20261027).

## Reproduzierte und behobene Fehler

1. Der Paketcompiler akzeptierte Leerzeichen, Listen oder `true` als
   Rechteinhaber, Quellen- oder Lizenz-URL. Nichtobjekte und ungültiges Unicode
   führten teilweise zu ungefangenen Ausnahmen. Freigaben erfordern jetzt
   nichtleeren UTF-8-Text und HTTP(S)-URLs ohne Zugangsdaten; fehlerhafte
   Eingaben liefern einen kontrollierten Fehler, bevor die Ausgabedatei
   angefasst wird. Die Rechteprüfung selbst bleibt eine menschliche Aufgabe.
2. Der 6.144-Byte-Serverpuffer verwarf vollständige Spielstände bei acht
   Spielern und gültigen langen Karten mit JSON-Escaping. Die Größe wird nun
   aus den erlaubten Karten-, Hand-, Spieler- und Stapelgrenzen abgeleitet:
   15.756 Bytes, einschließlich 2.048 Bytes für feste JSON-Felder. Das kostet
   pro bestehendem Serverobjekt 9.612 Bytes zusätzlichen festen Speicher;
   es gibt keine zusätzliche Allokation beim Senden. Tatsächlicher freier Heap
   und Sendelatenz auf dem ESP32 müssen noch gemessen werden.
3. Die 256-Byte-Zwischenablage für `winningText` ließ lange Pick-2/3-Karten
   weg. Der Server schreibt jetzt alle gewählten Antworten direkt und korrekt
   escaped in die JSON-Zeichenkette.
4. Der E-Ink-Umbruch übersprang den Rest langer Wörter und konnte UTF-8 an
   einer Bytegrenze abschneiden. Ein isolierter Aufruf des bisherigen
   Activity-Codes zeigte: 203 Eingabebytes, 158 ausgegebene Bytes, Endmarke
   verloren. Der neue Umbruch trennt an Zeichengrenzen, setzt Wörter fort und
   zeigt bei ausgeschöpften Bildschirmzeilen drei sichtbare Punkte. Dafür
   genügt ein 160-Byte-Zeilenpuffer; die zweite bisherige 160-Byte-Kopie entfällt.
5. Die isolierte CI reagierte nur auf Änderungen ihrer eigenen Workflowdatei.
   Die Pfadfilter umfassen jetzt Importer und Tests. Der Hostrunner ist
   ausführbar, nutzt einen eigenen temporären Ordner und verträgt bereits
   erzeugte HTML-Header auf Hosts mit vorzeichenbehaftetem `char`.

Die betroffenen vorhandenen C++-Dateien wurden mit dem vorgeschriebenen
Repository-Wrapper und clang-format 21.1.8 formatiert; der Diff enthält
deshalb auch bestehende Formatierungsabweichungen.

## Tatsächliche lokale Prüfungen

| Prüfung | Ergebnis und Reichweite |
| --- | --- |
| Python-Offline-/Lokalisierungssuite | 20 Testmethoden bestanden, einschließlich CLI-Fehlerpfaden und Compiler-zu-Server-Integration. |
| CLI → `.dat` → C++-Leser → Spiel → WebSocket-JSON | Pick 1/2/3, Draw 0/1/2, acht Spieler mit maximaler Handgröße; Prompts mit 384 und Antworten mit 160 UTF-8-Bytes, Escaping und Umlaute. Je Kombination 24 unabhängig mit Python decodierte vollständige Zustände aus Playing/Judging/Reveal; Siegertext vollständig. Vor der Korrektur scheiterten alle drei Kombinationen am Nachrichtenversand. |
| Regeln, Sessions, Protokoll | 23.535 Assertions, 80 bzw. 60 Prüfungen, jeweils ohne Fehler. |
| Hostintegration | Vollständige 3-/8-Spieler-Demospiele, Reconnects, Disconnect-Fortschritt, Browseradapter und SD-Leser bestanden. Jeder abgeschnittene Paketpräfix und beide simulierten I/O-Fehler behalten ein zuvor gültiges Deck unverändert. |
| Backpressure und Stresstest | 113 bzw. 18.859 Prüfungen ohne Fehler; langsame/getrennte Clients, Ticketwechsel, Rematch und acht Browser modelliert. Kein Ersatz für Funk-/TCP-Messungen am Gerät. |
| Textumbruch | Lange Wörter, UTF-8-Zeichen, Byte- und Breitenbegrenzungen sowie sichtbare Kürzung bestanden. |
| Echter Browser | Chrome über Playwright: 320×568, 390×844 und 1280×800; Join, Lobby, Pick 3, Judging und Reveal bestanden. 15 Screenshots gespeichert; kein übersprungener Browsercheck im abschließenden Hostlauf. |
| SDL-Build | `simulator_x4_pro` erfolgreich; separater Buildordner. |
| SDL mit SD | Freigegebenes synthetisches Paket mit Umlauten und ein 383-Byte-Prompt mit langem UTF-8-Wort; fünf identische Demo-Fallbacks; sechs unterschiedliche echte Activity-Phasen. Logs und BMP/PNG-Aufnahmen gespeichert. |
| Firmware | Vollständiger x4pro-Build des geänderten Quellstands erfolgreich, anschließend nach der letzten C++-Änderung erneut erfolgreich gebaut: Flash 7.702.294 / 8.323.072 Bytes, statisches RAM 129.432 / 327.680 Bytes. Kein Upload. |
| Qualität | Repository-Formatierungswrapper und `git diff --check` bestanden. |

## Reproduktion und lokale Belege

```sh
python3 -m unittest discover -s tools_local/cardparty_l10n -p 'test_*.py' -v
NODE_PATH="$PWD/.pio/browser-tools/node_modules" \
  CARDPARTY_REQUIRE_BROWSER=1 CARDPARTY_BROWSER_CHANNEL=chrome \
  sh host-tests/cardparty/run.sh
python3 scripts_local/cardparty_sd_pack_smoke.py \
  --binary .pio/cardparty-sim-build/simulator_x4_pro/program \
  --out qa-artifacts/cardparty-sd/sdl --all-states
```

`CARDPARTY_BROWSER_CHANNEL` ist optional; ohne Angabe wird das von Playwright
installierte Chromium verwendet. Für macOS waren die installierten
mbedTLS-3-Include-/Librarypfade beim Simulator-Build nötig. Die SDL-Prüfung
benötigt einen verfügbaren Videotreiber; auf Linux kann sie unter Xvfb laufen.

Der ESP32-Builder verweigert Projektpfade mit Leerzeichen. Deshalb wurde die
Firmware unter `/private/tmp/caw-sd-fw-3u56q1w1` gebaut. Die geänderten
Firmwarequellen wurden bytegenau mit der Arbeitskopie verglichen; ihre
SHA-256-Werte liegen in `qa-artifacts/cardparty-sd/firmware-source-sha256.json`.
Firmware und Simulator haben getrennte Buildordner. Wiederholte Builds nach
reinen Dokumentationsänderungen sind nicht erforderlich.

Die lokalen, ignorierten Belege stehen unter `qa-artifacts/cardparty-sd/`:
`compiler-before.log`, `pipeline-before.log`, `wrap-before.log`,
`python-tests.log`, `host-tests.log`, `firmware-build.log`,
`simulator-build.log`, `sd-activity-smoke.log` und `sdl/`.
Browseraufnahmen: `.pio/cardparty-browser-viewports/`.
Diese Dateien und synthetischen Testpakete gehören nicht in einen Release.

## Offene Freigaben und Grenzen

Noch erforderlich sind ein physischer X4-Pro-Test mit echtem SD-Lesen,
Heap-/PSRAM-Messungen, langen Texten und WLAN-Sendelatenz bei 3–8 Telefonen,
Hotspot/Existing-Wi-Fi, Reconnects, Sessionende und Stromunterbrechung.
Die SDL-Seats und Host-Netzwerkklassen sind Testmodelle. Der größere
Nachrichtenpuffer beweist insbesondere keine nichtblockierenden langen
TCP-Sends auf dem ESP32.

Die Freigabefelder und der Release-Flag sind weiterhin keine Signatur.
CRC prüft Datenkorruption, nicht Herkunft oder Rechte. Netzwerkdownload,
signierter Katalog und atomare Aktivierung sind weiterhin nicht implementiert.

In dieser Arbeit wurden weder PR #47 noch CrossPlay-/Nightly-Steuerung
geändert, keine fremden Karten importiert/veröffentlicht und kein OTA oder
Merge ausgeführt. Commit und Push auf den bestehenden Featurebranch wurden
ausdrücklich freigegeben. Ein Merge erfordert weiterhin eine gesonderte Freigabe.
