# Cards Against Whatever – Konzept Milestone 2

Stand 2026-10-28, Basis `120c6e3`. Konzept, noch nichts davon gebaut.

Grundregel: **Der X4 ist König.** Alles, was den Spielverlauf für alle ändert
(Regeln, Rauswerfen, Ende, Deck), entscheidet nur der X4. Handys dürfen
vorschlagen (Pause, „Nochmal“-Stimme, Strike), nie bestimmen. Das hält das
heutige Sicherheitsmodell: Handys senden nur authentifizierte, eng begrenzte
Nachrichten (`CardPartyProtocol.h`).

## 1. Begriffe

| Begriff | Bedeutung |
|---|---|
| Host | der X4 |
| Juror | heute „Card Czar“; wählt die Siegerantwort. Intern bleibt `czar_`, nur die Anzeige ändert sich |
| Spieler-ID | `Name·Wort`, z. B. `Ivan·Toast`, dauerhaft pro Handy |
| blockiert | Spiel wartet auf einen Spieler, der gerade gebraucht wird und weg ist |
| pausiert | jemand hat Pause gedrückt |

## 2. Regeln vor dem Start (X4: neuer Punkt `[05] REGELN`)

| Regel | Werte | Vorgabe | Im Spiel änderbar |
|---|---|---|---|
| Runden | 3 … 30 | 10 | nein |
| Antwort-Timer | aus, 30, 45, 60, 90, 120 s | aus | ja, ab nächster Runde |
| Verlängerung | aus, 10, 15, 20, 30 s | 15 s | ja |
| Juror-Timer | aus, 30, 60, 90 s | aus | ja (nur X4) |
| Automodus | aus, 5, 8, 12 s nach der Auflösung | aus | ja, sofort |
| Rando Cardrissian | an, aus | an | ja, ab nächster Runde |

`Rules` (`CardPartyCore.h:20`) hat `rounds` und `rando` schon; heute fest auf
10 und an. Neu dazu kommen die Zeiten. Die Auswahl wird wie die Kategorien
auf SD gespeichert (`/games/cardparty/rules.bin`).

## 3. Ablauf

Die Phasen bleiben `Lobby → Playing → Judging → Reveal → … → Finished`.
**Pause** und **blockiert** sind keine Phasen, sondern Zustände darüber:
Während sie gelten, steht jede Uhr still und keine Spielaktion wird
angenommen. Die Phase bleibt, wie sie war.

### 3.1 Antwort-Timer und Verlängerung

- Start mit der Runde. Der Server führt die Frist (`millis()`), die Handys
  bekommen die Restzeit und zählen bis zum nächsten Frame selbst herunter.
- Ablauf, noch Antworten offen, Verlängerung nicht verbraucht: einmalig
  **+Verlängerung**, alle sehen „+15 s“.
- Danach Ablauf: Einsendungen schließen (`closeSubmissions`). Ohne eine
  einzige menschliche Antwort: Runde ohne Punkte abbrechen (`abortRound`).
- X4-Knopf **+ZEIT**: jederzeit in `Playing`, addiert die eingestellte
  Verlängerung (bei „aus“: 15 s). Unbegrenzt oft, der X4 ist König.

### 3.2 Juror-Timer (nur wenn am X4 eingeschaltet)

Läuft in `Judging`. Bei Ablauf wählt der Server zufällig eine Antwort, der
Sieger bekommt den Punkt, die Auflösung zeigt „Juror hat geschlafen“.
**Offene Frage A** (Alternative: Runde ohne Punkte abbrechen).

### 3.3 Automodus

Nach `Reveal` wartet der X4 die eingestellte Zeit und ruft `nextRound()`.
Pause und Blockade halten den Countdown an. Nach der letzten Runde kein
Automatismus: Die Siegerehrung bleibt stehen, bis der X4 entscheidet.

### 3.4 Verbindung weg → blockiert (15 s)

- **Gebraucht** wird ein Spieler, wenn er Juror in `Judging` ist oder in
  `Playing` noch nicht eingereicht hat (und nicht Juror ist).
