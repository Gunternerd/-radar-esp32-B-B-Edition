// Beispiel: src/secret.h (nicht ins Repo committen!)
// Kopiere diese Datei nach src/secret.h und trage deine Werte ein.
#pragma once

// WiFi (nur Fallback; normalerweise via Web-Provisioning gesetzt)
#define WIFI_SSID "MeinWLAN"
#define WIFI_PASS "MeinPasswort"

// Provisioning Access Point
#define PROV_AP_SSID "Radar-Setup"
#define PROV_AP_PASS "radar1234"

// Telegram Bot
#define BOT_TOKEN "<TELEGRAM_BOT_TOKEN>"
#define ADMIN_ID "<TELEGRAM_CHAT_ID>"

// Google Apps Script
#define G_SCRIPT_ID "<SPREADSHEET_OR_SCRIPT_ID_OPTIONAL>"
#define SECRET_GOOGLE_URL "https://script.google.com/macros/s/<DEPLOYMENT_ID>/exec"

// Google Drive & Email (falls vom Script genutzt)
#define DRIVE_ROOT_FOLDER_ID "<DRIVE_FOLDER_ID>"
#define NOTIFY_EMAIL         "<EMAIL_OPTIONAL>"
