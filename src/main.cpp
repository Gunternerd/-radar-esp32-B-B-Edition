// Radar-ESP Firmware
// Hardware: ESP32-WROVER MGN4R8, Hi-Link LD2451, SD card on VSPI
//
// Aufgabe:
// - LD2451 Sensordaten auswerten
// - alle Upload-Messwerte lokal als CSV auf SD-Karte speichern
// - dieselben Messwerte an Google Apps Script senden
//
// Web‑Provisioning: startet bei fehlender WiFi‑Konfiguration als AP und bietet eine Weboberfläche
// zum Auswählen/Eintragen von SSID/Passwort und zum Löschen (Format-ähnlich) der SD-Karte.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <time.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

#include "secret.h"

#ifndef FW_VERSION
#define FW_VERSION "Radar 2.0"
#endif
#define FW_BUILD __DATE__ " " __TIME__

// Hardware pins
// ESP32-WROVER uses GPIO16/GPIO17 internally for PSRAM.
#define SENSOR_RX_PIN      26
#define SENSOR_TX_PIN      27
#define SD_CS_PIN           5
#define OLED_SDA_PIN       21
#define OLED_SCL_PIN       22
#define WAKE_BUTTON_PIN    32

// Configuration
#define SENSOR_BAUD        115200
#define SAMPLE_WINDOW_MS   3000
#define WIFI_TIMEOUT_MS    4000
#define RADAR_CONFIG_REFRESH_MS 300000UL
// Mount point will be /sdcard; files stored under that path
#define LOG_FILE_PATH      "/radar_log.csv"
#define WIFI_CFG_PATH      "/wifi.cfg"
#define OLED_WIDTH         128
#define OLED_HEIGHT        64
#define OLED_ADDR         0x3C
#define OLED_ALT_ADDR     0x3D
#define OLED_REFRESH_MS    500
#define DISPLAY_SLEEP_MS  300000
#define BUTTON_DEBOUNCE_MS  50
#define LD2451_CONFIG_ACK_TIMEOUT_MS 300
#define LD2451_CONFIG_INTER_CMD_DELAY_MS 20

// Geplanter Neustart: ESP läuft nach ~8 Tagen ohne Neustart instabil,
// daher alle 48h nachts um 2 Uhr automatisch neu starten.
#define SCHEDULED_RESTART_HOUR 2
#define SCHEDULED_RESTART_MIN_INTERVAL_S (47UL * 3600UL) // knapp unter 48h als Sicherheitsmarge
#define SCHEDULED_RESTART_CHECK_MS 20000UL

// NTP
static const char* NTP_SERVER    = "pool.ntp.org";
static const long  GMT_OFFSET_S  = 3600;   // UTC+1
static const int   DST_OFFSET_S  = 3600;   // Sommerzeit
static const char* TZ_BERLIN = "CET-1CEST,M3.5.0,M10.5.0/3";

