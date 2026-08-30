# Xteink X4 Pro: Flashing, Boot und Recovery verstehen

Diese Dokumentation erklärt das geplante Vorgehen für den Inx-X4-Pro-Port **ohne vorausgesetztes Wissen über Mikrocontroller oder Firmware**.

> [!WARNING]
> Der aktuelle Alpha-Stand ist noch **nicht zum Flashen freigegeben**. Die ersten Schritte in dieser Anleitung sind absichtlich nur lesend. Der erste Schreibvorgang wird erst freigegeben, nachdem die echte Partitionierung eines X4 Pro ausgelesen, vollständig gesichert und der Recovery-Weg validiert wurde.

## 1. Was ist auf dem X4 Pro eigentlich gespeichert?

Der X4 Pro besitzt einen ESP32-S3-Mikrocontroller und 16 MiB Flash-Speicher.

Dieser Flash ist vergleichbar mit einer sehr kleinen SSD. Darauf liegt nicht nur "das Betriebssystem", sondern mehrere getrennte Bereiche mit unterschiedlichen Aufgaben.

Vereinfacht:

```text
16 MiB Flash

+--------------------------------------------------+
| Bootloader                                       |
+--------------------------------------------------+
| Partitionstabelle                                |
+--------------------------------------------------+
| NVS / Geräteeinstellungen / Kalibrierungsdaten   |
+--------------------------------------------------+
| OTA-Metadaten                                    |
+--------------------------------------------------+
| App-Slot A                                       |
+--------------------------------------------------+
| App-Slot B                                       |
+--------------------------------------------------+
| weitere Datenbereiche                            |
+--------------------------------------------------+
```

Die tatsächlichen Adressen und Größen werden **nicht geraten**, sondern vom realen Gerät ausgelesen.

## 2. Was ist ein Mikrocontroller?

Der ESP32-S3 ist der Hauptprozessor des Readers.

Er enthält unter anderem:

- CPU-Kerne;
- RAM;
- USB-Unterstützung;
- SPI/I2C/SDMMC-Schnittstellen;
- WLAN;
- Deep-Sleep-Funktionen;
- Zugriff auf den externen Flash-Speicher.

Beim Einschalten beginnt der ESP32-S3 nicht direkt mit Inx. Er durchläuft eine Boot-Kette.

## 3. Die Boot-Kette

Sehr vereinfacht passiert beim Einschalten Folgendes:

```text
ESP32-S3 ROM
    |
    v
Bootloader
    |
    v
Partitionstabelle lesen
    |
    v
OTA-Metadaten lesen
    |
    v
gewählten App-Slot starten
    |
    v
Inx / Original-Firmware / andere Firmware
```

### ROM-Bootcode

Ein kleiner Teil des Bootcodes ist direkt im Chip eingebaut. Er kann nicht wie normaler Flash überschrieben werden.

Er ist unter anderem dafür zuständig, den eigentlichen Bootloader aus dem externen Flash zu laden.

### Bootloader

Der Bootloader ist ein kleines Programm, das vor der eigentlichen Reader-Firmware läuft.

Er entscheidet anhand der Partitionierung und OTA-Metadaten, welche Anwendung gestartet werden soll.

**Warum ist der Bootloader kritisch?**

Wenn wir ihn mit einer falschen Datei oder falschen Chip-Konfiguration überschreiben, kann das Gerät unter Umständen nicht mehr normal starten.

Deshalb gilt für unseren ersten Test:

```text
Bootloader: NICHT ANFASSEN
```

## 4. Was ist die Partitionstabelle?

Die Partitionstabelle beschreibt, welcher Bereich des Flash-Speichers welche Aufgabe hat.

Beispiel, nur zur Veranschaulichung:

```text
Name       Start       Größe
---------------------------------
nvs        0x009000    ...
otadata    0x00E000    ...
ota_0      0x010000    ...
ota_1      0x650000    ...
```

Die Partitionstabelle ist also gewissermaßen die "Landkarte" des Flash-Speichers.

Wenn eine Firmware von anderen Offsets ausgeht als das Gerät tatsächlich verwendet, kann ein Blind-Flash Datenbereiche überschreiben.

Deshalb gilt:

```text
Partitionstabelle zuerst AUSLESEN.
Partitionstabelle beim ersten Test NICHT ÜBERSCHREIBEN.
```

## 5. Was ist NVS?

NVS steht für **Non-Volatile Storage**.

