# Radar 2.0 – Zwischenstand / Ideen

Stand: 2026-06-02

## Was bereits umgesetzt wurde

### Projektkopie
- Das Originalprojekt `nur_radar` wurde nicht verändert.
- Alle neuen Arbeiten wurden in `Radar 2.0` gemacht.

### Google-Sheets-basierte Radar-Konfiguration
In `Radar 2.0/src/main.cpp` wurde eine erste Konfig-Struktur eingebaut:
- `angle_min`
- `angle_max`
- `min_speed_kmh`
- `sample_window_ms`
- `sensor_range_min`
- `sensor_range_max`
- `sensor_sensitivity`

Dazu gibt es in der Weboberfläche eine neue Seite:
- `/radar_config`

Aktuell möglich:
- Werte im Webformular anzeigen
- Werte nach Google Apps Script senden
- Werte von Google Apps Script laden
- `min_speed_kmh` und `sample_window_ms` werden bereits im ESP-Code verwendet

Noch nicht vollständig umgesetzt:
- echte Winkel-Auswertung im ESP
- direkte Übernahme aller Parameter in die LD2451-Sensorkonfiguration

### Google Apps Script erweitert
Datei:
- `Radar 2.0/Google scripts online/radar_google_script.gs`

Neu ergänzt:
- Sheet `RadarConfig`
- `GET_RADAR_CONFIG`
- `SET_RADAR_CONFIG`

Hinweis:
- Das Script im Google-Konto muss mit diesen Änderungen neu deployed/aktualisiert werden, damit der ESP die neuen Actions nutzen kann.

### OTA / Web-Updater
In `Radar 2.0/src/main.cpp` wurde ein Web-Updater eingebaut:
- Seite `/update`
- Upload einer `firmware.bin`
- OTA-Flash mit `Update.h`
- Neustart nach erfolgreichem Update

### OTA-Partitionslayout
Neue Datei:
- `Radar 2.0/partitions_ota.csv`

In `Radar 2.0/platformio.ini` gesetzt:
- `board_build.partitions = partitions_ota.csv`

Damit ist das Projekt auf OTA vorbereitet.

## Build-Stand
Lokaler Build wurde erfolgreich ausgeführt:

```bash
platformio run -e esp32-wrover-mgn4r8
```

Ergebnis:
- Build erfolgreich
- erzeugte Firmware-Datei:
  - `Radar 2.0/.pio/build/esp32-wrover-mgn4r8/firmware.bin`

## Idee für zukünftigen Workflow
In Zukunft Updates so machen:

1. lokal bauen
2. Datei verwenden:
   - `Radar 2.0/.pio/build/esp32-wrover-mgn4r8/firmware.bin`
3. im Browser aufrufen:
   - `http://<esp-ip>/update`
4. `firmware.bin` hochladen

## Sinnvolle nächste Schritte

### 1. OTA auf echter Hardware testen
Prüfen:
- Weboberfläche erreichbar
- `/update` funktioniert
- `firmware.bin` lässt sich erfolgreich installieren
- Gerät startet korrekt neu

### 2. Google Apps Script wirklich aktualisieren
Prüfen:
- `GET_RADAR_CONFIG` liefert JSON
- `SET_RADAR_CONFIG` speichert Werte ins Sheet `RadarConfig`
- ESP kann Werte erfolgreich laden

### 3. Radar-Konfiguration technisch weiter verdrahten
Noch offen:
- `sensor_range_min`
- `sensor_range_max`
- `sensor_sensitivity`
- `angle_min`
- `angle_max`

Mögliche Umsetzung:
- direkte Anbindung an `configureLd2451()`
- oder Software-Filter im ESP, falls der Sensorwinkel nicht direkt sauber setzbar ist

### 4. Radar-Datenformat sauber verifizieren
Aktuell ist die Geschwindigkeitsauswertung noch heuristisch.
Später prüfen:
- echtes LD2451-Payloadformat
- ob Winkel-/Positionsdaten im Frame vorhanden sind
- daraus bessere Richtung/Winkel-Filter bauen

### 5. Optional: Update-Seite verbessern
Spätere Verbesserungen:
- Upload-Fortschritt anzeigen
- Versionsanzeige auf der Webseite
- Sicherheitsabfrage / Passwortschutz für Update-Seite