// Observed radar binary frame
static const uint8_t LD_HEADER[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t LD_TAIL[4] = {0xF8, 0xF7, 0xF6, 0xF5};
static const uint8_t LD_CONFIG_HEADER[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t LD_CONFIG_TAIL[4] = {0x04, 0x03, 0x02, 0x01};
static const int MAX_PAYLOAD_BYTES = 64;
static const unsigned long RADAR_DEBUG_INTERVAL_MS = 2000;

enum ParseState {
  SEEK_HEADER,
  COLLECT_PAYLOAD
};

static ParseState gParseState = SEEK_HEADER;
static int gHeaderMatch = 0;
static int gTailMatch = 0;
static uint8_t gPayload[MAX_PAYLOAD_BYTES];
static int gPayloadLen = 0;

static int gSpeedKmh = 0;
static int gWindowMaxKmh = 0;
static unsigned long gWindowStartMs = 0;
static unsigned long gLastRadarFrameMs = 0;
static unsigned long gRadarFrameCount = 0;
static unsigned long gRadarBytesSeen = 0;
static unsigned long gParserResetCount = 0;
static unsigned long gLengthErrorCount = 0;
static unsigned long gHeaderSeenCount = 0;
static unsigned long gLastRadarDebugMs = 0;
static unsigned long gLastDisplayUpdateMs = 0;
static unsigned long gLastDisplayWakeMs = 0;
static unsigned long gLastButtonEventMs = 0;
static bool gSdReady = false;
static bool gDisplayReady = false;
static bool gDisplaySleeping = false;
static bool gWakeButtonWasPressed = false;
static int gLastHttpCode = 0;
static uint8_t gDisplayAddr = OLED_ADDR;

static bool gProvisioningActive = false;
static WebServer gWebServer(80);
static DNSServer gDnsServer;
static bool gUpdateInProgress = false;
static size_t gUpdateBytesWritten = 0;

struct RadarConfig {
  int angleMinDeg;
  int angleMaxDeg;
  int minSpeedKmh;
  int sampleWindowMs;
  int sensorRangeMax;
  int sensorRangeMin;
  int sensorSensitivity;
  int mountAngleDeg; // fester Montagewinkel zur Straßenmitte (Software-Filter)
  int speedLimitKmh; // Geschwindigkeitslimit für Upload/Auswertung
};

static RadarConfig gRadarConfig = {-45, 45, 1, SAMPLE_WINDOW_MS, 20, 2, 4, 12, 50};
static unsigned long gLastRadarConfigRefreshMs = 0;
static bool gRadarConfigLoadedFromGoogle = false;

HardwareSerial SensorSerial(2);
Adafruit_SSD1306 Display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

static void printPayloadHex(const uint8_t* data, int len);

static int16_t readInt16LE(const uint8_t* data) {
  return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool sendLd2451Command(const uint8_t* command, size_t length) {
  const size_t written = SensorSerial.write(command, length);
  SensorSerial.flush();
  return written == length;
}

static bool waitForLd2451Ack(uint8_t expectedCmd, unsigned long timeoutMs = LD2451_CONFIG_ACK_TIMEOUT_MS) {
  const unsigned long startMs = millis();
  uint8_t header[4] = {0};
  int headerMatch = 0;

  while (millis() - startMs < timeoutMs) {
    while (SensorSerial.available() > 0) {
      const uint8_t b = (uint8_t)SensorSerial.read();
      if (b == LD_CONFIG_HEADER[headerMatch]) {
        header[headerMatch++] = b;
        if (headerMatch == 4) {
          while (SensorSerial.available() < 2 && millis() - startMs < timeoutMs) {
            delay(1);
          }
          if (SensorSerial.available() < 2) {
            return false;
          }

          uint8_t lenLo = (uint8_t)SensorSerial.read();
          uint8_t lenHi = (uint8_t)SensorSerial.read();
          const int ackLen = (int)((uint16_t)lenLo | ((uint16_t)lenHi << 8));
          if (ackLen <= 0 || ackLen > MAX_PAYLOAD_BYTES) {
            return false;
          }

          uint8_t payload[MAX_PAYLOAD_BYTES] = {0};
          int readCount = 0;
          while (readCount < ackLen && millis() - startMs < timeoutMs) {
            if (SensorSerial.available() > 0) {
              payload[readCount++] = (uint8_t)SensorSerial.read();
            } else {
              delay(1);
            }
          }
          if (readCount < ackLen) {
            return false;
          }

          uint8_t tail[4] = {0};
          int tailRead = 0;
          while (tailRead < 4 && millis() - startMs < timeoutMs) {
            if (SensorSerial.available() > 0) {
              tail[tailRead++] = (uint8_t)SensorSerial.read();
            } else {
              delay(1);
            }
          }
          if (tailRead < 4) {
            return false;
          }

          if (payload[0] == expectedCmd) {
            Serial.print("[LD2451 ACK] cmd=0x");
            Serial.print(expectedCmd, HEX);
            Serial.print(" result=");
            Serial.println((ackLen >= 3 && payload[2] == 0) ? "OK" : "FAIL");
            return ackLen >= 3 && payload[2] == 0;
          }

          headerMatch = 0;
        }
      } else {
        headerMatch = (b == LD_CONFIG_HEADER[0]) ? 1 : 0;
      }
    }
    delay(1);
  }

  Serial.print("[LD2451 ACK] timeout cmd=0x");
  Serial.println(expectedCmd, HEX);
  return false;
}

static bool configureLd2451() {
  const uint8_t enableConfig[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
  const uint8_t setDetection[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x06, 0x00, 0x02, 0x00, 20, 2, 5, 1, 0x04, 0x03, 0x02, 0x01};
  const uint8_t setSensitivity[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x06, 0x00, 0x03, 0x00, 2, 4, 0, 0, 0x04, 0x03, 0x02, 0x01};
  const uint8_t endConfig[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01};

  Serial.println("[LD2451 CFG] start");
  while (SensorSerial.available() > 0) {
    SensorSerial.read();
  }

  if (!sendLd2451Command(enableConfig, sizeof(enableConfig)) || !waitForLd2451Ack(0xFF)) {
    Serial.println("[LD2451 CFG] enable config failed");
    return false;
  }
  delay(LD2451_CONFIG_INTER_CMD_DELAY_MS);

  if (!sendLd2451Command(setDetection, sizeof(setDetection)) || !waitForLd2451Ack(0x02)) {
    Serial.println("[LD2451 CFG] detection params failed");
    return false;
  }
  delay(LD2451_CONFIG_INTER_CMD_DELAY_MS);

  if (!sendLd2451Command(setSensitivity, sizeof(setSensitivity)) || !waitForLd2451Ack(0x03)) {
    Serial.println("[LD2451 CFG] sensitivity params failed");
    return false;
  }
  delay(LD2451_CONFIG_INTER_CMD_DELAY_MS);

  if (!sendLd2451Command(endConfig, sizeof(endConfig)) || !waitForLd2451Ack(0xFE)) {
    Serial.println("[LD2451 CFG] end config failed");
    return false;
  }

  Serial.println("[LD2451 CFG] done");
  return true;
}

static String currentTimestamp() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 2000)) {
    // If NTP not yet available, return millis() for debug
    return String(millis());
  }

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
  return String(buffer);
}

// forward declaration for functions defined later
static bool connectWiFiWith(const String& ssid, const String& pass, unsigned long timeoutMs /*= WIFI_TIMEOUT_MS*/);
static int uploadToGoogleSheet(int speed, int limit, const char* source);
static bool loadRadarConfigFromGoogle();
static bool saveRadarConfigToGoogle();
static void handleRadarConfigPage();
static void handleRadarConfigSave();
static void handleUpdatePage();
static void handleUpdateUpload();
static void sendTelegramMessage(const String &text);

// Deep diagnostics for TTGO WROVER
static void runWroverDeepDiagnostics() {
  Serial.println("\n=== ESP32-WROVER Deep WiFi Diagnostics ===");

  // Basic chip info
  Serial.print("Chip Model: "); Serial.println(ESP.getChipModel());
  Serial.print("Revision: "); Serial.println(ESP.getChipRevision());
  Serial.print("Flash Size: "); Serial.println(ESP.getFlashChipSize());
  Serial.print("STA MAC: "); Serial.println(WiFi.macAddress());

  // Scan networks
  Serial.println("\nScanning WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);

  int n = WiFi.scanNetworks();
  Serial.printf("Found %d networks\n", n);

  int fritzIndex = -1;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int ch = WiFi.channel(i);
    int rssi = WiFi.RSSI(i);
    int enc = WiFi.encryptionType(i);
    Serial.printf("%2d: %s  RSSI:%d  CH:%d  ENC:%d\n", i, ssid.c_str(), rssi, ch, enc);

    if (ssid.startsWith("FRITZ") || ssid.startsWith("Fritz") || ssid.indexOf("7493") >= 0) {
      fritzIndex = i;
    }
  }

  if (fritzIndex < 0) {
    Serial.println("WARN: Keine FRITZ!Box im Scan gefunden!");
  } else {
    int ch = WiFi.channel(fritzIndex);
    Serial.printf("Found FRITZ!Box on channel %d\n", ch);
    if (ch >= 12) {
      Serial.println("!!! WARN: Router Kanal >=12 kann in manchen Regionen/Boards Probleme machen (5GHz/Regulatory).\nBitte 2.4GHz Kanal <=11 einstellen.");
    }
  }

  // Try a brief connect using secret.h for additional info
  Serial.println("Attempting brief connect to secret.h credentials...");
  bool ok = connectWiFiWith(String(WIFI_SSID), String(WIFI_PASS), 3000);
  if (ok) {
    Serial.println("Diagnostics: Connected using secret.h credentials");
    WiFi.disconnect();
  } else {
    Serial.println("Diagnostics: Failed to connect using secret.h");
  }

  Serial.println("=== End Deep Diagnostics ===\n");
}

// WiFi diagnostic helper: prints MAC, scan results and attempts a short connect to secret.h
static void runWifiDiagnostics() {
  // run deep diagnostics first
  runWroverDeepDiagnostics();

  Serial.println("=== WiFi Diagnostics (summary) ===");
  Serial.print("STA MAC: "); Serial.println(WiFi.macAddress());

  Serial.println("Scanning for networks...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.print("Networks found: "); Serial.println(n);
  for (int i = 0; i < n; ++i) {
    Serial.print(i+1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (RSSI "); Serial.print(WiFi.RSSI(i));
    Serial.print(" dBm) channel:"); Serial.print(WiFi.channel(i));
    Serial.print(" enc:"); Serial.println(WiFi.encryptionType(i));
    delay(10);
  }

  Serial.println("Attempting brief connect to secret.h credentials...");
  bool ok = connectWiFiWith(String(WIFI_SSID), String(WIFI_PASS), 3000);
  if (ok) {
    Serial.println("Diagnostics: Connected using secret.h credentials");
    WiFi.disconnect();
  } else {
    Serial.println("Diagnostics: Failed to connect using secret.h");
  }
  Serial.println("=== End Diagnostics ===");
}

// Connect using given credentials (non-blocking-ish with timeout)
static bool connectWiFiWith(const String& ssid, const String& pass, unsigned long timeoutMs = WIFI_TIMEOUT_MS) {
  if (ssid.length() == 0) return false;
  Serial.print("WiFi: try connect to '"); Serial.print(ssid); Serial.println("'");
  // fully disconnect, erase config from RAM, restore wifi driver and pause briefly
  WiFi.disconnect(true, true);
  esp_wifi_restore();
  delay(300);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: "); Serial.println(WiFi.localIP());
    return true;
  }
  Serial.print("WiFi failed, status="); Serial.println(WiFi.status());
  return false;
}

// Load/save WiFi credentials from Preferences (non-SD persistent storage)
static bool loadWifiFromSd(String& outSsid, String& outPass) {
  Preferences prefs;
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  if (ssid.length() == 0) return false;
  outSsid = ssid;
  outPass = pass;
  Serial.print("Loaded WiFi from Preferences: "); Serial.println(outSsid);
  return true;
}

static bool saveWifiToSd(const String& ssid, const String& pass) {
  Preferences prefs;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  Serial.println("Saved WiFi credentials to Preferences");
  return true;
}

// Recursively remove files/directories under path; returns number of removed entries
static int removeRecursive(const String& path) {
  int removed = 0;
  File dir = SD_MMC.open(path.c_str());
  if (!dir) return 0;
  File entry;
  while ((entry = dir.openNextFile())) {
    String name = String(path);
    if (!name.endsWith("/")) name += "/";
    name += entry.name();
    if (entry.isDirectory()) {
      removed += removeRecursive(name);
      // try to remove directory
      if (SD_MMC.rmdir(name.c_str())) removed++;
    } else {
      if (SD_MMC.remove(name.c_str())) removed++;
    }
    entry.close();
  }
  dir.close();
  return removed;
}

static String htmlPage(const String& body) {
  return String("<!doctype html><html><head><meta charset='utf-8'><meta name=viewport content='width=device-width,initial-scale=1'/>"
                "<title>Radar Setup</title>"
                "<style>"
                "body{font-family:Arial,sans-serif;max-width:760px;margin:20px auto;padding:0 16px;background:#f6f7fb;color:#222;}"
                "h1,h2{margin:0 0 10px 0;}"
                ".card{background:#fff;border:1px solid #ddd;border-radius:10px;padding:14px;margin:12px 0;box-shadow:0 1px 3px rgba(0,0,0,0.05);}"
                ".nav{display:flex;gap:8px;flex-wrap:wrap;}"
                "button,input,select{font-size:16px;padding:10px 12px;margin:4px 0;border-radius:8px;border:1px solid #bbb;}"
                "button{background:#1f6feb;color:#fff;border:none;cursor:pointer;}"
                "button:hover{background:#1757b3;}"
                "a{display:inline-block;text-decoration:none;}"
                ".muted{color:#666;font-size:14px;}"
                ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;}"
                ".big{font-size:28px;font-weight:bold;}"
                "</style></head><body>") + body + "</body></html>";
}

static String navLinks() {
  String s = String("<div class='card nav'><a href='/'><button>Home</button></a>"
                "<a href='/wifi'><button>WiFi</button></a>"
                "<a href='/radar_config'><button>Radar Config</button></a>"
                "<a href='/update'><button>Update</button></a>"
                "<a href='/diag'><button>Diagnose</button></a>"
                "<a href='/status'><button>Status</button></a></div>");
  s += String("<p class='muted'>FW ") + String(FW_VERSION) + String(" • Build ") + String(FW_BUILD) + String("</p>");
  return s;
}

static int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

static void normalizeRadarConfig() {
  gRadarConfig.angleMinDeg = clampInt(gRadarConfig.angleMinDeg, -90, 90);
  gRadarConfig.angleMaxDeg = clampInt(gRadarConfig.angleMaxDeg, -90, 90);
  if (gRadarConfig.angleMinDeg > gRadarConfig.angleMaxDeg) {
    const int tmp = gRadarConfig.angleMinDeg;
    gRadarConfig.angleMinDeg = gRadarConfig.angleMaxDeg;
    gRadarConfig.angleMaxDeg = tmp;
  }

  gRadarConfig.minSpeedKmh = clampInt(gRadarConfig.minSpeedKmh, 0, 200);
  gRadarConfig.sampleWindowMs = clampInt(gRadarConfig.sampleWindowMs, 500, 15000);
  gRadarConfig.sensorRangeMin = clampInt(gRadarConfig.sensorRangeMin, 0, 100);
  gRadarConfig.sensorRangeMax = clampInt(gRadarConfig.sensorRangeMax, 1, 100);
  if (gRadarConfig.sensorRangeMin > gRadarConfig.sensorRangeMax) {
    const int tmp = gRadarConfig.sensorRangeMin;
    gRadarConfig.sensorRangeMin = gRadarConfig.sensorRangeMax;
    gRadarConfig.sensorRangeMax = tmp;
  }
  gRadarConfig.sensorSensitivity = clampInt(gRadarConfig.sensorSensitivity, 1, 9);
  gRadarConfig.mountAngleDeg = clampInt(gRadarConfig.mountAngleDeg, -90, 90);
  gRadarConfig.speedLimitKmh = clampInt(gRadarConfig.speedLimitKmh, 0, 200);
}

static bool parseBoolFromPayload(const String& payload) {
  return payload.indexOf("\"ok\":true") >= 0 || payload.indexOf("\"success\":true") >= 0;
}

static bool loadRadarConfigFromGoogle() {
#ifndef SECRET_GOOGLE_URL
  Serial.println("SECRET_GOOGLE_URL not defined - cannot load radar config");
  return false;
#else
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  String url = String(SECRET_GOOGLE_URL) + "?action=GET_RADAR_CONFIG";
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.print("[RADAR CFG] GET failed, code=");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("[RADAR CFG] JSON parse failed: ");
    Serial.println(err.c_str());
    return false;
  }

  gRadarConfig.angleMinDeg = doc["angle_min"] | gRadarConfig.angleMinDeg;
  gRadarConfig.angleMaxDeg = doc["angle_max"] | gRadarConfig.angleMaxDeg;
  gRadarConfig.minSpeedKmh = doc["min_speed_kmh"] | gRadarConfig.minSpeedKmh;
  gRadarConfig.sampleWindowMs = doc["sample_window_ms"] | gRadarConfig.sampleWindowMs;
  gRadarConfig.sensorRangeMin = doc["sensor_range_min"] | gRadarConfig.sensorRangeMin;
  gRadarConfig.sensorRangeMax = doc["sensor_range_max"] | gRadarConfig.sensorRangeMax;
  gRadarConfig.sensorSensitivity = doc["sensor_sensitivity"] | gRadarConfig.sensorSensitivity;
  gRadarConfig.mountAngleDeg = doc["mount_angle_deg"] | gRadarConfig.mountAngleDeg;
  gRadarConfig.speedLimitKmh = doc["speed_limit_kmh"] | gRadarConfig.speedLimitKmh;
  normalizeRadarConfig();
  gRadarConfigLoadedFromGoogle = true;
  gLastRadarConfigRefreshMs = millis();
  Serial.println("[RADAR CFG] loaded from Google");
  return true;
#endif
}

static bool saveRadarConfigToGoogle() {
#ifndef SECRET_GOOGLE_URL
  Serial.println("SECRET_GOOGLE_URL not defined - cannot save radar config");
  return false;
#else
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  normalizeRadarConfig();

  HTTPClient http;
  String url = String(SECRET_GOOGLE_URL) + "?action=SET_RADAR_CONFIG"
               "&angle_min=" + String(gRadarConfig.angleMinDeg) +
               "&angle_max=" + String(gRadarConfig.angleMaxDeg) +
               "&min_speed_kmh=" + String(gRadarConfig.minSpeedKmh) +
               "&sample_window_ms=" + String(gRadarConfig.sampleWindowMs) +
               "&sensor_range_min=" + String(gRadarConfig.sensorRangeMin) +
               "&sensor_range_max=" + String(gRadarConfig.sensorRangeMax) +
               "&sensor_sensitivity=" + String(gRadarConfig.sensorSensitivity) +
               "&mount_angle_deg=" + String(gRadarConfig.mountAngleDeg) +
               "&speed_limit_kmh=" + String(gRadarConfig.speedLimitKmh);
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int httpCode = http.GET();
  const String payload = (httpCode > 0) ? http.getString() : String();
  http.end();

  const bool ok = (httpCode == 200) && parseBoolFromPayload(payload);
  Serial.print("[RADAR CFG] save code=");
  Serial.print(httpCode);
  Serial.print(" payload=");
  Serial.println(payload);
  if (ok) {
    gRadarConfigLoadedFromGoogle = true;
    gLastRadarConfigRefreshMs = millis();
  }
  return ok;
#endif
}

static void handleRadarConfigPage() {
  String body = navLinks();
  body += "<h1>Radar Konfiguration</h1>";
  body += "<div class='card'><p class='muted'>Werte werden über Google Apps Script / Google Sheets geladen und gespeichert. Winkel wirken aktuell als Software-Filter im ESP.</p>";
  body += "<p>Quelle: "; body += (gRadarConfigLoadedFromGoogle ? "Google geladen" : "Default/Fallback"); body += "</p>";
  body += "<form method='POST' action='/radar_config/save'>";
  body += "Min Winkel (Grad)<br><input name='angle_min' type='number' min='-90' max='90' value='" + String(gRadarConfig.angleMinDeg) + "'><br>";
  body += "Max Winkel (Grad)<br><input name='angle_max' type='number' min='-90' max='90' value='" + String(gRadarConfig.angleMaxDeg) + "'><br>";
  body += "Min Geschwindigkeit km/h<br><input name='min_speed_kmh' type='number' min='0' max='200' value='" + String(gRadarConfig.minSpeedKmh) + "'><br>";
  body += "Messfenster ms<br><input name='sample_window_ms' type='number' min='500' max='15000' value='" + String(gRadarConfig.sampleWindowMs) + "'><br>";
  body += "Sensor Distanz min<br><input name='sensor_range_min' type='number' min='0' max='100' value='" + String(gRadarConfig.sensorRangeMin) + "'><br>";
  body += "Sensor Distanz max<br><input name='sensor_range_max' type='number' min='1' max='100' value='" + String(gRadarConfig.sensorRangeMax) + "'><br>";
  body += "Empfindlichkeit<br><input name='sensor_sensitivity' type='number' min='1' max='9' value='" + String(gRadarConfig.sensorSensitivity) + "'><br>";
  body += "Montagewinkel Grad<br><input name='mount_angle_deg' type='number' min='-90' max='90' value='" + String(gRadarConfig.mountAngleDeg) + "'><br>";
  body += "Limit km/h<br><input name='speed_limit_kmh' type='number' min='0' max='200' value='" + String(gRadarConfig.speedLimitKmh) + "'><br>";
  body += "<button type='submit'>Speichern</button></form>";
  body += "<form method='POST' action='/radar_config/reload'><button type='submit'>Neu von Google laden</button></form>";
  body += "</div>";
  gWebServer.send(200, "text/html", htmlPage(body));
}

static void handleRadarConfigSave() {
  if (gWebServer.method() != HTTP_POST) {
    gWebServer.send(405, "text/plain", "Method not allowed");
    return;
  }

  gRadarConfig.angleMinDeg = gWebServer.arg("angle_min").toInt();
  gRadarConfig.angleMaxDeg = gWebServer.arg("angle_max").toInt();
  gRadarConfig.minSpeedKmh = gWebServer.arg("min_speed_kmh").toInt();
  gRadarConfig.sampleWindowMs = gWebServer.arg("sample_window_ms").toInt();
  gRadarConfig.sensorRangeMin = gWebServer.arg("sensor_range_min").toInt();
  gRadarConfig.sensorRangeMax = gWebServer.arg("sensor_range_max").toInt();
  gRadarConfig.sensorSensitivity = gWebServer.arg("sensor_sensitivity").toInt();
  gRadarConfig.mountAngleDeg = gWebServer.arg("mount_angle_deg").toInt();
  gRadarConfig.speedLimitKmh = gWebServer.arg("speed_limit_kmh").toInt();
  normalizeRadarConfig();

  const bool ok = saveRadarConfigToGoogle();
  String body = navLinks();
  body += "<div class='card'><h2>Radar Konfiguration</h2>";
  body += ok ? "<p>Gespeichert.</p>" : "<p>Speichern fehlgeschlagen. Werte nur lokal im RAM aktualisiert.</p>";
  body += "<p><a href='/radar_config'><button>Zurück</button></a></p></div>";
  gWebServer.send(200, "text/html", htmlPage(body));
}

static void handleUpdatePage() {
  String body = navLinks();
  body += "<h1>Firmware Update</h1>";
  body += "<div class='card'><p class='muted'>Hier kann künftig die mit PlatformIO erzeugte <code>firmware.bin</code> hochgeladen werden.</p>";
  body += "<p>Bitte nur Dateien für dieses Board und dieses Projekt verwenden.</p>";
  body += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  body += "<input type='file' name='firmware' accept='.bin,application/octet-stream'><br>";
  body += "<button type='submit'>Firmware hochladen</button></form></div>";
  gWebServer.send(200, "text/html", htmlPage(body));
}

static void handleUpdateUpload() {
  HTTPUpload& upload = gWebServer.upload();

  if (upload.status == UPLOAD_FILE_START) {
    gUpdateInProgress = true;
    gUpdateBytesWritten = 0;
    Serial.printf("[OTA] Update start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    } else {
      gUpdateBytesWritten += upload.currentSize;
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    const bool ok = Update.end(true);
    gUpdateInProgress = false;
    String body = navLinks();
    body += "<div class='card'><h2>Firmware Update</h2>";
    if (ok) {
      body += "<p>Update erfolgreich. Bytes: " + String((unsigned long)gUpdateBytesWritten) + "</p>";
      body += "<p>ESP startet neu...</p>";
      body += "<script>setTimeout(function(){ window.location.href='/' }, 8000);</script>";
      gWebServer.send(200, "text/html", htmlPage(body + "</div>"));
      delay(300);
      ESP.restart();
    } else {
      body += "<p>Update fehlgeschlagen.</p></div>";
      gWebServer.send(500, "text/html", htmlPage(body));
      Update.printError(Serial);
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    gUpdateInProgress = false;
    Update.abort();
    Serial.println("[OTA] Update aborted");
  }
}

static void handleRoot() {
  String body = navLinks();
  body += "<script>setTimeout(function(){ window.location.reload(); }, 5000);</script>";
  body += "<h1>Radar ESP</h1><p class='muted'>Startseite aktualisiert sich alle 5 Sekunden automatisch.</p>";
  body += "<div class='grid'>";
  body += "<div class='card'><h2>Radar</h2><p class='big'>" + String(gSpeedKmh) + " km/h</p>";
  body += "<p>Status: "; body += (millis() - gLastRadarFrameMs < 2000 ? "aktiv" : "warte"); body += "</p></div>";
  body += "<div class='card'><h2>Netzwerk</h2>";
  body += "<p>WLAN: "; body += (WiFi.status() == WL_CONNECTED ? "verbunden" : "offline/AP"); body += "</p>";
  body += "<p>IP: "; body += WiFi.localIP().toString(); body += "</p></div>";
  body += "<div class='card'><h2>Upload</h2>";
  body += "<p>Letzter Code: "; body += String(gLastHttpCode); body += "</p>";
  body += "<p>Zeit: "; body += currentTimestamp(); body += "</p>";
  body += "<p>Radar-Konfig: "; body += (gRadarConfigLoadedFromGoogle ? "Google" : "Default"); body += "</p>";
  body += "<p>Limit: "; body += String(gRadarConfig.speedLimitKmh); body += " km/h</p>";
  body += "</div>";

  body += "<div class='card'><h2>Schnellaktionen</h2>";
  body += "<p>Limit setzen: ";
  const int limits[5] = {10,20,30,50,70};
  for (int i=0;i<5;i++){ body += String("<form style='display:inline' method='POST' action='/limit_set'><input type='hidden' name='limit' value='") + String(limits[i]) + String("'><button type='submit'>") + String(limits[i]) + String(" km/h</button></form> "); }
  body += "</p><hr>";
  body += "<form method='POST' action='/pdf_daily'><button type='submit'>Tages-PDF erzeugen</button></form>";
  body += "<form method='POST' action='/pdf_2h'><button type='submit'>2h-PDF erzeugen</button></form>";
  body += "</div>";

  body += "<div class='card'><h2>Weitere Aktionen</h2>";
  body += "<form method='POST' action='/telegram_test'><button type='submit'>Telegram Test</button></form>";
  body += "<form method='POST' action='/google_test'><button type='submit'>Google Testupload</button></form>";
  body += "<form method='POST' action='/reboot'><button type='submit'>Neustart</button></form>";
  body += "</div>";

  body += "<div class='card'><h2>WLAN Schnellzugriff</h2>";
  body += "<p><a href='/wifi'><button>Zur WLAN-Konfiguration</button></a></p>";
  body += "<p><a href='/radar_config'><button>Radar-Konfiguration</button></a></p>";
  body += "</div>";

  gWebServer.send(200, "text/html", htmlPage(body));
}

static void handleWifiPage() {
  int n = WiFi.scanNetworks();
  String body = navLinks();
  body += "<h1>WLAN Konfiguration</h1>";
  body += "<div class='card'><form method='POST' action='/save'>SSID: <select name='ssid'>";
  for (int i = 0; i < n; ++i) {
    body += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
  }
  body += "</select><br>Passwort: <input name='pass' type='password'><br><button type='submit'>Speichern & Verbinden</button></form>";
  body += "<form method='POST' action='/clear_wifi'><button type='submit'>Gespeichertes WLAN löschen</button></form>";
  body += "</div>";
  gWebServer.send(200, "text/html", htmlPage(body));
}

static void handleDiagPage() {
  String body = navLinks();
  body += "<h1>Diagnose</h1><div class='card'>";
  body += "<p>STA MAC: "; body += WiFi.macAddress(); body += "</p>";
  body += "<p>AP MAC: "; body += WiFi.softAPmacAddress(); body += "</p>";
  body += "<p>WiFi Status: "; body += String(WiFi.status()); body += "</p>";
  body += "<p>Letzter HTTP Code: "; body += String(gLastHttpCode); body += "</p>";
  body += "<p>Radar letzte Daten vor: "; body += String(millis() - gLastRadarFrameMs); body += " ms</p>";
  body += "<p>Radar verbunden: "; body += ((millis() - gLastRadarFrameMs) < 2000 ? "ja" : "nein"); body += "</p>";
  body += "<p>Radar Frames empfangen: "; body += String(gRadarFrameCount); body += "</p>";
  body += "<p>Aktuelle Geschwindigkeit: "; body += String(gSpeedKmh); body += " km/h</p>";
  body += "<p>Min Geschwindigkeit: "; body += String(gRadarConfig.minSpeedKmh); body += " km/h</p>";
  body += "<p>Winkelbereich: "; body += String(gRadarConfig.angleMinDeg); body += " bis "; body += String(gRadarConfig.angleMaxDeg); body += " Grad</p>";
  body += "<p>Montagewinkel: "; body += String(gRadarConfig.mountAngleDeg); body += " Grad</p>";
  body += "<p>Limit: "; body += String(gRadarConfig.speedLimitKmh); body += " km/h</p>";
  body += "<p>Aktuelle Zeit: "; body += currentTimestamp(); body += "</p>";
  body += "</div>";
  gWebServer.send(200, "text/html", htmlPage(body));
}

static void handleSave() {
  if (gWebServer.method() != HTTP_POST) {
    gWebServer.send(405, "text/plain", "Method not allowed");
    return;
  }
  String ssid = gWebServer.arg("ssid");
  String pass = gWebServer.arg("pass");
  ssid.trim(); pass.trim();
  if (ssid.length() == 0) {
    gWebServer.send(400, "text/plain", "SSID empty");
    return;
  }
  bool ok = saveWifiToSd(ssid, pass); // now saves to Preferences
  if (!ok) {
    gWebServer.send(500, "text/plain", "Failed to save credentials");
    return;
  }
  // Try to connect
  gWebServer.send(200, "text/html", htmlPage("<p>Saved. Trying to connect... Rebooting.</p>"));
  delay(1000);
  // Attempt connect briefly then restart to normal mode (short timeout)
  connectWiFiWith(ssid, pass, 4000);
  ESP.restart();
}

static void handleFormat() {
  // delete files on SD (simulate format)
  if (!gSdReady) {
    gWebServer.send(500, "text/plain", "SD not ready");
    return;
  }
  int removed = removeRecursive("/");
  // try to recreate empty log file
  File f = SD_MMC.open(LOG_FILE_PATH, FILE_WRITE);
  if (f) {
    f.println("timestamp,speed_kmh,upload_status,http_code");
    f.close();
  }
  String body = "<p>Deleted " + String(removed) + " entries. Recreated log file." + "</p>";
  body += "<p><a href='/'>Back</a></p>";
  gWebServer.send(200, "text/html", htmlPage(body));
}

static bool gWebServerStarted = false;

static void startWebServer() {
  if (gWebServerStarted) {
    return;
  }

  // Register web handlers
  gWebServer.on("/", HTTP_GET, handleRoot);
  gWebServer.on("/wifi", HTTP_GET, handleWifiPage);
  gWebServer.on("/radar_config", HTTP_GET, handleRadarConfigPage);
  gWebServer.on("/update", HTTP_GET, handleUpdatePage);
  gWebServer.on("/diag", HTTP_GET, handleDiagPage);
  gWebServer.on("/favicon.ico", HTTP_GET, []() {
    gWebServer.send(204, "text/plain", "");
  });
  gWebServer.on("/save", HTTP_POST, handleSave);
  gWebServer.on("/radar_config/save", HTTP_POST, handleRadarConfigSave);
  gWebServer.on("/update", HTTP_POST, []() {}, handleUpdateUpload);
  gWebServer.on("/radar_config/reload", HTTP_POST, []() {
    const bool ok = loadRadarConfigFromGoogle();
    gWebServer.send(200, "text/html", htmlPage(navLinks() + String("<div class='card'><p>") + (ok ? "Radar-Konfiguration neu geladen." : "Laden fehlgeschlagen.") + "</p><p><a href='/radar_config'><button>Zurück</button></a></p></div>"));
  });
  gWebServer.on("/limit_set", HTTP_POST, []() {
    int limit = gWebServer.hasArg("limit") ? gWebServer.arg("limit").toInt() : gRadarConfig.speedLimitKmh;
    gRadarConfig.speedLimitKmh = limit;
    normalizeRadarConfig();
    const bool ok = saveRadarConfigToGoogle();
    String body = navLinks();
    body += "<div class='card'><h2>Limit</h2>";
    body += String("<p>Neues Limit: ") + String(gRadarConfig.speedLimitKmh) + String(" km/h</p>");
    body += ok ? "<p>In Google gespeichert.</p>" : "<p>Nicht gespeichert (offline?).</p>";
    body += "<p><a href='/'><button>Zurück</button></a></p></div>";
    gWebServer.send(200, "text/html", htmlPage(body));
  });
  gWebServer.on("/pdf_daily", HTTP_POST, []() {
    if (WiFi.status() != WL_CONNECTED) { gWebServer.send(500, "text/html", htmlPage(navLinks() + String("<div class='card'><p>Kein WLAN.</p></div>"))); return; }
    HTTPClient http; String url = String(SECRET_GOOGLE_URL) + "?action=GENERATE_DAILY_PDF&id=" + WiFi.macAddress();
    http.begin(url); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const int code = http.GET(); String payload = code>0 ? http.getString() : String(); http.end();
    // Sanitiziere Payload: trim, Quotes entfernen
    payload.trim();
    if (payload.startsWith("\"") && payload.endsWith("\"")) {
      payload = payload.substring(1, payload.length()-1);
    }
    if (code==200 && payload.startsWith("http")) {
      // Direkt zum PDF weiterleiten
      gWebServer.sendHeader("Location", payload, true);
      gWebServer.send(302, "text/plain", "Redirect to PDF");
      return;
    }
    String body = navLinks(); body += "<div class='card'><h2>Tages-PDF</h2>";
    if (payload.startsWith("http")) { body += String("<p><a href='") + payload + String("' target='_blank'>PDF öffnen</a></p>"); }
    else { body += String("<p>Fehler ") + String(code) + String("</p><pre>") + payload + String("</pre>"); }
    body += "<p><a href='/'><button>Zurück</button></a></p></div>";
    gWebServer.send(200, "text/html", htmlPage(body));
  });
  gWebServer.on("/pdf_2h", HTTP_POST, []() {
    if (WiFi.status() != WL_CONNECTED) { gWebServer.send(500, "text/html", htmlPage(navLinks() + String("<div class='card'><p>Kein WLAN.</p></div>"))); return; }
    HTTPClient http; String url = String(SECRET_GOOGLE_URL) + "?action=GENERATE_2H_PDF&id=" + WiFi.macAddress();
    http.begin(url); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const int code = http.GET(); String payload = code>0 ? http.getString() : String(); http.end();
    payload.trim();
    if (payload.startsWith("\"") && payload.endsWith("\"")) {
      payload = payload.substring(1, payload.length()-1);
    }
    if (code==200 && payload.startsWith("http")) {
      gWebServer.sendHeader("Location", payload, true);
      gWebServer.send(302, "text/plain", "Redirect to PDF");
      return;
    }
    String body = navLinks(); body += "<div class='card'><h2>2h-PDF</h2>";
    if (payload.startsWith("http")) { body += String("<p><a href='") + payload + String("' target='_blank'>PDF öffnen</a></p>"); }
    else { body += String("<p>Fehler ") + String(code) + String("</p><pre>") + payload + String("</pre>"); }
    body += "<p><a href='/'><button>Zurück</button></a></p></div>";
    gWebServer.send(200, "text/html", htmlPage(body));
  });
  gWebServer.on("/format", HTTP_POST, handleFormat);
  gWebServer.on("/clear_wifi", HTTP_POST, []() {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
    gWebServer.send(200, "text/html", htmlPage(navLinks() + String("<p>Gespeichertes WLAN gelöscht.</p>")));
  });
  gWebServer.on("/telegram_test", HTTP_POST, []() {
    sendTelegramMessage("Radar Telegram Test erfolgreich");
    gWebServer.send(200, "text/html", htmlPage(navLinks() + String("<p>Telegram Test gesendet.</p>")));
  });
  gWebServer.on("/google_test", HTTP_POST, []() {
    gLastHttpCode = uploadToGoogleSheet(77, 0, "WEBTEST");
    gWebServer.send(200, "text/html", htmlPage(navLinks() + String("<p>Google Testupload ausgelöst. HTTP Code: ") + String(gLastHttpCode) + "</p>"));
  });
  // Minimal extra endpoints
  gWebServer.on("/status", HTTP_GET, []() {
    // build status page
    String body = navLinks();
    body += "<h2>Radar Status</h2>";
    body += "<p>WiFi mode: ";
    body += (WiFi.getMode() == WIFI_AP ? "AP" : (WiFi.getMode() == WIFI_STA ? "STA" : "Unknown"));
    body += "</p>";
    body += "<p>WiFi status: ";
    body += String(WiFi.status());
    body += "</p>";
    body += "<p>STA MAC: "; body += WiFi.macAddress(); body += "</p>";
    body += "<p>IP: "; body += WiFi.localIP().toString(); body += "</p>";
    body += "<p>SD: "; body += (gSdReady ? "OK" : "missing"); body += "</p>";
    if (gSdReady && SD_MMC.exists(LOG_FILE_PATH)) {
      File f = SD_MMC.open(LOG_FILE_PATH, FILE_READ);
      if (f) {
        body += "<p>Log size: "; body += String(f.size()); body += " bytes</p>";
        f.close();
      }
    }
    body += "<p>Last speed: "; body += String(gSpeedKmh); body += " km/h</p>";
    body += "<p><a href='/download_log'>Download log</a></p>";
    body += "<form method='POST' action='/reboot'><input type='submit' value='Reboot'></form>";
    gWebServer.send(200, "text/html", htmlPage(body));
  });

  gWebServer.on("/download_log", HTTP_GET, []() {
    gWebServer.send(501, "text/plain", "SD storage disabled");
  });

  gWebServer.on("/reboot", HTTP_POST, []() {
    String body = navLinks();
    body += "<div class='card'><h2>Neustart</h2>";
    body += "<p>ESP startet neu... bitte 5-10 Sekunden warten.</p>";
    body += "<p><a href='/'><button>Zur Startseite</button></a></p>";
    body += "<script>setTimeout(function(){ window.location.href='/' }, 8000);</script>";
    body += "</div>";
    gWebServer.send(200, "text/html", htmlPage(body));
    delay(300);
    ESP.restart();
  });

  gWebServer.onNotFound([]() {
    IPAddress targetIp = gProvisioningActive ? WiFi.softAPIP() : WiFi.localIP();
    gWebServer.sendHeader("Location", String("http://") + targetIp.toString(), true);
    gWebServer.send(302, "text/plain", "Redirect");
  });
  gWebServer.begin();
  gWebServerStarted = true;
  Serial.println("Web server started");
}

static void startProvisioningAP() {
  Serial.println("Starting provisioning AP");
  WiFi.mode(WIFI_AP);
  const char* apSsid = "Radar-Setup";
  const char* apPass = "radar1234";
  WiFi.softAP(apSsid, apPass);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(apIP);

  gDnsServer.start(53, "*", apIP);
  gProvisioningActive = true;
  startWebServer();
}

static void stopProvisioningAP() {
  gProvisioningActive = false;
  gWebServer.stop();
  gDnsServer.stop();
  WiFi.softAPdisconnect(true);
}

static void connectWiFi() {
  // legacy: try secret.h credentials
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi: already connected");
    return;
  }
  connectWiFiWith(String(WIFI_SSID), String(WIFI_PASS));
}

static void initTime() {
  configTzTime(TZ_BERLIN, NTP_SERVER);
}

// Prüft nachts um 2 Uhr, ob seit dem letzten Neustart bereits ~48h vergangen sind,
// und startet den ESP dann geplant neu (Workaround gegen Absturz nach ~8 Tagen Laufzeit).
static void checkScheduledRestart() {
  static unsigned long lastCheckMs = 0;
  const unsigned long nowMs = millis();
  if (nowMs - lastCheckMs < SCHEDULED_RESTART_CHECK_MS) {
    return;
  }
  lastCheckMs = nowMs;

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 100)) {
    return; // Zeit noch nicht per NTP synchronisiert
  }

  if (timeInfo.tm_hour != SCHEDULED_RESTART_HOUR || timeInfo.tm_min != 0) {
    return;
  }

  Preferences prefs;
  prefs.begin("sys", false);
  const uint32_t lastRestart = prefs.getUInt("lastRestart", 0);
  const uint32_t nowEpoch = (uint32_t)time(nullptr);

  if (nowEpoch - lastRestart < SCHEDULED_RESTART_MIN_INTERVAL_S) {
    prefs.end();
    return;
  }

  prefs.putUInt("lastRestart", nowEpoch);
  prefs.end();

  Serial.println("[SYS] Geplanter Neustart (48h-Zyklus, 2 Uhr)");
  sendTelegramMessage("Radar ESP: geplanter Neustart (48h-Zyklus)");
  delay(500);
  ESP.restart();
}