Das ist ein Bereich für kleine persistente Daten. Je nach Firmware können dort beispielsweise liegen:

- Geräteeinstellungen;
- WLAN-Daten;
- Hardwareinformationen;
- Kalibrierungswerte;
- Displaycontroller-Auswahl;
- andere gerätespezifische Parameter.

Wir wissen nicht bei jedem Produktionsstand des X4 Pro vollständig, welche Daten dort wichtig sind.

Daher:

```text
NVS sichern und beim ersten Test nicht löschen.
```

## 6. Was ist OTA?

OTA bedeutet **Over The Air Update**.

Das Grundprinzip funktioniert unabhängig davon, ob die Firmware tatsächlich per WLAN übertragen wird.

Ein Gerät kann zwei Anwendungsbereiche haben:

```text
OTA Slot A
OTA Slot B
```

Beispiel:

```text
Slot A = aktuell funktionierende Original-Firmware
Slot B = frei / alte Firmware
```

Für ein Update kann die neue Anwendung zunächst in Slot B geschrieben werden.

Danach wird umgeschaltet:

```text
vorher:
ACTIVE -> Slot A
          Slot B

nachher:
          Slot A
ACTIVE -> Slot B
```

Das ist für uns sehr wertvoll.

Wir wollen den bekannten funktionierenden Slot zunächst **nicht überschreiben**.

## 7. Was ist `otadata`?

`otadata` enthält Informationen darüber, welcher OTA-App-Slot gebootet werden soll.

Man kann sich diesen Bereich vereinfacht als Boot-Auswahl merken:

```text
otadata -> starte ota_0
```

oder

```text
otadata -> starte ota_1
```

Deshalb schreiben wir beim ersten Versuch nicht blind in `otadata`.

Zuerst müssen wir wissen:

- welche OTA-Slots existieren;
- welcher Slot aktuell funktioniert;
- welcher Slot inaktiv ist;
- wie der getestete Recovery-Weg zurück funktioniert.

## 8. Was ist `firmware.bin`?

PlatformIO erzeugt beim Build mehrere Dateien.

Für unseren ersten Test interessiert uns hauptsächlich:

```text
.pio/build/x4pro/firmware.bin
```

Diese Datei enthält **nur die eigentliche Inx-Anwendung**.

Sie ist nicht dasselbe wie ein Full-Flash-Image.

Wir wollen gerade **keinen** Bundle-Flash ausführen, der zusätzlich diese Dateien schreibt:

```text
bootloader.bin
partitions.bin
```

Das ist absichtlich.

## 9. Warum ist `pio run -t upload` blockiert?

PlatformIO kann normalerweise den gesamten Flash-Vorgang automatisieren.

Das ist bei bekannten Entwicklungsboards praktisch. Für unseren ersten X4-Pro-Port ist es aber zu viel Automatik.

Ein Standard-Upload kann – abhängig vom Build und Upload-Setup – Bootloader, Partitionstabelle und Anwendung schreiben.

Wir wollen dagegen exakt kontrollieren:

```text
WAS wird geschrieben?
WO wird geschrieben?
WAS bleibt garantiert unangetastet?
```

Deshalb bricht der X4-Pro-Port einen generischen PlatformIO-Upload absichtlich ab.

Wenn dieser Befehl fehlschlägt:

```bash
pio run -e x4pro -t upload
```

ist das momentan **gewollt**.

## 10. Was bedeutet "bricken"?

"Brick" bedeutet sinngemäß: Das Gerät verhält sich nur noch wie ein Ziegelstein.

Dabei muss man zwischen zwei Situationen unterscheiden.

### Soft-Brick

Die Anwendung startet nicht mehr, aber der ESP32-S3 kann weiterhin über USB/Bootloader/Recovery angesprochen werden.

Beispiele:

- schwarzer oder unveränderter E-Ink-Bildschirm;
- Bootloop;
- App stürzt direkt ab;
- falscher App-Slot ausgewählt.

Ein Soft-Brick ist häufig reparierbar.

### Hard-Brick bzw. schwer recoverbarer Zustand

Bootregion, Flash-Konfiguration oder Recovery-Zugang wurden so verändert, dass die üblichen Wiederherstellungswege nicht mehr funktionieren.

Genau dieses Risiko reduzieren wir, indem wir Bootloader und Partitionstabelle beim ersten Test nicht anfassen.

## 11. Unser Sicherheitsprinzip

