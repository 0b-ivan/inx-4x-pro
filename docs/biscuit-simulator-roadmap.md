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