// SD usage disabled: we will store WiFi creds in Preferences instead
static void initSdCard() {
  Serial.println("SD storage disabled in firmware configuration");
  gSdReady = false;
}

static void initDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  gDisplayAddr = OLED_ADDR;
  gDisplayReady = Display.begin(SSD1306_SWITCHCAPVCC, gDisplayAddr);
  if (!gDisplayReady) {
    gDisplayAddr = OLED_ALT_ADDR;
    gDisplayReady = Display.begin(SSD1306_SWITCHCAPVCC, gDisplayAddr);
  }

  if (!gDisplayReady) {
    Serial.println("OLED init failed");
    return;
  }

  Display.clearDisplay();
  Display.setTextColor(SSD1306_WHITE);
  Display.setTextSize(1);
  Display.setCursor(0, 0);
  Display.println("Radar startet...");
  Display.print("OLED 0x");
  Display.println(gDisplayAddr, HEX);
  Display.display();
  gLastDisplayWakeMs = millis();
}

static void updateDisplay(bool force = false) {
  if (!gDisplayReady || gDisplaySleeping) {
    return;
  }

  if (!force && millis() - gLastDisplayUpdateMs < OLED_REFRESH_MS) {
    return;
  }
  gLastDisplayUpdateMs = millis();

  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  const bool radarRecent = millis() - gLastRadarFrameMs < 2000;

  Display.clearDisplay();
  Display.setTextColor(SSD1306_WHITE);

  Display.setTextSize(1);
  Display.setCursor(0, 0);
  Display.print("Radar: ");
  Display.println(radarRecent ? "OK" : "warte");

  Display.setCursor(0, 12);
  Display.print("Frame: ");
  Display.print(millis() - gLastRadarFrameMs);
  Display.println(" ms");

  Display.setCursor(0, 24);
  Display.print("WLAN : ");
  Display.println(wifiOk ? "OK" : "offline");

  Display.setCursor(0, 36);
  Display.print("Up   : ");
  Display.println(gLastHttpCode == 0 ? "-" : String(gLastHttpCode));

  Display.setTextSize(2);
  Display.setCursor(0, 48);
  Display.print(gSpeedKmh);
  Display.print(" km/h");

  Display.display();
}