Wir behandeln den vorhandenen funktionierenden Zustand wie ein Produktionssystem.

Das Vorgehen ist:

```text
READ
  |
  v
VERIFY
  |
  v
BACKUP
  |
  v
VERIFY BACKUP
  |
  v
WRITE ONLY INACTIVE APP SLOT
  |
  v
VERIFY WRITE
  |
  v
TEST BOOT
  |
  +---- funktioniert ----> weiter testen
  |
  +---- funktioniert nicht -> zurück zum bekannten Slot
```

Nicht:

```text
erase-flash
   |
   v
full flash
   |
   v
hoffen
```

## 12. Phase 0: Firmware in CI testen

Bevor echte Hardware beschrieben wird, muss das Projekt überhaupt korrekt für den X4 Pro gebaut werden.

Build:

```bash
pio run -e x4pro
```

CI prüft denselben X4-Pro-Target.

Dabei validieren wir unter anderem:

- ESP32-S3-Target;
- FreeInk-Abhängigkeiten;
- Kompilierbarkeit der HAL;
- Linken der Anwendung;
- Firmwaregröße.

Ein grüner Build beweist noch nicht, dass Display, Touch oder Sleep elektrisch korrekt funktionieren.

Er beweist aber, dass wir kein offensichtliches C3-Binary mehr erzeugen.

## 13. Phase 1: Gerät nur lesen

Das erste echte Gerät wird zunächst **nicht beschrieben**.

### Voraussetzungen

```bash
python3 -m pip install --upgrade esptool platformio
```

Reader per USB-C anschließen.

Unter macOS:

```bash
ls /dev/cu.*
```

Beispiel:

```text
/dev/cu.usbmodem1101
```

### X4-Pro-Firmware bauen

```bash
pio run -e x4pro
```

### Inspector starten

```bash
python3 scripts/x4pro_inspect.py \
  --port /dev/cu.usbmodem1101 \
  --firmware .pio/build/x4pro/firmware.bin \
  --backup
```

Das Script ist absichtlich read-only.

Es macht folgende Schritte.

### Schritt A: Chip erkennen

Es fragt den ESP ab und akzeptiert nur einen ESP32-S3.

Wenn versehentlich ein klassischer X4 mit ESP32-C3 angeschlossen wäre, soll der Test abbrechen.

### Schritt B: Partitionstabelle lesen

Es liest nur den Flash-Bereich der Partitionstabelle.

Danach kennen wir die realen Offsets des Geräts.

### Schritt C: OTA-Slots validieren

Das Script verlangt mindestens zwei OTA-Anwendungs-Slots.

Ein Single-Slot-Layout wird für unseren ersten Versuch nicht akzeptiert.

### Schritt D: Größe prüfen

`firmware.bin` muss vollständig in den Zielslot passen.

Wenn nicht:

```text
ABBRUCH
```

### Schritt E: kompletten Flash sichern

Mit `--backup` werden die gesamten 16 MiB gelesen.

Ergebnis:

```text
x4pro-preflight/
├── partition-table.bin
├── x4pro-full-16mb.bin
└── x4pro-full-16mb.bin.sha256
```

## 14. Warum SHA-256?

SHA-256 ist ein kryptografischer Hash.

Er dient hier als Fingerabdruck der Backup-Datei.

Beispiel:

```text
9f...ab  x4pro-full-16mb.bin
```

Wenn die Datei später beschädigt oder verändert wird, stimmt der Hash nicht mehr.

Damit können wir überprüfen, dass unser Recovery-Backup noch exakt dasselbe ist.

## 15. Was sichern wir mit dem Full-Flash-Dump?

Der 16-MiB-Dump enthält eine bitweise Kopie des externen Flash-Speichers, unter anderem:

- Bootloader;
- Partitionstabelle;
- NVS;
- OTA-Metadaten;
- vorhandene App-Slots;
- weitere persistente Datenpartitionen.

Das ist wesentlich besser als nur die Original-App zu sichern.

> Das Backup allein garantiert trotzdem keine Recovery. Auch der tatsächliche USB-/Bootloader-Zugang des konkreten Geräts muss funktionieren. Deshalb validieren wir beides vor dem ersten Schreibtest.

## 16. Phase 2: aktiven und inaktiven Slot bestimmen

Dieser Schritt ist noch Teil der Hardware-Freigabe.

Wir müssen eindeutig feststellen:

```text
welcher Slot wird aktuell gebootet?
welcher Slot bleibt als Recovery erhalten?
welcher Slot darf das Test-Image aufnehmen?
```