## Wichtige Dateien
- `Radar 2.0/src/main.cpp`
- `Radar 2.0/platformio.ini`
- `Radar 2.0/partitions_ota.csv`
- `Radar 2.0/Google scripts online/radar_google_script.gs`

## Neuer Fortschritt vom heutigen Hardware-Test

### ESP erfolgreich geflasht
- `Radar 2.0` wurde erfolgreich auf den ESP geflasht.
- Serieller Monitor zeigte sauberen Start.
- WLAN-Verbindung erfolgreich.
- Webserver erfolgreich gestartet.
- Weboberfläche erreichbar unter:
  - `http://192.168.178.100/`

### Google-Anbindung erfolgreich
- Mess-Upload an Google funktioniert.
- Telegram-Versand funktioniert.
- Neue Radar-Konfiguration über Google Apps Script funktioniert jetzt ebenfalls.
- Im ESP-Log erschien erfolgreich:
  - `[RADAR CFG] loaded from Google`

### Wichtige Änderung
- `Radar 2.0/src/secret.h` wurde auf die neue Web-App-URL umgestellt.
- Aktive Google Web-App URL:
  - `https://script.google.com/macros/s/AKfycbxL2TXzRBR08rT2LHokWkxKOgmR-NERGRT0e7-h6X_zzJCugbmqD2bAPBJcRBw7qaTp/exec`

### Radar aktuell nicht angeschlossen
- Keine Sensordaten erwartet.
- Log mit `bytes=0` und `frames=0` ist deshalb aktuell normal.

## Erkenntnisse zum geplanten Aufbau
- Sensor steht etwa `1 m` wegen Gehweg von der Straße entfernt.
- Sensor wird später seitlich am Straßenrand mit festem Winkel montiert.
- Geplanter fester Montagewinkel: `12°` zur Straßenmitte.
- Ziel: Fahrzeuge auf der Straße mittig erfassen, Gehweg und Laterne möglichst aus dem Hauptfokus heraushalten.

Daraus abgeleitet:
- Distanzfilter sind wahrscheinlich wichtiger als freie Winkelverstellung.
- Ein späterer fester Montagewert wie `mount_angle_deg` wäre sinnvoll.
- Aktueller geplanter Startwert für `mount_angle_deg`: `12`.
- `angle_min` / `angle_max` sollten eher als Sichtfeld-/Filterbereich verstanden werden, nicht als mechanische Drehung.
- Priorität später eher auf:
  - Distanzbereich
  - Empfindlichkeit
  - Mindestgeschwindigkeit
  - Richtung

## Empfehlung für morgen
Sinnvoller nächster Schritt:
1. OTA-Upload / Web-Update im Browser testen
2. Limit-Einstellung / Limit-Button in der Weboberfläche ergänzen
3. PDF-Erzeugung per Button in der Weboberfläche ergänzen
4. Weboberfläche sprachlich an den realen Aufbau anpassen
5. neuen Konfigwert `mount_angle_deg` ergänzen und initial auf `12` vorbereiten
6. mögliche SD-Karten-Konfiguration / Reaktivierung prüfen
7. später mit angeschlossenem Radar echte Sensordaten prüfen

## Hinweis für den nächsten Termin
Wenn wir weitermachen, sinnvoller Einstieg:
1. OTA-Upload über `/update` praktisch testen
2. Limit-Einstellung in der Weboberfläche planen/einbauen
3. PDF-Button in der Weboberfläche planen/einbauen
4. prüfen, ob `/radar_config` und `/update` im Browser wie erwartet aussehen
5. mögliche SD-Karten-Konfiguration / Reaktivierung besprechen und testen
6. Web-Konfiguration auf Montage-/Distanzlogik anpassen
7. sobald Radar angeschlossen ist: Parser und Sensor-Daten testen

---

## Neuer Fortschritt – 2026-06-01 (Abend)

### Firmware/Web-UI
- Neuer Konfigwert `mount_angle_deg` end-to-end umgesetzt
  - ESP: `RadarConfig.mountAngleDeg`, Standard `12`, in `normalizeRadarConfig()` begrenzt
  - Web-UI `/radar_config`: neues Feld „Montagewinkel Grad“
  - Google GET/SET: `mount_angle_deg` wird geladen/gespeichert