static void sleepDisplay() {
  if (!gDisplayReady || gDisplaySleeping) {
    return;
  }

  Display.clearDisplay();
  Display.display();
  Display.ssd1306_command(SSD1306_DISPLAYOFF);
  gDisplaySleeping = true;
}

static void wakeDisplay() {
  if (!gDisplayReady) {
    return;
  }

  Display.ssd1306_command(SSD1306_DISPLAYON);
  gDisplaySleeping = false;
  gLastDisplayWakeMs = millis();
  updateDisplay(true);
}

static void handleDisplayPower() {
  const bool buttonPressed = digitalRead(WAKE_BUTTON_PIN) == LOW;
  const unsigned long now = millis();

  if (buttonPressed && !gWakeButtonWasPressed && now - gLastButtonEventMs >= BUTTON_DEBOUNCE_MS) {
    gLastButtonEventMs = now;
    wakeDisplay();
  }

  gWakeButtonWasPressed = buttonPressed;

  if (!gDisplaySleeping && now - gLastDisplayWakeMs >= DISPLAY_SLEEP_MS) {
    sleepDisplay();
  }
}

static int decodeSpeedFromPayload(const uint8_t* data, int len) {
  if (len <= 0) {
    return 0;
  }

  // Beobachtetes Idle-Frame: 00 00 0F
  // Solange das echte Bewegungsformat noch verifiziert wird,
  // behandeln wir sehr kurze/all-zero-ish Frames als Leerlauf.
  if (len <= 3) {
    return 0;
  }

  // Heuristik: suche das letzte plausible Geschwindigkeitsbyte im Payload.
  // So sehen wir bei Bewegung sofort Werte, auch wenn die exakte Feldposition
  // noch nicht final dokumentiert ist.
  for (int i = len - 1; i >= 0; --i) {
    const uint8_t candidate = data[i];
    if (candidate > 0 && candidate <= 120) {
      Serial.print("[RADAR TARGET] speedCandidate=");
      Serial.print(candidate);
      Serial.print(" index=");
      Serial.print(i);
      Serial.print(" payload=");
      printPayloadHex(data, len);
      Serial.println();
      return (int)candidate;
    }
  }

  return 0;
}

