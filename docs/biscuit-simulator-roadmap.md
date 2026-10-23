# Biscuit im X4-Pro-Simulator – Integrationsplan

## Stand 1: echte Activity-Implementierungen

Clock, Device Info und OTP Generator laufen im nativen Build statt in
`sim_hw_stubs.cpp`. Die Apps sind im Shelf sichtbar. Das ESP32-Build bleibt
unverändert; simulierte Geräteinformationen sind ausdrücklich als solche
gekennzeichnet. Ein reproduzierbarer Zeitstempel lässt sich mit
`CROSSPLAY_SIM_EPOCH=1704067200` setzen (UTC).

`host-tests/biscuit-sim/run.sh` testet Uhrzeit-Fixierung und kontrolliert,
dass die drei Apps nie wieder durch No-op-VTables ersetzt werden.

## Weiterer Ausbau

1. Netzwerk: DNS, Ping, HTTP, mDNS, Host-Scanner, WiFi Connect – isolierter
   In-Memory-Resolver/Testserver, TCP-Zeitüberschreitungen, reproduzierbare APs.
2. BLE: Scanner, Proximity, Tracker, Contact, Beacon, Keyboard, Mesh – virtuelle
   Geräte/Advertisements, simulierte RSSI-/Timeout-Ereignisse, UI-Start/Stop.
3. WLAN-Rohframes: Probe Sniffer, Crowd Density, Deauth Detector,
   Packet Monitor, Network Monitor, Device Fingerprinter – nur synthetische
   Testframes und passive Erkennung.
4. Aktive Funk-Apps: simulierte Zustände, Abbrüche und Cleanup,
   keine echten Störframes oder Credential-Erfassung.
5. CI: Simulator mit Xvfb starten, Eingaben injecten, Zustände/Screenshots
   prüfen. Der bisherige CI-Simulatorjob baut nur, er klickt nicht.

**Tests dürfen nicht durch leere HAL-/Activity-Stubs grün werden.**
Hardwareeffekte und Funkinteroperabilität bleiben Device-Abnahmetests.

## Stand 2: virtuelles WLAN + DNS (Integration)

- WiFi Connect verwendet die bestehende CrossPoint-Simulator-WLAN-Klasse,
  einschließlich `CROSSPOINT_SIM_WIFI_CONNECT=fail|timeout|no-ssid`.
- DNS Lookup verwendet rein lokale Resolver-Fixtures:
  `example.test → 192.0.2.10`, `gateway.test → 192.0.2.1`,
  `localhost → 127.0.0.1`. Andere Hosts schlagen kontrolliert fehl.
- Der WiFi-Status kann über `CROSSPLAY_SIM_WIFI_CONNECTED=0|1` gesetzt werden.
- Beide Activities werden real kompiliert; No-op-VTables entfernt.
- Unit-, Wiring- und Buildtests vorhanden; kein SDL-UI-Smoke-Test.

- Ping ist jetzt ebenfalls eine echte native Activity. Sie prüft wie auf dem
  ESP32 **TCP-Port 80, nicht ICMP**; im Simulator werden allein
  `example.test`, `gateway.test` und `localhost` als erreichbar gemeldet.
  Alle anderen Ziele liefern Timeout, ohne ein Netzwerkpaket zu versenden.

## Headless-End-to-End-Smoke-Test

`scripts_local/biscuit_sim_smoke.sh` startet nach dem Simulator-Build
alle sechs derzeit simulierten Aktivitäten mit `CROSSPLAY_AUTOSTART`.
Der gepinnte CrossPoint-Simulator liefert automatische Tasten-Ereignisse
(`CROSSPOINT_SIM_INPUT_SCRIPT`) und BMP-Screenshots
(`CROSSPOINT_SIM_SCREENSHOTS`).
Die CI prüft Prozessende, Autostart-Log, BMP-Format und nichtleere Pixel.
Für OTP Generator wird zusätzlich ein Screenshot vor und nach `RIGHT`
verglichen. Screenshots werden als CI-Artefakt abgelegt.

Dieser Smoke-Test kontrolliert echte UI-Ausführung und einfache
Interaktionen, aber **nicht** komplette Feature-, BLE- oder RF-Interoperabilität.
