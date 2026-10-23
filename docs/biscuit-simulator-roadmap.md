# Biscuit im X4-Pro-Simulator – Integrationsplan

## X4-Pro-Hardwareabnahme: Touch-Footer nur in Biscuit (24.10.2026)

Ein Gerätefoto zeigte im Shelf eine über den unteren Listeneinträgen
liegende Leiste mit „Zurück“, „Öffnen“, „Hoch“, „Runter“. Ursache:
`ShelfFolderActivity` rief `drawButtonHintsAlways()` auf, obwohl die
FreeInkUI-Liste eine eigene Touch-/Seitentasten-Navigation besitzt.

**Regel für GT911-Touchgeräte:** Die permanenten, tappbaren
Vier-Tasten-Footer bleiben ausschließlich in den Biscuit-Activities.
Shelf, Spiele, Player und die bisherigen CrossPlay-Apps benutzen stattdessen
ihre eigenen Touch-Flächen und Gesten; sie zeichnen keinen zusätzlichen
Biscuit-Footer. Die vorhandenen nicht erzwungenen Button-Hinweise dürfen auf
Geräten ohne Touch weiterhin erscheinen. Bestehende Biscuit-
`drawButtonHintsAlways()`-Aufrufe und passende Hitboxen bleiben intakt.

Der Host-Wiring-Test verhindert eine erneute Einblendung in
`ShelfFolderActivity` und den ursprünglichen CrossPlay-Apps.
CI-Firmware- und SDL-Tests prüfen die Software; eine abschließende
Sichtprüfung auf realem GT911 bleibt erforderlich.


## Aktueller Integrationsstand (23.10.2026)

Der native X4-Pro-Simulator führt **25 Original-Activities und 1 sichere UI-Preview** aus:

| Gruppe | Activities |
| --- | --- |
| Offline/System | CLOCK, DEVICE INFO, OTP GENERATOR |
| WLAN/Netzwerk | WIFI CONNECT, DNS LOOKUP, PING, HOST SCANNER, HTTP CLIENT, MDNS BROWSER, PROBE SNIFFER, CROWD DENSITY, DEV FINGERPRINT, DEAUTH DETECTOR, PACKET MONITOR, NETWORK MONITOR, WIFI DEAUTH |
| BLE | BLE PROXIMITY, BLE SCANNER, BLE CONTACT, TRACKER DETECT, BLE BEACON, AIRTAG SPOOF, BLE SPAM, BLE KEYBOARD |
| ESP-NOW | MESH CHAT (rein virtueller Transport) |
| UI-only, keine Angriffslogik | FIRE (separater nativer Preview-Adapter) |

Die ursprünglichen No-op-VTables der fünfundzwanzig Original-Activities wurden entfernt.
Ihre realen Implementierungen und Shelf-Einträge werden auf dem Simulator
kompiliert. Hardwarepfade bleiben für das Firmware-Build erhalten.

