# Radar 2.0 (ESP32 + LD2451)

ESP32-basierte Radarmessung mit Weboberfläche, OTA-Updates und Google Apps Script Anbindung.

Hinweis: Dieses Repository enthält keine Geheimnisse. Trage deine lokalen Werte in `src/secret.h` ein (aus der mitgelieferten Beispiel-Datei) und deploye dein eigenes Google Apps Script.

## Features
- Web-UI auf dem ESP32: Startseite, WLAN, Radar-Konfig, Diagnose, OTA-Update
- OTA-Update per `/update` (PlatformIO `firmware.bin` hochladen)
- Konfiguration über Google Sheets/Apps Script (GET/SET)
- PDF-Export (Tages-/2h-PDF) über Apps Script, Direkt-Download-Link
- Konfigurierbare Parameter (Auszug):
  - `angle_min` / `angle_max` (Winkelfilter – noch nicht aktiv ausgewertet)
  - `mount_angle_deg` (Dokumentiert Montagewinkel; Filter folgt später)
  - `min_speed_kmh`, `sample_window_ms`
  - `sensor_range_min` / `sensor_range_max` / `sensor_sensitivity`
  - `speed_limit_kmh` (Limit, wird an Google mitgesendet)
  - `direction_override` (−1=auto, 1=approach, 0=away; Workaround bis echte Richtung verifiziert)

## Hardware
- Board: ESP32-WROVER (getestet mit MGN4R8/ESP-WROVER-KIT)
- Radar: Hi-Link LD2451 (UART)
- Optional: SSD1306 OLED I2C (Adafruit)

## Projektstruktur
```
├─ src/
│  ├─ main.cpp                 Firmware (Web-UI, Parser, Upload)
│  ├─ secret.example.h         Vorlage für lokale Geheimnisse
│  └─ secret.h                 LOKAL, NICHT COMMITTEN
├─ Google scripts online/      Apps-Script Dateien
│  ├─ radar_google_script.gs   Web-Endpoint (doGet) + PDF + Konfig
│  ├─ config.example.gs        Vorlage für Apps-Script-Konfig
│  └─ config.gs                LOKAL im Script-Projekt anlegen (nicht committen)
├─ partitions_ota.csv          OTA-Partitionslayout
├─ platformio.ini              PIO-Umgebung (esp32-wrover-mgn4r8)
└─ .github/workflows/build.yml GitHub Actions Build (optional)
```

## Quickstart (lokal)
1) Abhängigkeiten: PlatformIO CLI (oder VS Code + PIO)
2) Geheimnisse anlegen
   - `cp src/secret.example.h src/secret.h`
   - Felder ausfüllen (WLAN optional, Telegram/Google bei Bedarf)
3) Bauen:
   - `platformio run -e esp32-wrover-mgn4r8`
   - Ergebnis: `.pio/build/esp32-wrover-mgn4r8/firmware.bin`
4) Flashen per OTA:
   - ESP im WLAN erreichbar → Browser `http://<esp-ip>/update`
   - `firmware.bin` hochladen, Neustart abwarten

## Web-UI
- `/` Startseite mit Schnellaktionen (Limit setzen, PDF erzeugen)
- `/wifi` WLAN-Provisioning (Preferences im ESP)
- `/radar_config` Konfiguration (per Google GET/SET)
- `/diag` Diagnose (Status, letzte Frames etc.)
- `/update` OTA Update (Upload von `firmware.bin`)

## Google Apps Script
1) Neues Script-Projekt anlegen (Apps Script)
2) Dateien aus `Google scripts online/*.gs` übernehmen:
   - `radar_google_script.gs` einfügen
   - `config.example.gs` als `config.gs` einfügen und mit echten Werten füllen
3) Als Web-App deployen (Version anlegen)
   - Execute as: Me
   - Who has access: Anyone (Link)
   - Deployment-URL kopieren
4) In `src/secret.h` `SECRET_GOOGLE_URL` auf deine Deployment-URL setzen
5) Test im Browser:
   - `.../exec?action=GET_RADAR_CONFIG` → JSON
   - `.../exec?action=SET_RADAR_CONFIG&...` → `{ok:true,...}`

Hinweise PDF
- Apps Script erzeugt die PDF in einem Drive-Ordner und gibt einen Direkt-Download-Link zurück
- 2h-PDFs werden automatisch nach 2 Tagen in den Papierkorb verschoben (konfigurierbar)

## CI: GitHub Actions (optional)
- Workflow-Datei: `.github/workflows/build.yml`
- Baut das Projekt bei Push/PR
- Schritt: kopiert `src/secret.example.h` zu `src/secret.h`, damit der Build ohne echte Geheimnisse läuft
- Artefakt: `firmware.bin`

## Sicherheit/Geheimnisse
- `src/secret.h` und `Google scripts online/config.gs` sind in `.gitignore`
- Nutze die bereitgestellten `*.example.*`-Dateien als Vorlage

## Lizenz
Siehe [LICENSE](LICENSE) (MIT).