static void printPayloadHex(const uint8_t* data, int len) {
  for (int i = 0; i < len; i++) {
    if (data[i] < 16) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    if (i < len - 1) {
      Serial.print(' ');
    }
  }
}

static void processPayload(const uint8_t* data, int len) {
  static int debugFramesPrinted = 0;

  gLastRadarFrameMs = millis();
  gRadarFrameCount++;

  const int decodedSpeed = decodeSpeedFromPayload(data, len);
  gSpeedKmh = (decodedSpeed >= gRadarConfig.minSpeedKmh) ? decodedSpeed : 0;

  if (debugFramesPrinted < 40) {
    Serial.print("[RADAR FRAME] len=");
    Serial.print(len);
    Serial.print(" payload=");
    printPayloadHex(data, len);
    Serial.print(" decodedSpeed=");
    Serial.println(decodedSpeed);
    debugFramesPrinted++;
  }
}

static void resetParser() {
  gParseState = SEEK_HEADER;
  gHeaderMatch = 0;
  gTailMatch = 0;
  gPayloadLen = 0;
  gParserResetCount++;
}

static void tryDecodeSlidingWindowFrame(const uint8_t* data, int len) {
  (void)data;
  (void)len;
  // deaktiviert: hat ungeframe-te Rohbytes faelschlich als gueltige Geschwindigkeiten interpretiert
}