Solange das nicht eindeutig ist, wird nichts geschrieben.

## 17. Phase 3: nur den inaktiven App-Slot schreiben

Der spätere Schreibbefehl wird ungefähr dieses Prinzip haben:

```text
write firmware.bin -> exakt ermittelter inaktiver OTA-App-Offset
```

Nicht:

```text
write -> 0x0
```

und nicht:

```text
erase entire flash
```

Die konkrete Adresse wird **erst aus der ausgelesenen Partitionstabelle übernommen**.

Beispielsyntax, absichtlich noch nicht ausführbar dokumentiert:

```bash
esptool ... write-flash <INACTIVE_OTA_OFFSET> firmware.bin
```

Solange `<INACTIVE_OTA_OFFSET>` nicht durch einen verifizierten Wert des konkreten Geräts ersetzt wurde, ist das kein Flash-Befehl.

## 18. Phase 4: geschriebenes Image verifizieren

Nach einem späteren Schreibvorgang soll der entsprechende Bereich wieder gelesen und mit `firmware.bin` verglichen werden.

Damit prüfen wir:

```text
Datei, die wir bauen
        ==
Bytes, die wirklich im OTA-Slot liegen
```

Erst danach wird eine Boot-Umschaltung erwogen.

## 19. Phase 5: Test-Boot

Beim ersten Start geht es nicht darum, sofort alle Inx-Funktionen zu testen.

Reihenfolge:

### 1. Boot

```text
[ ] kein Reset-Loop
[ ] serielle Ausgabe erreichbar
[ ] Anwendung erreicht setup()/Hauptprogramm
```

### 2. Display

```text
[ ] Displaycontroller wird erkannt
[ ] BUSY hängt nicht dauerhaft
[ ] vollständiges Bild erscheint
[ ] Orientierung stimmt
[ ] keine offensichtlich falschen Spannungs-/Refresh-Effekte
```

### 3. Eingabe

```text
[ ] obere/erste Seitentaste
[ ] untere/zweite Seitentaste
[ ] Tap
[ ] Home-Taste
[ ] horizontaler Swipe
[ ] Power-Taste
```

### 4. SD-Karte

```text
[ ] SDMMC initialisiert
[ ] Karte mountet
[ ] Dateien können gelesen werden
[ ] EPUB lässt sich öffnen
```

### 5. Stromversorgung

```text
[ ] Batterieanzeige plausibel
[ ] USB-Erkennung plausibel
[ ] Gerät kann schlafen
[ ] Gerät wacht wieder auf
[ ] Neustart funktioniert
```

Erst danach werden Komfortfunktionen wie Frontlight und RTC weiter integriert.

## 20. Temporäres Bedienmodell

Inx ist ursprünglich für mehr physische Tasten ausgelegt.

Der X4 Pro besitzt dagegen zwei Seitentasten und Touch.

Die aktuelle Kompatibilitätsschicht ist:

```text
Seitentaste 1       -> Up / Previous
Seitentaste 2       -> Down / Next
Touch Tap           -> Confirm
Home Tap            -> Back
Swipe horizontal    -> Left / Right
Power               -> Power
```

Das ist keine endgültige Touch-Oberfläche.

Es ermöglicht zunächst, das bestehende Inx-Menü auf echter Hardware zu testen.

## 21. Was passiert, wenn Inx nicht startet?

Dann gilt zuerst:

```text
NICHT PANISCH WEITERFLASHEN
```

Wir unterscheiden:

### Fall A: USB/Bootloader weiterhin erreichbar

Das ist der gewünschte Recovery-Fall.

Dann können wir den Boot wieder auf den unveränderten bekannten Slot zurückstellen bzw. über den validierten Recovery-Weg starten.

### Fall B: Display bleibt stehen, aber serieller Boot funktioniert

Dann ist möglicherweise nur die Anwendung oder Displayinitialisierung fehlerhaft.

Der E-Ink-Bildschirm behält sein letztes Bild auch ohne laufende Anwendung. Ein eingefrorenes Bild bedeutet daher nicht automatisch, dass der ESP32 tot ist.

### Fall C: Bootloop

Serielle Logs auslesen. Originalslot nicht überschreiben. Anschließend Recovery-Slot auswählen.

### Fall D: keine USB-Kommunikation