- Ist ein gebrauchter Spieler 15 s offline: **blockiert**. Alle Handys zeigen
  „Warte auf Ivan·Toast …“ und sind gesperrt, der X4 zeigt dasselbe mit zwei
  Knöpfen:
  - **OHNE IHN WEITER**: Einreicher → sein Zug fällt weg (`releaseTurn`).
    Juror → Runde ohne Punkte abbrechen, nächster Juror.
  - **RAUSWERFEN**: siehe 3.6.
- Kommt der Spieler zurück, löst sich die Blockade von selbst.
- Nicht gebrauchte Spieler blockieren nie. Der X4 und alle Handys sehen
  trotzdem jederzeit, wer offline ist (Markierung in der Spielerliste).
- Heute gibt der Server einen gehaltenen Zug nach 30 s still frei
  (`kTurnGraceMs`, `CardPartyBrowserServer.h:72`). Das entfällt: Nach 15 s
  wird blockiert, und der X4 entscheidet.
- **Offene Frage B:** Soll der Automodus eine Blockade nach z. B. 60 s selbst
  mit „ohne ihn weiter“ lösen? Vorschlag: ja, nur im Automodus.

### 3.5 Pause

- Jeder Spieler und der X4 kann pausieren. Es gibt nur eine Pause zur selben
  Zeit; wer pausiert hat, steht bei allen auf dem Schirm.
- Aufheben dürfen **der Pausierende und der X4**, sonst niemand.
- Geht der Pausierende offline, kann nur noch der X4 aufheben.

### 3.6 Rauswerfen und Spielende

- **Rauswerfen** (X4, Spielerliste): Platz wird frei, sein Ticket ungültig,
  sein Handy zeigt „Du wurdest aus der Party entfernt“. Kein Bann: Er kann
  beim nächsten Spiel (Lobby nach Rematch) wieder beitreten. War er Juror der
  laufenden Runde: Runde ohne Punkte abbrechen. Hatte er eingereicht: seine
  Antwort fliegt aus dem Stapel.
- Fallen dadurch weniger als 3 verbundene Spieler übrig: Spiel endet regulär
  mit Siegerehrung.
- **Spiel beenden** (X4, zweimal tippen): sofort Siegerehrung mit dem
  aktuellen Stand, alle Handys zeigen „Spiel vom Host beendet“.

### 3.7 Siegerehrung

- Nach der letzten Runde oder beim Beenden: Rangliste, über dem Namen des
  Siegers ein **Krönchen**; bei Gleichstand bekommen alle Punktgleichen eins.
- Handys: Knopf **„Nochmal!“** (Stimme). Der X4 zeigt „3/5 wollen nochmal“.
- X4: **NOCHMAL** (heute `[ REMATCH ]`) oder **BEENDEN**. Nur der X4 entscheidet.

## 4. Deck und Kategorien im Spiel

- Grundlage: Handkarten-IDs zeigen ins **ganze Pack**, nicht in eine
  gefilterte Kopie. Heute baut `FilteredDeck` eine Kopie mit neuen Indizes
  (`CardPartyCategories.h`). Das wird umgestellt: Das Spiel bekommt das ganze
  Pack plus eine Maske und filtert erst beim Ziehen. Nur so lassen sich
  Kategorien mitten im Spiel schalten, ohne dass Handkarten ihre Bedeutung
  verlieren, und nur so sind Strikes (Abschnitt 7) eindeutig.
- **Kategorien** (X4, Spielmenü): wirken ab dem nächsten Ziehen auf Antworten
  und ab der nächsten Runde auf Sätze. Handkarten bleiben, bis sie gespielt
  sind.
- **Pack wechseln** (X4, nur in `Reveal` und Lobby): alle Hände werden neu
  ausgeteilt, Punkte bleiben. Kurz liegen zwei Packs im RAM (siehe T14).
- Reichen die Karten nach dem Schalten nicht mehr für den Tisch, verweigert
  der X4 den Schalter mit Begründung, wie heute im Setup.

## 5. Bedienung am X4