static void readSensorSerial() {
  static uint8_t recentBytes[16] = {0};
  static int recentCount = 0;

  while (SensorSerial.available() > 0) {
    const uint8_t b = (uint8_t)SensorSerial.read();
    gRadarBytesSeen++;

    if (recentCount < (int)sizeof(recentBytes)) {
      recentBytes[recentCount++] = b;
    } else {
      for (size_t i = 1; i < sizeof(recentBytes); ++i) {
        recentBytes[i - 1] = recentBytes[i];
      }
      recentBytes[sizeof(recentBytes) - 1] = b;
    }
    // tryDecodeSlidingWindowFrame(recentBytes, recentCount); // vorerst deaktiviert

    if (gParseState == SEEK_HEADER) {
      if (b == LD_HEADER[gHeaderMatch]) {
        gHeaderMatch++;
        if (gHeaderMatch == 4) {
          gHeaderSeenCount++;
          gParseState = COLLECT_PAYLOAD;
          gPayloadLen = 0;
          gTailMatch = 0;
        }
      } else {
        gHeaderMatch = (b == LD_HEADER[0]) ? 1 : 0;
      }
      continue;
    }

    if (gPayloadLen >= MAX_PAYLOAD_BYTES) {
      gLengthErrorCount++;
      Serial.print("[RADAR] payload overflow, recent=");
      printPayloadHex(recentBytes, recentCount);
      Serial.println();
      resetParser();
      continue;
    }

    gPayload[gPayloadLen++] = b;

    if (b == LD_TAIL[gTailMatch]) {
      gTailMatch++;
      if (gTailMatch == 4) {
        const int payloadWithoutTailLen = gPayloadLen - 4;
        if (payloadWithoutTailLen >= 0) {
          processPayload(gPayload, payloadWithoutTailLen);
        }
        resetParser();
      }
    } else {
      gTailMatch = (b == LD_TAIL[0]) ? 1 : 0;
    }
  }

  const unsigned long now = millis();
  if (now - gLastRadarDebugMs >= RADAR_DEBUG_INTERVAL_MS) {
    gLastRadarDebugMs = now;
    Serial.print("[RADAR DEBUG] bytes=");
    Serial.print(gRadarBytesSeen);
    Serial.print(" headers=");
    Serial.print(gHeaderSeenCount);
    Serial.print(" frames=");
    Serial.print(gRadarFrameCount);
    Serial.print(" lenErr=");
    Serial.print(gLengthErrorCount);
    Serial.print(" resets=");
    Serial.print(gParserResetCount);
    Serial.print(" speed=");
    Serial.println(gSpeedKmh);
  }
}

