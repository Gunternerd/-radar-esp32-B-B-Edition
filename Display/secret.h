// Dieses File sollte nicht zum Git Repo hinzugefügt werden.
// Es enthält alle "geheimen" Daten.
#pragma once

// ── WiFi Konfiguration (mittels Provisioning über Web‑AP wird das Netzwerk gesetzt) ──
#define WIFI_SSID "FRITZ!Box 7493"
#define WIFI_PASS "10715cids!000000"

// ── Provisioning Access Point (lokaler Setup‑AP wenn kein WLAN konfiguriert ist) ──
#define PROV_AP_SSID "Radar-Setup"
#define PROV_AP_PASS "radar1234"

// ── Telegram Bot Konfiguration ──
#define BOT_TOKEN "8422826492:AAH5fwUKB4VKCGSs2DabWxZPN50AZYObz00"
#define ADMIN_ID "656440105"

// ── Google Apps Script Konfiguration ──
#define G_SCRIPT_ID "1frmQbtIDsq1X7W6Ga9idWJ3d0ZaBep6AU57KMOySUyPdRIjbgAvfRsRI"
#define SECRET_GOOGLE_URL "https://script.google.com/macros/s/AKfycbwjOIGWbCBanQJOisgh5ZTwVsUqXSFlKnK2x6LmUmeg_vpOzM1r_KklGeSqZ2sJzBCB/exec"

// ── Google Drive & Email Konfiguration (wird in Radar_Google_Script.gs verwendet) ──
#define DRIVE_ROOT_FOLDER_ID "1h5YkXeq3YcGe6gRqLClrvDPrHFCL7rQd"
#define NOTIFY_EMAIL         "1gnt23@googlemail.com"
