# Biscuit im X4-Pro-Simulator – Integrationsplan

## Aktueller Integrationsstand (23.10.2026)

Der native X4-Pro-Simulator führt **15 echte Biscuit-Activities** aus:

| Gruppe | Activities |
| --- | --- |
| Offline/System | CLOCK, DEVICE INFO, OTP GENERATOR |
| WLAN/Netzwerk | WIFI CONNECT, DNS LOOKUP, PING, HOST SCANNER, HTTP CLIENT, MDNS BROWSER, PROBE SNIFFER, CROWD DENSITY |
| BLE | BLE PROXIMITY, BLE SCANNER, BLE CONTACT, TRACKER DETECT |

Die ursprünglichen No-op-VTables der fünfzehn Activities wurden entfernt.
Ihre realen Implementierungen und Shelf-Einträge werden auf dem Simulator
kompiliert. Hardwarepfade bleiben für das Firmware-Build erhalten.

Letzte erfolgreich bestätigte Pipeline:
[CI #39984886630](https://github.com/0b-ivan/inx-4x-pro/actions/runs/39984886630)
(14 Activities). Die Crowd-Density-Erweiterung benötigt noch eine eigene CI-Bestätigung.

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

- Autostart und nichtleere Screenshots aller fünfzehn Apps
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