// --- HTTP-Upload an Google Sheet ---
static int uploadToGoogleSheet(int speed, int limit, const char* source) {
#ifndef SECRET_GOOGLE_URL
    Serial.println("SECRET_GOOGLE_URL not defined - skipping uploadToGoogleSheet");
    return -2;
#else
    if (WiFi.status() != WL_CONNECTED) return -1;

    const int maxRetries = 3;
    int attempt = 0;
    int lastHttpCode = -1;
    while (attempt < maxRetries) {
        attempt++;
        HTTPClient http;
        String url = String(SECRET_GOOGLE_URL) +
                     "?speed=" + String(speed) +
                     "&limit=" + String(limit) +
                     "&source=" + String(source);
        http.begin(url);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        int httpCode = http.GET();
        lastHttpCode = httpCode;
        String resp = "";
        if (httpCode > 0) {
            resp = http.getString();
        }

        Serial.print("[HTTP] Upload attempt "); Serial.print(attempt);
        Serial.print(": speed="); Serial.print(speed);
        Serial.print(" limit="); Serial.print(limit);
        Serial.print(" code="); Serial.print(httpCode);
        Serial.print(" resp="); Serial.println(resp);

        http.end();

        if (httpCode >= 200 && httpCode < 300) {
            break;
        }

        delay(500);
    }
    return lastHttpCode;
#endif
}