- **Setup:** `[01]` Hotspot, `[02]` Wi-Fi, `[03]` Deck, `[04]` Kategorien,
  `[05]` Regeln, dazu Wall of Fame.
- **Im Spiel:** unten zwei Knöpfe: links die heutige Hauptaktion (Einsendungen
  schließen, nächste Runde …), rechts **MENÜ**. Das Spielmenü: Pause /
  Fortsetzen, +Zeit, Automodus, Rando, Juror-Timer, Kategorien, Deck wechseln,
  Spieler (Status, Rauswerfen), Wall of Fame, Spiel beenden.
- Timer auf dem X4 oben rechts. E-ink kann nicht jede Sekunde neu zeichnen,
  ohne zu flackern: Der X4 zeigt die Restzeit in 10-s-Schritten, die Handys
  sekundengenau.
- Neue X4-Bildschirme (Regeln, Spielmenü, Siegerehrung) entstehen nach der
  Projektregel: drei Varianten im Simulator rendern, nebeneinanderlegen, die
  beste bauen.

## 6. Darstellung

- **Antwort im Satz:** Auf dem Handy wird die gewählte Karte schon vor dem
  Absenden in die Lücke eingesetzt, hervorgehoben (fett, unterstrichen). Beim
  Juror erscheint jede Einsendung als ganzer Satz. In der Auflösung steht der
  Siegersatz hervorgehoben, auf Handys und auf dem X4. Sätze ohne Lücke: die
  Antwort steht dahinter. Pick 2/3 füllt die Lücken in Reihenfolge.
- X4: Dafür brauche ich einen Umbruch, der innerhalb eines Satzes zwischen
  normal und fett wechselt (`drawText` hat einen Stil-Parameter,
  `GfxRenderer.h:274`). Heute bricht `drawWrapped` nur in einer Schrift um.
- **Juror** statt Czar in allen Texten.
- **Deutsch:** X4-Texte über `tr()` mit `STR_CAW_*` in `english.yaml` und
  `german.yaml`. Die Browser-Seite bekommt eine eigene Texttabelle de/en. Die
  Sprache kommt aus der Systemeinstellung des X4 (`I18N.getLanguage()`, wie
  Trivia) und wird im Welcome-Frame mitgeschickt. Kartentexte sind davon
  unberührt.

## 7. Dauerhaft auf SD

Geschrieben wird nur zwischen Runden und am Spielende, nie im WebSocket-Takt.

### 7.1 Spieler-IDs

- Beim ersten Beitritt vergibt der X4 eine zufällige ID (64 Bit), das Handy
  speichert sie (`localStorage`) und schickt sie bei jedem Beitritt mit.
- Angezeigt wird `Name·Wort`. Das Wort kommt aus einer festen Liste von etwa
  64 kurzen, harmlosen Wörtern (de und en), bleibt pro ID gleich und ist am
  Tisch eindeutig.
- `/games/cardparty/players.bin`: bis 64 Einträge (ID, Name, Wort, zuletzt
  gesehen); der älteste fällt raus.
- Grenze: Die ID ist kein Passwort. Wer sie aus dem Browser kopiert, kann sich
  in der Wall of Fame als jemand anderes ausgeben. Für eine Party reicht das.

### 7.2 Wall of Fame

- Pro Spieler-ID: gespielte Spiele, Spielsiege, Rundensiege.
- Die letzten 20 Siegersätze als Text (Satz mit eingesetzter Antwort, auf
  200 Byte gekürzt, mit Name·Wort und Datum).
- `/games/cardparty/fame.bin`, etwa 6 KB.
- Abrufbar jederzeit: am X4 (Setup und Spielmenü) und auf jedem Handy über
  einen Knopf. Das Handy holt sie mit einer eigenen Nachricht, in Seiten zu
  10 Einträgen, damit kein Frame das heutige Budget sprengt.

### 7.3 Strikes („passt nicht“)

- Spieler markieren eine **Karte in der eigenen Hand** für den aktuellen Satz.
  Das Handy zeigt „markiert (1/3)“.