Dann wird **nicht** mit weiteren zufälligen Flash-Befehlen experimentiert. Wir prüfen zuerst Boot-Modus, Kabel, USB-Gerät und den konkreten Recovery-Zugang.

## 22. Warum E-Ink besonders wirkt

Ein E-Ink-Display unterscheidet sich von LCD/OLED.

Nach einem Refresh bleibt das Bild physisch sichtbar, auch wenn der Prozessor schläft oder abstürzt.

Daher kann diese Situation auftreten:

```text
Display zeigt Menü
ESP32-Anwendung ist aber bereits abgestürzt
```

Deshalb sind serielle Logs beim Bring-up wichtig.

## 23. Hardware-Abstraktionsschicht

Inx sollte nicht überall direkt GPIO-Nummern kennen.

Stattdessen verwenden wir FreeInk als Hardware-Abstraktion.

Vereinfacht:

```text
Inx UI / Reader
      |
      v
HalDisplay / HalGPIO
      |
      v
FreeInk
      |
      v
X4-Pro BoardConfig
      |
      v
ESP32-S3 Pins und Peripherie
```

Dadurch liegen X4-Pro-spezifische Dinge wie Displaypins, Touch-I2C, SDMMC und Batteriegauge an einer zentralen Stelle.

## 24. Wichtige X4-Pro-Komponenten

### Display

800x480 E-Ink. Je nach Produktionsbatch kann ein anderer kompatibler Controller verbaut sein.

Deshalb wird der Controller beim Boot erkannt, bevor das Display initialisiert wird.

### GT911

Der GT911 ist der Touchcontroller.

Er kommuniziert über I2C mit dem ESP32-S3.

### SDMMC

Die microSD-Karte des X4 Pro wird nicht wie beim alten X4 einfach als SPI-SD behandelt.

Der Port verwendet den nativen SDMMC-Pfad des FreeInk-X4-Pro-Profils.

### PSRAM

Der X4 Pro besitzt zusätzliches externes RAM. Das ist für größere Puffer und komplexere Reader-Funktionen nützlich.

### CW2017

Der CW2017 ist der Batteriegauge. Er liefert den Ladezustand der Batterie.

### Frontlight

Der X4 Pro besitzt warmes und kaltes Frontlight. Die Hardwareunterstützung ist bekannt, die Inx-Oberfläche dafür ist aber noch nicht integriert.

## 25. Befehle, die momentan ausdrücklich verboten sind

Nicht verwenden:

```bash
esptool erase-flash
```

Nicht verwenden:

```bash
pio run -e x4pro -t upload
```

Nicht blind verwenden:

```bash
esptool write-flash 0x0 ...
```

Nicht flashen:

```text
bootloader.bin
partitions.bin
```

Nicht versuchen, die Upstream-Inx-OTA-Funktion für den X4 Pro zu verwenden.

## 26. Der sichere erste praktische Schritt

Der nächste reale Hardware-Schritt ist ausschließlich:

```bash
python3 scripts/x4pro_inspect.py \
  --port <DEIN_USB_PORT> \
  --firmware .pio/build/x4pro/firmware.bin \
  --backup
```

Danach werden die Ergebnisse geprüft.

Erst wenn folgende Punkte erfüllt sind, beginnt die Flash-Phase:

```text
[ ] ESP32-S3 eindeutig erkannt
[ ] Partitionstabelle plausibel
[ ] mindestens zwei OTA-App-Slots
[ ] firmware.bin passt vollständig
[ ] aktiver Slot eindeutig bestimmt
[ ] inaktiver Slot eindeutig bestimmt
[ ] vollständiger 16-MiB-Dump vorhanden
[ ] SHA-256 vorhanden und geprüft
[ ] USB-Recovery-Weg funktioniert
[ ] Originalslot bleibt unangetastet
```

## 27. Kurzfassung

Wenn du dir nur fünf Dinge merken willst:

1. **Bootloader = Startprogramm. Nicht überschreiben.**
2. **Partitionstabelle = Landkarte des Flash. Erst auslesen, nicht ersetzen.**
3. **OTA-Slots = zwei Plätze für Anwendungen. Einen behalten wir als Rettungsanker.**
4. **Full-Flash-Backup = Kopie des aktuellen Gerätezustands vor unserem Experiment.**
5. **Wir schreiben beim ersten Test nur die Inx-App in den verifizierten inaktiven App-Slot.**

Damit wird aus einem riskanten "Firmware drauf und hoffen" ein kontrollierter, reversibler Bring-up-Prozess.