// Telegram helper
static void sendTelegramMessage(const String &text) {
#ifndef BOT_TOKEN
  Serial.println("BOT_TOKEN not defined - skipping Telegram message");
  return;
#else
  if (WiFi.status() != WL_CONNECTED) {
    // try to connect briefly
    connectWiFi();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram: no WiFi - cannot send message");
    return;
  }

  // Support mehrere Empfänger: ADMIN_ID kann kommaseparierte Liste enthalten
  String adminList = String(ADMIN_ID);
  adminList.trim();
  if (adminList.length() == 0) {
    Serial.println("Telegram: ADMIN_ID empty");
    return;
  }

  // Escape-Text vorbereiten
  String esc;
  for (size_t i = 0; i < text.length(); ++i) {
    char c = text.charAt(i);
    if (c == '\\' || c == '"') esc += '\\';
    esc += c;
  }

  int sent = 0;
  int start = 0;
  while (start < adminList.length()) {
    int comma = adminList.indexOf(',', start);
    String chatId = (comma < 0) ? adminList.substring(start) : adminList.substring(start, comma);
    chatId.trim();
    start = (comma < 0) ? adminList.length() : comma + 1;
    if (chatId.length() == 0) continue;

    HTTPClient http;
    String url = String("https://api.telegram.org/bot") + String(BOT_TOKEN) + "/sendMessage";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String payload = String("{\"chat_id\":\"") + chatId + "\",\"text\":\"" + esc + "\"}";
    int httpCode = http.POST(payload);
    Serial.print("Telegram to "); Serial.print(chatId); Serial.print(" code: "); Serial.println(httpCode);
    http.end();
    sent++;
  }
  if (sent == 0) {
    Serial.println("Telegram: no valid chat ids in ADMIN_ID");
  }
#endif
}

static void appendMeasurementToSd(const String& timestamp, int speedKmh, bool uploadOk, int httpCode) {
  // SD storage disabled — do nothing
  (void)timestamp; (void)speedKmh; (void)uploadOk; (void)httpCode;
}

static void handleMeasurementWindow() {
  if (gSpeedKmh > gWindowMaxKmh) {
    gWindowMaxKmh = gSpeedKmh;
  }

  if (millis() - gWindowStartMs < (unsigned long)gRadarConfig.sampleWindowMs) {
    return;
  }

  if (gWindowMaxKmh > 0) {
    const String timestamp = currentTimestamp();
    const int httpCode = uploadToGoogleSheet(gWindowMaxKmh, gRadarConfig.speedLimitKmh, "RADAR");
    const bool uploadOk = httpCode >= 200 && httpCode < 300;
    gLastHttpCode = httpCode;

    appendMeasurementToSd(timestamp, gWindowMaxKmh, uploadOk, httpCode);

    Serial.print(timestamp);
    Serial.print(" speed=");
    Serial.print(gWindowMaxKmh);
    Serial.print("km/h upload=");
    Serial.println(httpCode);

    if (uploadOk) {
      sendTelegramMessage(String("Radar: ") + gWindowMaxKmh + " km/h um " + timestamp);
    }
  }

  gWindowMaxKmh = 0;
  gWindowStartMs = millis();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Print MAC addresses early for troubleshooting MAC-filtering on routers
  // Note: do not share your password here; only the MAC is needed for router allowlist
  Serial.println("Booting...");
  Serial.print("WiFi STA MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("WiFi AP  MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  SensorSerial.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  delay(100);
  // configureLd2451(); // vorerst deaktiviert, bis das Live-Datenformat sauber verifiziert ist
  gWindowStartMs = millis();

  initDisplay();
  updateDisplay(true);

  // Init SD early so we can read wifi creds from it
  initSdCard();

  // Try to load WiFi credentials from SD; if available, try to connect
  String ssd, pass;
  if (loadWifiFromSd(ssd, pass)) {
    if (!connectWiFiWith(ssd, pass)) {
      Serial.println("Saved WiFi credentials failed to connect");
      Serial.println("Running WiFi diagnostics...");
      runWifiDiagnostics();
      startProvisioningAP();
    } else {
      Serial.println("Connected using saved WiFi from SD");
      loadRadarConfigFromGoogle();
      startWebServer();
    }
  } else {
    // No saved creds, attempt connect with secret.h creds, else provisioning
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Initial connect with secret.h failed — running diagnostics...");
      runWifiDiagnostics();
      startProvisioningAP();
    } else {
      loadRadarConfigFromGoogle();
      startWebServer();
    }
  }

  initTime();
  updateDisplay(true);

  // Testeintrag nach Neustart
  delay(500);
  gLastHttpCode = uploadToGoogleSheet(77, 0, "TESTBOOT");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(String("Web UI: http://") + WiFi.localIP().toString() + "/");
    sendTelegramMessage(String("Radar ESP gestartet. IP: ") + WiFi.localIP().toString());
  } else if (gProvisioningActive) {
    Serial.println(String("Web UI (AP): http://") + WiFi.softAPIP().toString() + "/");
  }

  Serial.println("Radar ESP32-WROVER ready");
}

void loop() {
  if (gProvisioningActive) {
    gDnsServer.processNextRequest();
  }

  // Serve web UI in both provisioning AP mode and normal WiFi STA mode
  gWebServer.handleClient();

  if (WiFi.status() == WL_CONNECTED && millis() - gLastRadarConfigRefreshMs >= RADAR_CONFIG_REFRESH_MS) {
    loadRadarConfigFromGoogle();
  }

  readSensorSerial();
  handleMeasurementWindow();
  handleDisplayPower();
  updateDisplay();
  checkScheduledRestart();
}