- Gezählt wird pro Paar (Pack-CRC, Satz, Antwort), je Spieler-ID nur einmal,
  über Spiele hinweg. Bei **3 verschiedenen Spielern** ist das Paar gesperrt.
- Gesperrt heißt: Wird dieser Satz gezogen, wird jede gesperrte Karte auf
  einer Hand still gegen eine neue getauscht; Rando zieht sie dafür nicht.
- `/games/cardparty/strikes.bin`: pro Paar 14 Byte (CRC, Satz, Antwort, bis
  zu 3 ID-Kürzel); höchstens 2048 Paare (28 KB, PSRAM). Ist die Datei voll,
  fallen Paare mit nur einem Strike zuerst raus.

## 8. Protokoll

Neu vom Handy (alle mit Ticket):

| Nachricht | Wirkung |
|---|---|
| `join` + `pid` | Beitritt mit gespeicherter Spieler-ID |
| `pause` / `unpause` | Pause setzen bzw. aufheben (nur der eigene) |
| `strike` + Handplatz | Strike für Karte × aktuellen Satz |
| `again` | Stimme für „Nochmal“ |
| `fame` + Seite | Wall of Fame anfordern |

Neu im Status-Frame: Regeln, Restzeit, Verlängerung verbraucht, Pause (wer),
Blockade (auf wen), Offline-Liste, Anzeigenamen, Nochmal-Stimmen, Grund bei
Ende („vom Host beendet“, „rausgeworfen“). Geschätzt +250 Byte pro Frame;
heute maximal etwa 2,2 KB. Bleibt unter `kMessageBytes` und schont den
Back-Pressure-Pfad (T8).

## 9. Speicher

- RAM neu: Regeln, Timer, Pause und Blockade < 100 Byte; Strike-Tabelle des
  aktiven Packs bis 28 KB (PSRAM, beim Laden des Packs eingelesen);
  Spieler-Registry etwa 3 KB; Wall of Fame wird nur zum Anzeigen gelesen.
- Kein neuer FreeRTOS-Task: Timer laufen über `millis()` im bestehenden
  `loop()`.

## 10. Tests

- **Kern** (Host-Tests): Timer mit Verlängerung, Juror-Timer, Automodus,
  Pause mit Rechten, Blockade nach 15 s inklusive Rückkehr, Rauswerfen in
  jeder Phase, Spielende, Kategorien mitten im Spiel, Strikes mit 3
  verschiedenen IDs und Kartentausch.
- **Stresstest:** zusätzlich Pause- und Strike-Spam, Rauswerfen während eines
  Reload-Sturms.
- **Browser:** Viewport-Tests für die neuen Overlays (Pause, blockiert,
  rausgeworfen, Siegerehrung, Wall of Fame) bei 320, 390 und 1280 px.
- **Simulator:** Screenshots aller neuen X4-Bildschirme; Hardware-Abnahme
  bekommt T16 bis T20.

## 11. Reihenfolge

Jeder Block ist ein eigener Commit mit Tests, auf Wunsch mit Nightly.

1. **Spielablauf:** Regeln-Bildschirm, Timer und Verlängerung, +Zeit,
   Juror-Timer, Automodus, Pause, Blockade, Rauswerfen, Spielende,
   Siegerehrung mit Krönchen und Nochmal-Stimme, Spielmenü am X4, Pack-Filter
   auf ganze Pack-IDs umstellen, Kategorien und Pack im Spiel.
2. **Darstellung:** Antwort im Satz hervorgehoben (Handy und X4), Juror,
   Deutsch über `tr()` und Browser-Texttabelle.
3. **Dauerhaft:** Spieler-IDs, Wall of Fame, Strikes.

## 12. Offene Fragen

- **A:** Juror-Timer abgelaufen → zufällige Antwort gewinnt (Vorschlag) oder
  Runde ohne Punkte?
- **B:** Soll der Automodus eine Blockade nach 60 s selbst lösen (Vorschlag:
  ja)?
- **C:** Rausgeworfene Spieler: Beitritt zum nächsten Spiel erlaubt
  (Vorschlag) oder bis zum Verlassen der App gesperrt?