- Versionsanzeige in der Web-UI (alle Seiten): `FW Radar 2.0 • Build <Datum Zeit>`
- Limit-Funktion ergänzt
  - Neuer Konfigwert `speed_limit_kmh` (Default `50`), persistiert via Google
  - Startseite: Schnellbuttons `10/20/30/50/70 km/h` (POST `/limit_set`)
  - Upload nutzt das gesetzte Limit (`uploadToGoogleSheet(..., limit= speed_limit_kmh, ...)`)
- PDF-Export ergänzt
  - Buttons: „Tages-PDF erzeugen“ (`/pdf_daily`) und „2h-PDF erzeugen“ (`/pdf_2h`)
  - Apps Script liefert Direkt‑Download‑Links; PDF wird in Drive gespeichert und ist per Link lesbar

### Google Apps Script
- `getDefaultRadarConfig()` und `setRadarConfig()` um `speed_limit_kmh` erweitert
- Neue Helper-Funktion `publishAndGetDirectDownload(file)`
  - setzt Freigabe „Anyone with the link, View“
  - gibt Direkt‑Download‑URL `https://drive.google.com/uc?export=download&id=<ID>` zurück
- `createDailySummaryPdf()` und `createTwoHourSummaryPdf()` liefern nun Direkt‑Download‑URLs
- Aktives Deployment: Version 71
  - Web-App URL: `https://script.google.com/macros/s/AKfycbwLnJ6wB8q0ujjkRzrnlxXLTfzn0EllU8K7hBc2hdhsEQKZUwgMscnz_Kc76sdzS3lW/exec`
- Firmware `secret.h` auf diese URL aktualisiert und erfolgreich gebaut

### Validierung/Status
- Build erfolgreich, OTA‑Update empfohlen über `/update`
- Nach OTA:
  - `/radar_config` zeigt „Montagewinkel Grad“ und „Limit km/h“
  - Startseite zeigt Schnellbuttons und PDF‑Buttons
  - PDF‑Links laden direkt die Datei herunter; Datei liegt zusätzlich im Drive‑Ordner (Auto‑Cleanup 2 Tage für 2h‑PDFs bleibt bestehen)

### Offene Punkte (für den nächsten Termin)
1. Telegram: Funktionen/Flows in der UI (Testbutton ist vorhanden; Logik verfeinern)
2. Menü/Struktur der Web‑UI verbessern
3. Winkel-/Distanz‑Filterung im ESP, sobald Payload‑Felder verifiziert
4. Optionale Verdrahtung `sensor_range_*`/`sensor_sensitivity` in `configureLd2451()`
5. Optional: Fehlerdetailanzeige beim Speichern (HTTP‑Code/Response) in der Web‑UI

### Empfehlung für den nächsten Termin (aktualisiert)
1. Telegram‑Funktionen und Benachrichtigungen konkretisieren/umsetzen
2. Menü/Startseite strukturieren (Schnellaktionen, Status, Konfiguration klar trennen)
3. Mit angeschlossenem Radar: Live‑Frames sammeln und Winkel-/Distanzfelder verifizieren
4. Danach Software‑Filter (Winkel relativ zu `mount_angle_deg`) aktivieren

## Neuer Fortschritt – 2026-06-02

### Deployment/Anbindung
- Google Web‑App auf Version 72 aktualisiert
  - URL: `https://script.google.com/macros/s/AKfycbwBwtXaDgF_cweqUkRXTLrd9pvIriqO_c0LkafVvO23VBBT_1eL9sIoHe7dGKayXu4Q/exec`
- Firmware `secret.h` auf die neue URL gesetzt und erfolgreich gebaut (für OTA bereit)

### Konfiguration/Bedienung
- Richtungs‑Override ergänzt (Übergangslösung bis echte Richtungsbits verifiziert)
  - Optionen in `/radar_config`: Auto, Anfahrt, Wegfahrt
  - Bei „Auto“ bleibt Verhalten unverändert; ansonsten wird die gewählte Richtung hochgeladen