Letzte vollständig grüne Pipeline:
[CI #40014408650](https://github.com/0b-ivan/inx-4x-pro/actions/runs/40014408650)
(24 Activities, einschließlich Touch-E2E und visueller Regression).
Der neue Clock-Zeitplan und Mesh Chat benötigen jeweils neue CI-Bestätigung.

## Deterministische Fixtures – kein Zugriff auf Funkhardware

- Uhr: `CROSSPLAY_SIM_EPOCH` setzt eine feste UTC-Zeit.
- WLAN: Simulator-APs, `CROSSPLAY_SIM_WIFI_CONNECTED=0|1` und
  `CROSSPOINT_SIM_WIFI_CONNECT=fail|timeout|no-ssid`.
- DNS: `example.test`, `gateway.test` und `localhost`; alle anderen
  Domains werden ohne DNS-Netzwerkanfrage abgewiesen.
- Ping: TCP-Port 80 aus einer In-Memory-Fixture, nicht ICMP und
  keine echten Host-Sockets.
- HTTP Client: lokale GET-200-, GET-404-, POST-Echo- und Timeout-Fixtures.
- mDNS Browser: virtuelle HTTP-/SSH-Serviceeinträge.
- Host Scanner: zwei synthetische Geräte im Dokumentationsbereich
  `192.0.2.0/27`, keine echte LAN-Erkundung.
- BLE: `CROSSPLAY_SIM_BLE_SCENARIO=normal|weak|empty|timeout|disconnect`.
  Zwei virtuelle Advertisements, RSSI-Werte, GATT-Dienste, Characteristics
  und deterministische Read-Werte. Keine physischen BLE-Verbindungen.

**Simulation ist kein Hardware- oder Funkinteroperabilitätstest.**
Die Implementierung prüft UI, Navigation und reproduzierbare Zustände;
reale Radio-Initialisierung, Pairing, Signalqualität und Peer-Protokolle
müssen separat auf dem Gerät geprüft werden.

## Automatisierte Abnahme in GitHub Actions

`host-tests/biscuit-sim/run.sh` validiert Clock-, Netzwerk- und BLE-Fixtures
sowie die Wiring-Regeln. Diese verhindern, dass eine echte Activity
unbemerkt durch einen Stub ersetzt oder durch `#if !defined(SIMULATOR)`
ausgeblendet wird.

`scripts_local/biscuit_sim_smoke.sh` startet den nativen SDL-Simulator
über Xvfb mit `CROSSPLAY_AUTOSTART`, gibt Eingaben mit
`CROSSPOINT_SIM_INPUT_SCRIPT` vor und sichert BMPs über
`CROSSPOINT_SIM_SCREENSHOTS`. Geprüft werden:

- Autostart und nichtleere Screenshots aller 26 Apps
- OTP-Wechsel, HTTP-Menüänderung und mDNS-Discovery-Darstellung
- Host-Scanner-Darstellung bei verbundenem/getrenntem WLAN
- BLE Proximity mit virtuellen Geräten gegenüber leerem Scan
- BLE Scanner: Scan → Services → Characteristics → Characteristic-Wert →
  zurück über Characteristics und Services zur Scanansicht.
  Nur für diesen Test aktiviert `CROSSPLAY_SIM_BLE_TRACE=1` explizite
  Zustandsmarker; ihre Reihenfolge und die jeweiligen Bildänderungen
  werden überprüft.

Die Screenshots werden als CI-Artefakt bereitgestellt.
**Ein Bildvergleich allein beweist keine semantische Korrektheit**; deshalb
prüft der vollständige BLE-GATT-Rundgang zusätzlich den Zustandsverlauf.

## Nächste Integrationsstufen

1. BLE Contact und weitere rein passive BLE-Anzeigen mit virtuellen Peers.
2. Stale/Disconnect- und Fehlerfälle als eigene vollständige SDL-Abläufe.
3. Rohframe-Analyzer mit ausschließlich synthetischen Frames, soweit
   risikolos und ohne Zugriff auf fremde Funknetze.
4. Kontrolliertes Testen von Lifecycle, Ressourcenfreigabe und UI-Navigation.
5. Hardware-Abnahme für alle Radiofunktionen auf autorisierter Testhardware.

## Testergänzung: BLE-Abbruch und Radio-Cleanup

Die BLE-Contact-Activity beendet die Funknutzung jetzt **vor** der Rückgabe
an Developer Mode. Der Cleanup ist idempotent und wird im Simulator durch
`BLE_CONTACT:radio-off` protokolliert. Die Headless-CI testet den regulären
Austausch, einen leeren Scan sowie einen Abbruch vor dem ersten Scanzyklus.
Zusätzlich wird beim BLE Scanner ein fehlgeschlagener GATT-Verbindungsaufbau
mit `CROSSPLAY_SIM_BLE_SCENARIO=disconnect` geprüft. Alle Szenarien sind
lokale Fixtures ohne BLE-Hostzugriff.

Die Abdeckung dieser neuen Tests ist erst nach erfolgreichem CI-Lauf bestätigt.

## Passiver Tracker Detector – virtuelle BLE-Werbepakete

\`TRACKER DETECT\` verarbeitet ausschließlich synthetische Pakete mit
Testkennungen für Apple, Samsung und Tile; ein unbekanntes Paket wird
ignoriert. Szenarien: \`CROSSPLAY_SIM_TRACKER_SCENARIO=normal|empty|timeout|follower\`.
Das \`follower\`-Szenario simuliert eine längere Beobachtung und kann so
den realen Schwellenwert ohne 30 Minuten Wartezeit auslösen. Dabei entstehen
**weder BLE-Scans noch Funkübertragungen oder Verbindungen** auf dem Host.

Neue SDL-Regressionen prüfen normalen Scan, leeren Scan, synthetischen
Verfolger-Alarm, UI-Übergänge und Ressourcenfreigabe. Die neue Pipeline muss
diese noch bestätigen. Physische Radio-Interoperabilität ist nicht abgedeckt.

## Passiver Probe Sniffer – isolierte 802.11-Frames

Die echte Probe-Sniffer-Activity verarbeitet synthetische Probe Requests über
ihren vorhandenen `onProbeRequest`-Handler (SSID, MAC, RSSI, Duplikate).
Die In-Memory-Fixtures enthalten gezielte SSIDs, Broadcast-SSID, mehrfach
gesendete Frames und ungültige Management-Frames. Szenarien:
`CROSSPLAY_SIM_PROBE_SCENARIO=normal|empty|malformed`.
SDL testet Geräteliste, Detailansicht, Rücknavigation und Radio-Cleanup.
Der neue Stand ist erst nach grüner CI vollständig bestätigt.

**Keine realen WLAN-Scans, Monitor-Mode-Aufrufe oder fremde Gerätedaten.**

## Crowd Density: synthetische Messfenster

`CROWD DENSITY` benutzt die echte Activity und erhält ausschließlich synthetische
802.11-Probe-Requests. MAC-Deduplizierung, Ringpuffer für Messwerte und die
grafische Darstellung bleiben erhalten. Im Simulator wird das reguläre
30-Sekunden-Messfenster auf drei Sekunden beschleunigt. Auf der Firmware
bleiben die produktiven 30 Sekunden unverändert.

Die neue SDL-Suite prüft drei eindeutige MAC-Adressen trotz Duplikaten,
eine erste Fenster-Auswertung, den erneuten Start der Zählung und Radio-Cleanup.
`CROSSPLAY_SIM_PROBE_SCENARIO=empty|malformed` liefert Negativtests.

Die angezeigte Zahl ist eine **Geräteschätzung, keine Personenzählung**.
MAC-Randomisierung, mehrere Geräte pro Person und unvollständige Scans
verursachen teils erhebliche Abweichungen. Es werden keine realen Pakete
aufgezeichnet und kein WLAN-Monitor-Modus aktiviert.

Diese Erweiterung gilt erst nach erfolgreicher CI als validiert.

## Native BMP-Previews und pixelgenaue SDL-Referenzbilder

Aus dem Original-Repository `0b-ivan/biscuit_x3` wurde der native
`BitmapRenderer`-Mock übernommen. Die zusätzliche Host-Test-Suite
`host-tests/biscuit-preview/run.sh` kompiliert C++17, testet 1-Bit-BMPs,
Framebuffer-Leerzustand, zwei Vorschau-Szenen und Determinismus. Die
Mock-Previews sind **keine echten Activity-Render-Tests** und werden
in der CI ausdrücklich von den SDL-End-to-End-Screenshots unterschieden.

`host-tests/biscuit-sim/test_visual_goldens.py` gleicht echte SDL-BMPs mit
**sechs genehmigten pixelgenauen Baselines** ab: CLOCK, DEVICE INFO,
BLE Scanner/GATT-Wert, mDNS Browser, Host Scanner offline und Probe Sniffer.
Die Referenzen entstanden aus zwei erfolgreichen unabhängigen CI-Läufen
[#39984886630](https://github.com/0b-ivan/inx-4x-pro/actions/runs/39984886630)
und [#39988874651](https://github.com/0b-ivan/inx-4x-pro/actions/runs/39988874651).
Beide Durchläufe zeigten für diese Screenshots identische Pixel.

Der Test normiert den tatsächlichen 480 × 800 BGRA-Framebuffer auf Schwarz/
Weiß und vergleicht SHA-256, Gesamtzahl dunkler Pixel und die Verteilung
auf vier Quadranten. Bei Abweichungen scheitert CI; die vollständigen
Original-BMPs bleiben im Screenshot-Artefakt zur visuellen Analyse erhalten.
Screens mit nachweisbar zeitabhängigem Inhalt (z. B. OTP) sind noch nicht
als exakte Baselines zugelassen. Eine neue Referenz muss explizit geprüft
und eingecheckt werden; CI aktualisiert sie **niemals automatisch**.

**Lokal:** `bash host-tests/biscuit-preview/run.sh` sowie nach dem SDL-Lauf
`python3 host-tests/biscuit-sim/test_visual_goldens.py .pio/biscuit-ui-smoke`.

## Device Fingerprinter – synthetische OS-Heuristiken

Die echte `DeviceFingerprinterActivity` erhält ausschließlich künstliche 802.11-
Probe-Requests aus der bestehenden Fixture-Infrastruktur. Fünf einzigartige
MACs erzeugen iOS-, Android-, Windows-, Linux- und Unknown-Ergebnisse sowie
zwei absichtlich ungültige Pakete. Die originale `onProbePacket()`-Methode
führt Zählung, Deduplizierung und `estimateOs()` aus.

`CROSSPLAY_SIM_FINGERPRINT_SCENARIO=normal|empty|malformed` und optional
`CROSSPLAY_SIM_FINGERPRINT_TRACE=1` prüfen die echte Logik, UI-Übergänge und
Ressourcenfreigabe. **OS-Erkennung aus Probe-Häufigkeit und OUI ist lediglich
eine unzuverlässige Heuristik**, keine Identifizierung des Geräts oder Users.
Keine WLAN-Hardware oder fremden Netze werden im Simulator untersucht.
Der neue Stand gilt erst nach grüner CI als bestätigt.

## Deauth Detector – passiv und vollständig synthetisch

Die echte `DeauthDetectorActivity` führt ihren bestehenden `onPacket()`-
Handler mit ausschließlich im Arbeitsspeicher konstruierten 802.11-
Management-Frames aus. `CROSSPLAY_SIM_DEAUTH_SCENARIO=normal|spike|empty|malformed`
prüft Deauthentication-/Disassociation-Zähler, Ereignisringpuffer,
einen simulierten Alarm mit sechs Deauth-Frames, leere Scans,
ungültige Pakete und die Freigabe der simulierten Radio-Ressourcen.

Die SDL-Suite startet die Activity, prüft den Monitor-Bildschirm, Alarm,
Quittierung und Rücknavigation. Alle realen `esp_wifi`-Zugriffe sind im
Simulator ausgeschlossen. **Kein echtes Aussenden von Deauth-Paketen,
keine Überwachung fremder WLAN-Netze.** Die Heuristik erkennt lediglich
eine Häufung solcher Frames; sie belegt für sich genommen keinen Angriff.
Der neue Stand bedarf noch der vollständigen CI-Bestätigung.

## Packet Monitor – synthetische Frame-Statistik

Die echte `PacketMonitorActivity` wertet ausschließlich synthetische
802.11-Management-Frames aus. Die vorhandenen Zähler, der MAC-Hashspeicher
und der Kanalverlauf werden über die originale `onPacket()`-Methode
aktualisiert. `CROSSPLAY_SIM_PACKET_SCENARIO=normal|empty|malformed` erzeugt
fünf Pakete mit drei eindeutigen lokalen MAC-Adressen, einen leeren Lauf
oder nur ein kurzes Paket ohne gültige Quell-MAC. SDL prüft Live-Zähler,
Kanaleingaben und ordnungsgemäßes Radio-Cleanup.

Die Aufnahmefunktion bleibt im Quellcode vorhanden; ihre persistente
PCAP-Schreibstrecke benötigt noch eine eigene isolierte Abnahme. Im
Simulator wird **keine Funkhardware aktiviert**, kein fremdes Paket
aufgezeichnet und kein WLAN-Paket gesendet. Auch die neue Packet-Monitor-
Integration muss erst den Firmware-/Host-/SDL-CI-Lauf bestehen.

## Network Monitor: Frame-Gruppierung und Rogue-AP-Prüfung

Die originale `NetworkMonitorActivity` wertet ausschließlich synthetische
802.11-Management-Frames aus und gruppiert sie nach Quell-MAC/BSSID.
Ein zweiter simulato­rinterner Testlauf erzeugt in drei Scanphasen drei
virtuelle Access Points (zwei davon mit gleicher SSID `SimLab`, aber
unterschiedlicher Verschlüsselung und Kanal), um die tatsächliche
`analyzeGroups()`-Anomalieerkennung zu prüfen. Die SDL-Tests starten
Monitor, Event-Detail, Rücknavigation, Rogue-AP-Liste, Rogue-AP-Detail
sowie leere Scans und Radio-Cleanup. Im nativen Build laufen weder
Monitor-Modus noch echte WLAN-Scans.

Diese Erweiterung bedarf noch einer vollständig grünen CI-Abnahme.

## Offline-BLE-Advertising-Previews

`BLE BEACON` und `AIRTAG SPOOF` führen im nativen Simulator ihre echten
Menü-, Auswahl-, Intervall-, Zähler-, Abbruch- und Anzeigezustände aus.
Statt funkfähiger oder markentreuer Advertising-Daten benutzen beide den
explizit nicht sendefähigen Test-Payload `OFFLINE-SIM:<mode>:<sequence>`.
Die Testdaten werden nur im Arbeitsspeicher erzeugt und ausschließlich in
Status-/Screenshot-Regressionsprüfungen verwendet. Die AirTag-MAC-Rotation
ist im Simulator auf zwei Sekunden beschleunigt, damit sie ohne Wartezeit
geprüft werden kann; Firmware-Timing bleibt unverändert.

**Kein BLE-Funkverkehr, kein Replay, keine reale Apple-/Google-/Samsung-Identität.**
Die reale drahtlose Interoperabilität wird damit nicht geprüft. Die beiden
Integrationen benötigen noch einen vollständig grünen neuen CI-Lauf.

## WiFi Deauth: ausschließlich Simulation ohne Übertragung

`WIFI DEAUTH` führt im nativen Simulator die echte Auswahl- und
Aktivitätslogik mit zwei rein virtuellen APs aus. Der Hardware-TX-Pfad
bleibt unverändert und ist mittels `#ifndef SIMULATOR` vollständig aus
der nativen Kompilierung ausgeschlossen. Der Simulator zählt ausschließlich
lokale Testereignisse und kennzeichnet die Ansicht mit `NO TX` sowie
`Simulated Frames`. Ein leerer Scan sowie Abbruch und Radio-Cleanup
sind Teil der Headless-SDL-Abnahme. **Es werden keine Deauth-Pakete gesendet.**

## BLE Spam: nur virtueller, nicht sendefähiger Testdatenstrom

Die originale Activity samt Disclaimer, Auswahlmenü, Intervallsteuerung,
Random-All-Zyklus und Stopp ist im Simulator enthalten. Die Vorschau
nutzt ausschließlich `OFFLINE-SIM`-Datensätze, die ausdrücklich keine
BLE-Advertising-Frames darstellen. Der originale Arduino-BLE-Pfad wird nur
auf der Hardware kompiliert. Der SDL-Lauf prüft alle vier Modusfamilien,
Laufzeit-Zähler und Ressourcenfreigabe. **Kein Funk, keine fremden Geräte.**

## BLE Keyboard: isolierte DuckyScript-/HID-Emulation

Die echte `BleKeyboardActivity` führt im Simulator ihr eingebautes
Demo-DuckyScript durch: Skriptliste, Vorschau, virtuelle Paarung,
DuckyScript-Befehle (`STRING`, `GUI`, `ENTER`, `DELAY`), Done und Stopp.
Anstelle von Bluetooth-HID-Berichten werden in-memory Ereignisse gezählt
und protokolliert. Skripte des Host-Dateisystems werden nicht geladen;
Simulator-Delays sind auf 100 ms begrenzt. Der BLE-Server und die
HID-Report-Charakteristik sind ausschließlich im Firmwarepfad aktiv.
**Keine echten Tastatureingaben und keine Bluetooth-Kopplung am Host.**

## X4 Pro Touch-E2E

Der X4 Pro hat ein **GT911-Touchpanel** (FreeInk `FREEINK_CAP_TOUCH`).
Die bisherige Biscuit-CI verwendete fast ausschließlich simulierte
Tastendrücke. Zusätzlich enthält die SDL-Smoke-Suite nun einen echten
Touch-Eingabetest über `CROSSPOINT_SIM_INPUT_SCRIPT` mit `TAP:x,y,120`:

- Listeneintrag per Koordinate treffen (`rowTouch`)
- On-Screen-Tasten „Select“, „Back“ und „Exit“ bedienen
- Unterscheidbare 480 × 800-BMPs für Liste/Detail/Rücknavigation/Exit
- Nachweis der echten `ProbeSnifferActivity`-Zustände und Radio-Freigabe

Die Touch- und Tastensimulation laufen über denselben
`MappedInputManager`, aber über unterschiedliche Eingabepfade. Ein
Tasten-Smoke-Test ersetzt den Touch-Test daher nicht. Wischgesten,
Orientierungswechsel und weitere Activities brauchen separat erweiterte
Abdeckung. Das ersetzt keinen Test am realen GT911-Digitizer.

## Übergreifender Touch-Footer-Fix

Der X4 Pro setzt die normale reservierte Höhe für Tastenhinweise in
`UITheme::getMetrics()` auf null. Biscuit-Activities verwenden aber
`drawButtonHintsAlways()` und zeichnen damit eine sichtbare Touch-Leiste
mit 40 Pixeln Höhe. Für diese Fälle ist nun durchgängig
`GUI.tappedAlwaysHintButton()` erforderlich. Der Wiring-Test kontrolliert
die Nutzung in 19 Activities. Offline-Activities ohne diese Touch-Hitbox
bleiben davon unberührt.

Die SDL-Abnahme überprüft echte Touch-Events in **Probe Sniffer** und
**Network Monitor** inklusive Detailansicht, Zurück, Verlassen, BMPs
und Radio-Cleanup. Die übrigen Activities haben die korrekte
Trefferzonen-Verwendung, aber noch **keinen jeweils eigenen
vollständigen Touch-E2E-Test**. Besonders BLE-GATT-Untermenüs und
spezielle Gesten benötigen weiterhin separate Tests.

Von den ursprünglich 26 betrachteten Biscuit-Activities werden
25 mit ihrer ursprünglichen Implementierung ausgeführt. `FIRE` erhält
auf dem nativen Host lediglich eine separat kompilierte, sichere
UI-Preview; seine Hardware- und Angriffsausführung bleibt ausgeschlossen.
Mesh Chat nutzt ausschließlich virtuelle ESP-NOW-Pakete.

### Testverschärfung: Touch-Listenauswahl

Der Probe-Sniffer-Touchtest tippt ausdrücklich **Zeile 2** (die
Broadcast-SSID) statt der zunächst markierten ersten Zeile und verlangt
den Zustandsmarker `PROBE:touch-row:1`. Damit wird nicht nur irgendein
Touch-Ereignis, sondern die Änderung der tatsächlichen Listenauswahl
geprüft. Danach folgen die Touch-Schaltflächen für Detail, Zurück und Exit.

### Clock-Golden: Zustände strikt trennen

Ein früherer Screenshot-Golden-Test erwartete die Uhr **nach** einer
simulierten 650-ms-`ENTER`-Betätigung. Seit der Korrektur der
Tastendruckdauer wird dieser Druck richtig als langer Confirm erkannt
und wechselt tatsächlich zur Stoppuhr. Die Pixelabweichung ist also
eine echte **Zustandsänderung**, kein Uhren- oder Grafikfehler.

Der ursprünglich gewählte Aufnahmezeitpunkt von 1600 ms lag jedoch
noch innerhalb des CrossPoint-Bootbildschirms (CI #40018588442).
Daher wird `CLOCK.bmp` nun bei 4300 ms **nach dem Boot**, aber **vor**
dem auf 5300 ms verlegten langen Tastendruck aufgenommen. Die bereits
freigegebene Clock-Pixelreferenz wird nicht ersetzt.
`CLOCK_stopwatch.bmp` dokumentiert den zweiten Zustand bei 7200 ms. Der E2E-Test verlangt unterschiedliche Bilder
und den expliziten Activity-Zustandsmarker `CLOCK:mode:stopwatch`.
Die Golden-Referenz wird nicht unkontrolliert neu berechnet.

## Mesh Chat: virtueller ESP-NOW-Transport (Simulator-only)

`MESH CHAT` verwendet den originalen `MeshChatActivity`-Code mit
synthetischem ESP-NOW-Transport aus `sim-stubs/esp_now.h`.
Die Simulation liefert zwei Präsenz-Beacons und Chat-Nachrichten über
den registrierten Empfangs-Callback. Die originale Peer-Liste und der
Nachrichtenringpuffer bleiben aktiv. Ausgehende Pakete werden nur
gezählt, niemals versendet. Szenarien:
`CROSSPLAY_SIM_MESH_SCENARIO=normal|empty|malformed`.

Die SDL-Abnahme prüft Chat- und Peer-Ansichten, Rücknavigation sowie
Radio-Shutdown. Ein zusätzlicher X4-Pro-Touch-E2E-Test bedient die
On-Screen-Flächen Peers, Relay, Back und Exit, inklusive Radio-Cleanup.
Host-Tests kontrollieren ausschließlich virtuelle
ESP-NOW-Zustellung, virtuelle Sendezählung und Deinitialisierung.
Eine erfolgreich grüne Pipeline ist für die Abnahme noch erforderlich.
Keine echten MACs, keine Funkübertragung und keine Host-Sockets.

## FIRE: UI-Abnahme ohne Funk- oder Angriffs-Backend

`FIRE` nutzt unter `SIMULATOR` eine gesonderte
`src/apps_local/fire/FireSimActivity.cpp`-Implementierung **derselben**
`FireActivity`-Klasse für die Menüzustände, Bestätigung, Vorschau,
Abbruch, Ergebnisse und Touch-/Tastenbedienung. Jede angezeigte Funktion
ist als **SIMULATION** gekennzeichnet. Der Paket-Sendezähler bleibt immer
**0** und es gibt keine Verbindung zu Host-WLAN, Bluetooth, Live-Targets,
EAPOL/PMKID-Capture oder einem ESP32-Transmitter. Die Firmware baut
weiterhin das originale `FireActivity.cpp`.

**Wichtig:** Die Fire-UI-Preview ist keine Ausführung oder Validierung
der echten Funkangriffs-, Relay-, Portal- und Capture-Engines. In der
26er-Abdeckungszahl bedeutet `FIRE` ausdrücklich nur UI/Lifecycle;
alle anderen 25 behalten ihre reale Activity-Logik mit synthetischen
Ein-/Ausgaben. Echte RF-Funktion ist von dieser CI grundsätzlich nicht
abgedeckt. Die neue Firewall-Isolation wird durch Host-Wiring und SDL-
Smoke-Tests mit Zustandsprotokoll, BMPs, Touch und `no-radio` geprüft.
