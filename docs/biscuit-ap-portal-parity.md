# Biscuit X3 → X4 Pro: AP Lab und Captive Portal

Stand: 24. Oktober 2026. Vergleich mit **0b-ivan/biscuit_x3:master**.
Integration auf **feature/biscuit-simulator-hardware** (kein zusätzlicher Branch/PR).

## AP Clone – Original versus X4 Pro

| Funktion | Original X3 | X4 Pro nach Änderung |
| --- | --- | --- |
| Asynchrones WLAN-Scanning | Ja | Ja |
| SSID/RSSI/Kanal/Verschlüsselung anzeigen | Ja | Ja; **alle** Scanergebnisse sichtbar |
| Listenbedienung | Hardwaretasten | Hardwaretasten **und** GT911: Zeilen sowie Footer-Auf/Ab |
| AP mit gleicher SSID/Kanal starten | Beliebig | Nur für explizit gekennzeichnete **X4PRO-LAB-***-Testnetze |
| Reale Client-Anzahl/Uptime/AP-IP | Ja | Ja |
| AP-Startfehler und Kanal validieren | Unvollständig | Ja |
| AP nach Exit freigeben | Ja | Ja, zusätzlich Developer-Mode-Radio-Lifecycle |

Zusätzlich steht jetzt der **lokale Test-AP \`X4PRO-LAB-LOCAL\` auf Kanal 6** direkt in der Liste zur Verfügung. So kann der AP ohne ein bereits vorhandenes Testnetz gestartet werden. Eine gescannte fremde SSID bleibt nur lesbar. Die gemeinsame Arduino-freie Prüfung in \`LabApRules.h\` validiert den erlaubten Präfix und Kanal 1–13; \`test_ap_lab.cpp\` läuft in den Host-Tests mit.

Die Lab-Whitelist ist absichtlich nicht entfernt. Fremde Infrastruktur
darf über passive Scans angezeigt, aber nicht als täuschend ähnlich benannter
Access Point nachgebildet werden.

## Captive Portal – Original versus X4 Pro

| Funktion | Original X3 | X4 Pro nach Änderung |
| --- | --- | --- |
| Vorlagen auswählen | Built-in und SD-HTML | Drei fest integrierte, eindeutig als Labor gekennzeichnete Vorlagen |
| AP-Name editieren | Frei | Benutzerdefinierter optionaler Suffix unter **X4PRO-PORTAL** |
| WLAN-SoftAP | Ja | Ja, echte ESP32-Firmware-Schnittstelle |
| Wildcard-DNS-Port 53 | Ja | Ja |
| HTTP-Webserver Port 80 | Ja | Ja |
| Captive Redirect | Ja | Ja, GET auf Root |
| HTML-Formular per POST | Login-Formular | **Synthetischer Test-ID-POST** an \`/check\` |
| Eingaben anzeigen/speichern | Original speichert Benutzernamen und Passwörter | Nein; nur Anzahl validierter Testereignisse |
| Diagnostik | Nein | GET \`/status\` liefert JSON mit Testzählern |
| Touch und Hardwaretasten | X3-spezifisch | GT911-Zeilen, Footer, Start/Stop |
| Stop und Cleanup | Ja | HTTP, DNS und AP werden freigegeben |

Drei Laborvorlagen: *Start page*, *Synthetic form test*, *HTTP diagnostics*.
Nur fiktive Codes \`X4TEST-...\` (ASCII-Buchstaben und Ziffern) werden als
Testereignis gezählt; der eigentliche Wert wird weder gespeichert noch
angezeigt. Andere Posts erhalten HTTP 400/404. Echtes Phishing, die
Nachahmung von Drittanbieterseiten, beliebiges SD-HTML mit Login-Vorlagen
und das Erfassen von Zugangsdaten sind **keine implementierte Parität**.

## Testprotokoll / Abnahme

1. Host-Test \`host-tests/biscuit-sim/test_portal_lab.cpp\`: SSID-Suffix,
   synthetische IDs, ungültige Eingaben und Grenzwerte. Eingebunden über
   \`host-tests/biscuit-sim/run.sh\`.
2. CI-SDL-Simulator: UI-Screenshots und die bestehenden 26 Activity-Smokes
   sowie fünf zusätzliche Apps. Das ist kein Test echter WLAN-Frames.
3. Hardware-AP: mit eigenem \`X4PRO-LAB-TEST\` prüfen, dass nur dieser
   Eintrag als Access Point gestartet werden kann; auf zweiten Geräten
   verbinden und Clientzähler, Kanal und Shutdown prüfen.
4. Hardware-Portal: mit \`X4PRO-PORTAL-TEST\` verbinden, HTTP GET auf
   \`192.168.4.1/\`, GET \`/status\`, POST \`test_id=X4TEST-DEMO1\` an
   \`/check\`; Falscheingaben müssen HTTP 400 erzeugen.
5. Zwei Mal hintereinander starten und stoppen, freien Heap, GT911-Events,
   AP-/DNS-/HTTP-Freigabe und Radio-Wiederherstellung prüfen.

**Keine Hardware-Abnahme durch den GitHub-Commit:** Erst Firmware-CI,
SDL-Tests und Tests mit realem X4 Pro + isoliertem Client können volle
Funktion bestätigen.

## Übrige 26 Activities

Der bestehende Satz von 26 Simulator-Smokes wurde in diesem Schritt nicht
in eine Hardware-Paritätsbescheinigung umgewandelt. Eine Activity für Activity
dokumentierte Differenzliste und Funktions-E2E-Tests sind weiterhin erforderlich.

## Nightly-Freigabe und Hardware-Regression

Die CI führt den Firmware-Build inklusive Stackbudget, Host-Tests, echten SDL-Activity-Smokes und pixelgenauen Referenzbildvergleich aus. Ein mit \`release(nightly):\` beginnender Commit löst nach **allen drei erfolgreich abgeschlossenen CI-Jobs** den bereits bestehenden geschützten GitHub-Prerelease-Job aus. Das Release ist ausschließlich ein **OTA-Testimage**, kein Stable-Release.

Die auf echter Hardware noch nachzuweisenden Szenarien umfassen: Start und Stop des lokalen AP, Scanliste und Touch-Footer, SoftAP-Client-Verbindung sowie DHCP/IP, Captive-DNS auf Port 53, HTTP GET/POST inklusive negativer Formularfälle, AP/Portal-Wechsel ohne Resource-Leak, Bulletin Board mit zwei Clients, Dead Drop über SD-I/O und Wardriving CSV mit mehrfachen Scans. Dazu gehören jeweils Timeout, Speicherfehler, mehrfache Navigation und Reboot-Rückkehr.

**Wichtig:** Simulator-Smokes bedeuten nicht, dass die vollständigen 26 Originalfunktionen und die zusätzlichen fünf Anwendungen bereits auf realer X4-Pro-Hardware abgenommen sind. Ein tatsächlicher Test mit Gerät und isoliertem Client ist für diese Freigabe weiter Voraussetzung einer Stable-Promotion.
