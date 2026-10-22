# VARAGH → inx-4x-pro: Migrationsplan

Stand: 2026-10-21. Quelle: [PewPewzxc/VARAGH](https://github.com/PewPewzxc/VARAGH)
@ `9733c56` (v1.3.0 "Lion"), Ziel: dieses Repo @ `98e3470` (v1.14.3).

## Ausgangslage

| | inx-4x-pro (CrossPlay-Fork) | VARAGH |
|---|---|---|
| Abstammung | CrossPoint → CrossPlay → hier | CrossPoint → CrossInk → VARAGH |
| Zielgerät | X4 Pro (ESP32-S3, 8 MB PSRAM) | X4 Pro, X4 Classic, X3 |
| App-UI | FreeInkUI + Toybox (`src/apps_local/`) | eigenes `GameUi.h` (`src/activities/apps/`) |
| App-Registrierung | `src/apps_local/Shelf.cpp` (Games/Apps-Ordner) | `AppsActivity` mit Manage-Liste |
| i18n der Apps | `toybox::localize()` (EN/DE-Katalog) | Core-`tr()` mit EN/DE/FA |
| Lizenz | MIT | MIT |

Beide Lizenzen sind MIT: Übernommener Code bekommt einen Herkunftsvermerk
(Kopfkommentar und Eintrag in `THIRD-PARTY-NOTICES.md`).

**Was die Portierung leicht macht:** VARAGH trennt die Spiellogik
(`src/games/*Game.{h,cpp}`, nur `<cstdint>`/`<cstring>`/`<algorithm>`) streng von der
UI und testet sie mit eigenen Unit-Tests (`test/wordle_game`, `test/go_game`,
`test/calendar_math`, …). Die Logik wird **1:1 kopiert**, die Activity wird im
Toybox-Stil **neu geschrieben**. VARAGHs `GameUi.h`/`AppArt.h` werden nicht übernommen.

**Was nicht übernommen wird:** Reader-Kern-Änderungen von CrossInk (Library,
Bild-Pipeline, Section-Cache-Format v78). Sie kollidieren mit jedem Upstream-Merge
von CrossPoint, den `scripts_local/sync.sh` fährt.

---

## Phase 0: Bestehende Fehler beheben (vor jeder Migration)

Aus dem Simulator-Test vom 2026-10-21 (Build `simulator_x4_pro`, `check.sh --tests`
grün, alle 27 Apps geöffnet, Deutsch und Englisch).

### Tarot (höchste Priorität)

| # | Befund | Ort | Fix |
|---|---|---|---|
| T1 | **Download ist kaputt (404).** Commit `1bc14a0` hat `tarot/` aus `main` gelöscht, der Downloader lädt aber von `raw.githubusercontent.com/0b-ivan/inx-4x-pro/main/tarot/…`. Auf frischen Geräten lässt sich Tarot nie installieren. | `TarotDownloadActivity.cpp:13-14` | URL auf den Commit `0d44aa24e8f1…` pinnen (dort liefern alle 157 Dateien 200), oder Assets als Release-Asset hosten. |
| T2 | **Karten stehen auf dem Kopf.** Die BMPs sind links-rechts gespiegelt gespeichert, der Code zeichnet sie mit `flipVertical=true`, das ergibt in Summe 180°. Auf dem Gerät per Foto bestätigt. | `TarotActivity.cpp:83`, `SleepActivity.cpp:585` | Horizontal statt vertikal spiegeln: `flipHorizontal` in `GfxRenderer::drawBitmap` ergänzen (kleiner Eingriff), oder die Assets einmal korrigiert neu erzeugen und hosten (dann reicht `flip=false`). |
| T3 | Die Bedeutung wird **einzeilig** mit `drawCenteredText` gezeichnet: Sie läuft über den Bildschirmrand (ca. 2000 `Outside range`-Logs pro Ansicht) und liegt ohne Hintergrund mitten im Kartenbild. | `TarotActivity.cpp:86-88, 104-106` | Umbrechender Textblock (Toybox `target.text` mit `maxLines`) auf einem weißen Panel. |
| T4 | Die Hilfezeilen „Tap left: meaning / Tap right: history" und (auf Deutsch) der Start-Hinweis sind zu breit und werden beidseitig abgeschnitten. | `TarotActivity.cpp:93, 108` | Umbrechen oder kürzen. |
| T5 | **Umlaute fehlen**: Deutsche `tr()`-Texte werden mit der ASCII-Schrift von Toybox gezeichnet („Zurck", „Vollendng"). | Tarot-Texte, Schrift `kUiFontId` | Für Fließtext die Core-UI-Schrift (`UI_10_FONT_ID`) nutzen, oder die Texte in ASCII-Umschrift („ZURUECK") wie im restlichen Toybox-Katalog. |
| T6 | Back im WLAN-Dialog bricht ab **und** verlässt Tarot. Der Upstream-Dialog reagiert auf `wasPressed`, Tarot auf das anschließende `wasReleased`. | `TarotActivity.cpp:36-44` | Nach der Rückkehr aus einer Sub-Activity Eingaben ignorieren, bis alle Tasten losgelassen sind (VARAGH hat dafür `src/util/InputReleaseGuard.h` mit Test). |
| T7 | Ohne Assets zieht `RIGHT` trotzdem Karten („Unknown" / „No meaning found."), nur Tippen startet den Download. Der Startbildschirm sagt nicht, dass ein Download nötig ist. | `TarotActivity.cpp:27-71` | Ohne Assets einen eigenen Zustand „Deck herunterladen (≈42 MB)" anzeigen und alle Eingaben dorthin leiten. |
| T8 | Der Verlauf zeigt „Karte 31" statt Kartennamen. Die Überschrift klebt an der ersten Zeile. Kartennamen sind auf Deutsch nicht übersetzt. | `TarotActivity.cpp:114-121` | Namen aus `meanings.json` zeigen, Abstand korrigieren, DE-Namen ergänzen. |
| T9 | Der Downloader greift direkt auf `SdMan` zu (am `storageMutex` vorbei), und das aus einem eigenen Task. Laut `AGENTS.md` kann das das Gerät mit einer Panic (`xTaskPriorityDisinherit`) abstürzen lassen. | `TarotDownloadActivity.cpp:69-108` | Auf `Storage`/`HalFile` umstellen. |
| T10 | `cancel_` ist ein einfaches `bool`, wird aber von zwei Tasks gelesen und geschrieben. `onExit` löscht nach 1 s hart per `vTaskDelete` einen Task, der eventuell noch HTTP/SD hält. `loop()` fordert in jeder Iteration ein Neuzeichnen an (Flackern, Akku). | `TarotDownloadActivity.*` | `std::atomic<bool>`. Kooperativ beenden statt hart abbrechen. Nur bei Fortschrittsänderung neu zeichnen. |
| T11 | Der Downloader prüft die `sha256` aus dem Manifest nicht, nur die Größe. Er lädt außerdem 78 Thumbnails, die nirgends benutzt werden (`thumbPath` ist toter Code). | `TarotDownloadActivity.cpp`, `TarotAssets.cpp:34` | SHA prüfen (mbedTLS ist vorhanden). Thumbs nicht laden oder für den Verlauf nutzen. |
| T12 | Im Sleep-Menü stehen „Tarot" und „Taro" (ein Branding-Logo) direkt nebeneinander, das ist verwirrend. Tarot nutzt dasselbe Icon wie „Games". | `SettingsList.h:212-213`, `Shelf.cpp:66` | „Taro" umbenennen. Ein eigenes Icon. |

### Übersetzungen

| # | Befund | Ort | Fix |
|---|---|---|---|
| I1 | **Jeder Text der Form „X / Y" wird auf Deutsch zu „X PUNKTE"** (z. B. der Authenticator-Zähler „0 / 16"). `consumed` wird zwischen den `sscanf`-Versuchen nicht zurückgesetzt, ein alter Wert lässt `"%d POINTS%n"` fälschlich passen. Per Harness reproduziert. | `src/apps_local/ui/LocalI18n.h` ab ca. Zeile 1340 | `consumed = 0` vor jedem `sscanf`, plus ein Host-Test mit Negativfällen. |
| I2 | „STUDY" → **„ARBEITSZIMMER"** (das Zimmer, nicht Lernen). | `LocalI18n.h:1258` | „LERNEN" oder „KARTEIKARTEN". |
| I3 | Deutsch fehlen 31 Core-Keys, u. a. das Frontlight-Panel (`STR_FRONTLIGHT`, `STR_BRIGHTNESS`, `STR_WARMTH`, `STR_RESTORE_LIGHT_ON_WAKE`), `STR_TAP_FOR_READER_MENU`, der komplette Developer-Mode-Block (10 Keys) und die OPDS-Filter. | `lib/I18n/translations/german.yaml` | Ergänzen. |
| I4 | Authenticator und RSS sind auf Deutsch komplett englisch, weil ihre Strings nicht im `localize()`-Katalog stehen. | `src/apps_local/totp/`, RSS | Katalog ergänzen. |
| I5 | Andere Sprachen: Bei ~25 Sprachen fehlen 75–220 Keys. In `hebrew`/`polish` liegen je 8 tote Keys, die es im Englischen nicht mehr gibt. In `russian.yaml` fehlt bei `STR_PAGE_TOTAL_OVERALL_FORMAT` ein `%.2f`. | `lib/I18n/translations/*.yaml` | Tote Keys entfernen, Format korrigieren. Fehlende Keys fallen bereits sauber auf Englisch zurück. |
| I6 | Darstellung: „Power-Taste lange drücken: Ausschalten" stößt ohne Abstand an den Wert „An". | Settings → Bedienung | Kürzen. |

### Weitere Fehler

| # | Befund | Ort | Fix |
|---|---|---|---|
| B1 | Authenticator ohne Konten zeigt „NO ACCOUNTSAdd a TOTP…": Das `\n` kann die Schrift nicht zeichnen. Dasselbe Problem steckt in den Texten für „Tresor gesperrt" und „PIN einrichten". | `TotpActivity.cpp:654, 667, 696` | Überschrift und Text getrennt zeichnen. |
| B2 | **Back im RSS-Reader springt zwei Ebenen zurück** (RSS → Apps → Home): Die Activity reagiert auf `wasPressed`, das Loslassen landet in der Apps-Liste. Confirm hat dasselbe Muster. | `src/activities/settings/RssFeedListActivity.cpp:29, 38` | `wasReleased`. |
| B3 | Fork-Branding: README, Badges, Release-Links und der Study-QR-Code (`crossplay.ma-r-s.com/study`) zeigen auf das Original. `check.sh` erwartet den Branch `xteink`, hier heißt er `main`. Der Web-Emulator unter `site/emulator/` ist veraltet. | diverse | Auf das eigene Repo umstellen. `DEPLOY_BRANCH=main`. |
| B4 | Das Authenticator-Icon ist dasselbe wie bei Study, das RSS-Icon dasselbe wie bei Hacker News. | `Shelf.cpp` | Eigene Icons. |

---

## Phase 1: Spiele (wenig Risiko, schnell sichtbar)

Pro Spiel: Logik kopieren, Host-Test aus VARAGH nach `host-tests/<spiel>/`
portieren, Activity im Toybox-Stil neu, Eintrag in `Shelf.cpp`, deutsche Strings
in `LocalI18n.h`, Doku `docs/apps/<spiel>.md`. Vor der UI drei Varianten bauen
und im Simulator vergleichen (siehe `AGENTS.md`, „Three variants").

| Spiel | VARAGH-Quelle | Logik | Aufwand | Anmerkung |
|---|---|---|---|---|
| **Wordle** | `src/games/WordleGame.*`, `WordleWords.h` (1577 Zeilen Wortliste) | 364 Z. | M | Englische Wortliste (~1900 Lösungen, 22 000 Ratewörter). Eine deutsche Liste wäre ein eigenes Paket. Die Wortliste liegt als `constexpr` im Flash. |
| **Go** 9×9/13×13/19×19 | `src/games/GoGame.*` | 480 Z. | M–L | Die KI (`GameThinker`) braucht einen eigenen Task mit Zeitlimit. Mit dem Muster aus der Chess-App hier abgleichen. 19×19 auf 480 px Breite = 24 px pro Feld, das ist knapp, aber machbar. |
| **Tic Tac Toe** (klassisch und „infinite") | `src/games/TicTacToeGame.*` | 195 Z. | S | Guter Einstieg, um das Portierungsmuster einzuüben. |

Chess, Checkers und Sudoku gibt es hier schon. **Nicht** übernehmen, höchstens
einzelne Ideen (z. B. Sudoku-„Fill"/Notizen) vergleichen.

Akzeptanz: `check.sh` grün, neuer Host-Test grün, `sim-shot` ohne `No glyph`
und ohne `Outside range`, auf dem Gerät einmal gespielt.

## Phase 2: Uhr und Kalender

| Feature | VARAGH-Quelle | Aufwand | Anmerkung |
|---|---|---|---|
| Uhr-App (Ziffern, analog, Kalender) | `ClockActivity.*`, `ClockFace.*`, Test `test/calendar_math` | M | Neu zeichnen einmal pro Minute mit Teil-Refresh. Den persischen Kalender weglassen. |
| Uhr als Sleep-Screen | `SleepActivity.cpp` (Modus „Clock & Calendar") | S | Neuer `SLEEP_SCREEN_MODE` hinter `TARO`. Der Index wird in `settings.json` gespeichert, also hinten anhängen. |
| Sommerzeit | Test `test/daylight_saving` | S | Prüfen, ob „Sync Clock" hier schon eine Zeitzone kennt. |

## Phase 3: Komfort im Reader und in den Dateien (mittleres Risiko)

| Feature | VARAGH-Quelle | Aufwand | Risiko |
|---|---|---|---|
| Buchbewertung (5 Sterne, Halbsterne, `/.crosspoint/ratings.txt`) | `BookRatingActivity.*`, Test `book_rating_text` | M | Berührt `EndOfBookOptions` und die Buch-Menüs. Eine reine Textdatei ist upstream-neutral. |
| Swipe zum Löschen (Dateibrowser, Zuletzt gelesen) | FreeInk-SDK-Hit-Test (VARAGH-Patch) + Listen-Activities | M | Ändert `FileBrowserActivity`/`RecentBooksActivity`, das sind Upstream-Dateien und erzeugen Merge-Konflikte. Besser als Erweiterung von `UiListActivity` lösen. |
| `InputReleaseGuard` | `src/util/InputReleaseGuard.h`, Test | S | **Vorziehen nach Phase 0**: behebt T6 und schützt alle neuen Apps. |
| Nachtlicht-Zeitplan | Test `frontlight_schedule`, `FrontlightPanelActivity` | M | Ein Frontlight-Panel gibt es hier schon. Nur den Zeitplan ergänzen. |
| Control Center (Wisch von oben, 8 belegbare Knöpfe) | `FrontlightPanelActivity`, `QuickActions.*` | L | Kollidiert eventuell mit den Touch-Gesten der Spiele. Erst nach einer Gesten-Inventur. |

## Phase 4: Lernen und Wörterbuch (hoher Aufwand, optional)

| Feature | Aufwand | Anmerkung |
|---|---|---|
| Deutsch→Englisch-Wörterbuch mit Grundform-Suche (ging → gehen) | L | `german_stemmer` und `word_forms` sind reine Logik und gut testbar. Das Wörterbuch-Format mit `src/util/Dictionary*.h` hier abgleichen. |
| Tippen zum Nachschlagen / Langdruck-Menü / Auswahl-Griffe | XL | Tiefer Eingriff in `EpubReaderActivity`. Nur falls das Lesen mit Wörterbuch Hauptzweck wird. |
| Highlights-Hub und „Hinzufügen zu …" | L | Für Lernkarten **Study** (Anki/FSRS) nutzen statt VARAGHs Leitner-Flashcards: „Zu Study-Deck hinzufügen" aus dem Wörterbuch. |
| Online-Wörterbuch (Wiktionary, Datamuse, OpenThesaurus) | L | WLAN-Neustart-Logik, Timeouts. Erst nach den Offline-Wörterbüchern. |

**Nicht geplant:** persische/arabische Schrift und Rechts-nach-links-Darstellung (NotoVazir),
die Bild-Pipeline von CrossInk, X3/X4-Classic-Builds und Faster Screen Link
(SD/SPI-Takt, Hardware-Risiko).

---

## Arbeitsweise

1. Ein Branch pro Feature (`feature/varagh-wordle`, …), ein PR pro Feature.
2. Vor jedem Commit: `./scripts_local/check.sh` (Host-Tests plus Builds) und
   `sim-shot.sh` mit Screens auf Englisch **und** Deutsch.
3. Herkunft: Kopfkommentar `// Ported from VARAGH (MIT), src/games/WordleGame.cpp @ 9733c56`
   und ein Eintrag in `THIRD-PARTY-NOTICES.md`.
4. Hardware-Abnahme: auf dem X4 Pro flashen (`./scripts_local/wifi-flash.sh`), vor allem
   für Orientierung, Refresh-Verhalten und Akku.

## Reihenfolge

```
Phase 0  Tarot T1–T12, I1–I6, B1–B4   ──►  v1.14.4 (Bugfix-Release)
         + InputReleaseGuard
Phase 1  Tic Tac Toe ─► Wordle ─► Go  ──►  v1.15.0
Phase 2  Uhr und Kalender, Sleep-Uhr  ──►  v1.16.0
Phase 3  Bewertung, Swipe-Löschen,
         Nachtlicht                   ──►  v1.17.0
Phase 4  nach Bedarf
```