- Montageempfehlung konkretisiert (Sensor 3,5 m seitlich zur Spurmitte)
  - Zielwinkel für ca. 10 m Voraus: `~19°` (in `mount_angle_deg` dokumentieren)
  - Startwerte: `sensor_range_min=4`, `sensor_range_max=16`, `sensor_sensitivity=4`, `min_speed_kmh=6`

### Hinweise
- PDF‑Erzeugung: Datei wird im Drive abgelegt, Link ist Direkt‑Download; Cleanup für 2h‑PDFs bleibt bestehen
- Nächste Schwerpunkte unverändert: Telegram‑Flows und Menü/Struktur der Web‑UI

## Neuer Fortschritt – 2026-07-02

### Radar-Hardware
- Radarsensor angeschlossen und läuft einwandfrei (echte Sensordaten kommen jetzt an)

### Richtungserkennung komplett entfernt
Grund: Automatische Richtungserkennung hat nie funktioniert (immer „unknown“), daher raus statt Workaround.
- `src/main.cpp`: `directionOverride`-Konfigfeld, Web-UI-Auswahl in `/radar_config`, Google GET/SET, `directionToText()`, Telegram-/Log-Ausgabe sowie die nie befüllten Variablen `gRadarDirection`/`gWindowDirection` entfernt
- `Google scripts online/radar_google_script.gs` und `report.gs`: `direction`/`direction_override`-Parameter und die Spalte „Richtung“ aus `DATA_HEADERS`/`appendMeasurementRow` entfernt
- `README.md`: Doku zu `direction_override` gestrichen
- Nebenbei zwei verwaiste `uploadToGoogleSheet(...)`-Aufrufe mit altem 4. Parameter gefixt (hätten sonst den Build gebrochen)

### Geplanter Neustart (Stabilität)
- Neue Funktion `checkScheduledRestart()` in `main.cpp`, aufgerufen in `loop()`
- ESP startet automatisch alle ~48h nachts um 2 Uhr neu (Workaround, da das Gerät nach ~8 Tagen Dauerbetrieb sonst selbst abstürzt)
- Zeitpunkt/Intervall wird per `Preferences` (NVS) persistiert, übersteht also normale Neustarts

### Google Apps Script Deployment
- Neues Deployment: Version 76 (02.07.2026)
  - URL: `https://script.google.com/macros/s/AKfycbwQHhYzdYRlQq93dP75U8MatpkaKjUdOQuweHWEoNkFMueMMLqJNel2KJZPq-e8shju/exec`
- `src/secret.h` (`SECRET_GOOGLE_URL`) auf diese URL aktualisiert

### Offener Punkt: PDF-Erzeugung schlägt fehl
- `createTwoHourSummaryPdf` / `cleanupOldTwoHourPdfs` werfen `Exception: Access denied: DriveApp.`
- Vermutete Ursache: OAuth-Scope im Apps-Script-Manifest zu eng (`drive.file` statt `drive`) für Zugriff auf den bestehenden Ordner `DRIVE_ROOT_FOLDER_ID`, oder Skript wurde nach Scope-/Code-Änderung noch nicht neu autorisiert
- Noch zu prüfen: `appsscript.json` (Manifest) im Apps-Script-Editor ansehen und ggf. `oauthScopes` erweitern, danach `testCreate2hPdf` manuell ausführen und Berechtigung neu bestätigen

### Firmware-Build noch ausstehend
- Lokaler Build mit den obigen Änderungen (Richtung raus, geplanter Neustart) noch nicht gebaut/geflasht
- Befehl im Projektordner `Y:\Radar 2.0`:
  ```powershell
  platformio run -e esp32-wrover-mgn4r8
  ```
- Ergebnis: `.pio/build/esp32-wrover-mgn4r8/firmware.bin`, danach per `http://<esp-ip>/update` (OTA) hochladen

### Empfehlung für nächstes Mal
1. Firmware bauen und per OTA aufspielen (Richtungsentfernung + 48h-Neustart aktivieren)
2. DriveApp-Berechtigung im Apps-Script-Manifest klären, PDF-Erzeugung erneut testen
3. Danach: Telegram‑Flows und Menü/Struktur der Web‑UI (weiterhin offen)
