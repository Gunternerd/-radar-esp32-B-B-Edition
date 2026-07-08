#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

#include <time.h>

extern HardwareSerial Serial;


#include "secret.h"



#define GFX_BL 1
#define TOUCH_ADDR 0x3B
#define TOUCH_SDA 4
#define TOUCH_SCL 8
#define TOUCH_I2C_CLOCK 400000
#define TOUCH_RST_PIN 12
#define TOUCH_INT_PIN 11
#define AXS_MAX_TOUCH_NUMBER 1

#define RADAR_RX 17

#define RADAR_TX 18



using ::Serial;
namespace {

Arduino_DataBus *bus = nullptr;
Arduino_GFX *panel = nullptr;
Arduino_Canvas *gfx = nullptr;
// HardwareSerial RadarBridge(1); // UART-Verbindung entfällt
WiFiClientSecure secureClient;
UniversalTelegramBot *bot = nullptr;

enum Page {
  PAGE_RADAR = 0,
  PAGE_SYSTEM = 1,
  PAGE_DEBUG = 2,
  PAGE_MENU = 3,
};

struct TouchButton {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  const char *label;
  const char *action;
  uint16_t fillColor;
};

int radarSpeed = 0;
int radarLimit = 50;
String radarIP;
String radarTime = "--:--";
bool radarDot = false;
bool alarmEnabled = true;
bool lastAlarmActive = false;

int lastLoggedSpeed = -1;
int lastLoggedLimit = -1;
String lastLoggedIP;
String lastLoggedTime;
bool lastLoggedDot = false;

unsigned long lastBotCheck = 0;
unsigned long lastUiRefresh = 0;
unsigned long lastClockUpdate = 0;
unsigned long lastTouchReleaseCheck = 0;
unsigned long wifiConnectStartMs = 0;

const unsigned long BOT_INTERVAL = 2000;
const unsigned long UI_INTERVAL = 250;
const unsigned long TOUCH_DEBOUNCE_MS = 180;
const int MAX_APPROVED_USERS = 12;
const int MAX_EMAIL_PENDING_USERS = 12;
const int SWIPE_MIN_DX = 60;
const int SWIPE_MAX_DY = 90;
const unsigned long SWIPE_MAX_DURATION_MS = 900;
const int SWIPE_MIN_SPEED_PX_PER_S = 90;
const unsigned long SCREEN_SAVER_TIMEOUT_MS = 5UL * 60UL * 1000UL;
const int DAILY_SUMMARY_HOUR = 0;
const int DAILY_SUMMARY_MINUTE = 1;

Page currentPage = PAGE_RADAR;
String lastStatusMessage = "Bereit";
bool touchActive = false;
uint16_t lastTouchX = 0;
uint16_t lastTouchY = 0;
uint16_t touchStartX = 0;
uint16_t touchStartY = 0;
unsigned long touchStartMs = 0;
unsigned long lastUserInteractionMs = 0;
bool screenSaverActive = false;
bool wifiConnectInProgress = false;
bool wifiConnectStarted = false;
int lastDailySummaryDayOfYear = -1;

String approvedUserIds[MAX_APPROVED_USERS];
String pendingUserIds[MAX_APPROVED_USERS];
String pendingChatIds[MAX_APPROVED_USERS];
String emailPendingUserIds[MAX_EMAIL_PENDING_USERS];
String emailPendingChatIds[MAX_EMAIL_PENDING_USERS];

void sendTelegram(const String &message);

const TouchButton menuButtons[] = {
  {10, 52, 146, 42, "+1", "limit_up_1", RGB565_DARKGREEN},
  {166, 52, 146, 42, "+5", "limit_up_5", RGB565_DARKGREEN},
  {322, 52, 146, 42, "+10", "limit_up_10", RGB565_DARKGREEN},
  {10, 104, 146, 42, "-1", "limit_down_1", RGB565_MAROON},
  {166, 104, 146, 42, "-5", "limit_down_5", RGB565_MAROON},
  {322, 104, 146, 42, "-10", "limit_down_10", RGB565_MAROON},
  {10, 156, 146, 42, "Alarm", "alarm_toggle", RGB565_NAVY},
  {166, 156, 146, 42, "Last 10", "show_last10", RGB565_PURPLE},
  {322, 156, 146, 42, "Reboot", "reboot", RGB565_RED},
  {10, 208, 110, 42, "Radar", "page_radar", RGB565_BLUE},
  {128, 208, 110, 42, "System", "page_system", RGB565_BLUE},
  {246, 208, 110, 42, "Debug", "page_debug", RGB565_BLUE},
  {364, 208, 106, 42, "Test1", "test_1kmh", RGB565_DARKCYAN},
};
// --- Last 10 Messwerte ---
#include <vector>
struct LastEntry {
  String time;
  int speed;
  int limit;
};
std::vector<LastEntry> last10Entries;
bool showLast10Window = false;

// Forward-Deklarationen für Funktionen, die im Namespace verwendet werden
extern void drawScreenFrame();
extern void drawHeader(const char *title);

namespace {
void drawLast10Window() {
  gfx->fillScreen(RGB565_BLACK);
  drawScreenFrame();
  drawHeader("Letzte 10 Messwerte");
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);
  int y = 54;
  gfx->setCursor(12, y);
  gfx->print("Zeit   Spd Lim");
  y += 22;
  for (const auto& entry : last10Entries) {
    gfx->setCursor(12, y);
    gfx->print(entry.time);
    gfx->setCursor(100, y);
    gfx->print(entry.speed);
    gfx->setCursor(170, y);
    gfx->print(entry.limit);
    y += 22;
    if (y > 300) break;
  }
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setCursor(12, 420);
  gfx->print("Zurueck: Tippen");
  gfx->flush();
}
}

bool isAdmin(const String &id) {
  return id == ADMIN_ID;
}

bool isApprovedUser(const String &id) {
  for (int i = 0; i < MAX_APPROVED_USERS; ++i) {
    if (approvedUserIds[i] == id) {
      return true;
    }
  }
  return false;
}

bool isPrivilegedUser(const String &id) {
  return isAdmin(id) || isApprovedUser(id);
}

int findPendingIndexByUser(const String &id) {
  for (int i = 0; i < MAX_APPROVED_USERS; ++i) {
    if (pendingUserIds[i] == id) {
      return i;
    }
  }
  return -1;
}

int addApprovedUser(const String &id) {
  if (id.length() == 0 || isAdmin(id) || isApprovedUser(id)) {
    return -1;
  }
  for (int i = 0; i < MAX_APPROVED_USERS; ++i) {
    if (approvedUserIds[i].length() == 0) {
      approvedUserIds[i] = id;
      return i;
    }
  }
  return -1;
}

bool addPendingRequest(const String &userId, const String &chatId) {
  if (userId.length() == 0 || isPrivilegedUser(userId)) {
    return false;
  }

  int existing = findPendingIndexByUser(userId);
  if (existing >= 0) {
    pendingChatIds[existing] = chatId;
    return false;
  }

  for (int i = 0; i < MAX_APPROVED_USERS; ++i) {
    if (pendingUserIds[i].length() == 0) {
      pendingUserIds[i] = userId;
      pendingChatIds[i] = chatId;
      return true;
    }
  }
  return false;
}

String takePendingChatId(const String &userId) {
  int idx = findPendingIndexByUser(userId);
  if (idx < 0) {
    return "";
  }
  String chatId = pendingChatIds[idx];
  pendingUserIds[idx] = "";
  pendingChatIds[idx] = "";
  return chatId;
}

int findEmailPendingIndexByUser(const String &id) {
  for (int i = 0; i < MAX_EMAIL_PENDING_USERS; ++i) {
    if (emailPendingUserIds[i] == id) {
      return i;
    }
  }
  return -1;
}

bool setEmailPending(const String &userId, const String &chatId) {
  if (userId.length() == 0) {
    return false;
  }

  int existing = findEmailPendingIndexByUser(userId);
  if (existing >= 0) {
    emailPendingChatIds[existing] = chatId;
    return true;
  }

  for (int i = 0; i < MAX_EMAIL_PENDING_USERS; ++i) {
    if (emailPendingUserIds[i].length() == 0) {
      emailPendingUserIds[i] = userId;
      emailPendingChatIds[i] = chatId;
      return true;
    }
  }
  return false;
}

bool isEmailPending(const String &userId) {
  return findEmailPendingIndexByUser(userId) >= 0;
}

String takeEmailPendingChatId(const String &userId) {
  int idx = findEmailPendingIndexByUser(userId);
  if (idx < 0) {
    return "";
  }
  String chatId = emailPendingChatIds[idx];
  emailPendingUserIds[idx] = "";
  emailPendingChatIds[idx] = "";
  return chatId;
}

bool pointInButton(uint16_t x, uint16_t y, const TouchButton &button) {
  return x >= button.x && x < button.x + button.w && y >= button.y && y < button.y + button.h;
}

bool getTouchPoint(uint16_t &x, uint16_t &y) {
  uint8_t data[AXS_MAX_TOUCH_NUMBER * 6 + 2] = {0};
  const uint8_t readCmd[11] = {
      0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00,
      static_cast<uint8_t>((AXS_MAX_TOUCH_NUMBER * 6 + 2) >> 8),
      static_cast<uint8_t>((AXS_MAX_TOUCH_NUMBER * 6 + 2) & 0xff),
      0x00, 0x00, 0x00};

  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(readCmd, sizeof(readCmd));
  if (Wire.endTransmission() != 0) {
    return false;
  }

  if (Wire.requestFrom(TOUCH_ADDR, sizeof(data)) != sizeof(data)) {
    return false;
  }

  for (size_t index = 0; index < sizeof(data); ++index) {
    data[index] = Wire.read();
  }

  if (data[1] == 0 || data[1] > AXS_MAX_TOUCH_NUMBER) {
    return false;
  }

  uint16_t rawX = ((data[2] & 0x0F) << 8) | data[3];
  uint16_t rawY = ((data[4] & 0x0F) << 8) | data[5];
  if (rawX > 500 || rawY > 500) {
    return false;
  }

  y = map(rawX, 0, 320, 320, 0);
  x = rawY;
  return true;
}

void updateClockFromNtp() {
  struct tm timeInfo;
  if (getLocalTime(&timeInfo)) {
    char buffer[6];
    strftime(buffer, sizeof(buffer), "%H:%M", &timeInfo);
    radarTime = String(buffer);
  }
}

void startWiFiConnect() {
  if (wifiConnectStarted) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiConnectStartMs = millis();
  wifiConnectInProgress = true;
  wifiConnectStarted = true;
  lastStatusMessage = "WiFi verbindet...";
  Serial.println("[WIFI] connect started");
}

void updateWiFiConnect() {
  if (!wifiConnectStarted) {
    return;
  }

  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (wifiConnectInProgress) {
      wifiConnectInProgress = false;
      radarIP = WiFi.localIP().toString();
      lastStatusMessage = "WiFi verbunden";
      Serial.println("[WIFI] connected: " + radarIP);
    }
    return;
  }

  if (!wifiConnectInProgress) {
    return;
  }

  if (millis() - wifiConnectStartMs >= 15000) {
    wifiConnectInProgress = false;
    lastStatusMessage = "WiFi Timeout";
    Serial.println("[WIFI] timeout");
  }
}

void logToSheets(const String &event) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  auto urlEncode = [](const String &value) {
    String encoded;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < value.length(); ++i) {
      uint8_t c = static_cast<uint8_t>(value[i]);
      bool isAlphaNum =
          (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
      if (isAlphaNum || c == '-' || c == '_' || c == '.' || c == '~') {
        encoded += static_cast<char>(c);
      } else {
        encoded += '%';
        encoded += hex[(c >> 4) & 0x0F];
        encoded += hex[c & 0x0F];
      }
    }
    return encoded;
  };

  HTTPClient http;
  String url = String(SECRET_GOOGLE_URL) + "?event=" + urlEncode(event) +
               "&limit=" + String(radarLimit) + "&source=DISPLAY";
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  Serial.printf("Sheets-Log '%s' -> HTTP %d\n", event.c_str(), code);
  http.end();
}

String callGoogleAction(const String &action, const String &extraQuery = "") {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }

  auto urlEncode = [](const String &value) {
    String encoded;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < value.length(); ++i) {
      uint8_t c = static_cast<uint8_t>(value[i]);
      bool isAlphaNum =
          (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
      if (isAlphaNum || c == '-' || c == '_' || c == '.' || c == '~') {
        encoded += static_cast<char>(c);
      } else {
        encoded += '%';
        encoded += hex[(c >> 4) & 0x0F];
        encoded += hex[c & 0x0F];
      }
    }
    return encoded;
  };

  String url = String(SECRET_GOOGLE_URL) + "?action=" + urlEncode(action);
  if (extraQuery.length() > 0) {
    url += "&" + extraQuery;
  }

  HTTPClient http;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  String response;
  if (code > 0) {
    response = http.getString();
  }
  Serial.printf("Google Action '%s' -> HTTP %d\n", action.c_str(), code);
  http.end();
  return response;
}

String requestDailySummaryPdf(const String &requesterId) {
  String query = "id=" + requesterId;
  String response = callGoogleAction("GENERATE_DAILY_PDF", query);
  response.trim();
  return response;
}

String requestTwoHourSummaryPdf(const String &requesterId) {
  String query = "id=" + requesterId;
  String response = callGoogleAction("GENERATE_2H_PDF", query);
  response.trim();
  return response;
}

String addTestEntryToSheets(const String &requesterId, const String &source) {
  String query = "speed=1&limit=" + String(radarLimit) + "&id=" + requesterId + "&source=" + source;
  String response = callGoogleAction("ADD_TEST_ENTRY", query);
  response.trim();
  return response;
}

void runDailySummaryScheduler() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    return;
  }

  if (timeInfo.tm_hour == DAILY_SUMMARY_HOUR &&
      timeInfo.tm_min == DAILY_SUMMARY_MINUTE &&
      timeInfo.tm_yday != lastDailySummaryDayOfYear) {
    String pdfResult = requestDailySummaryPdf(ADMIN_ID);
    if (pdfResult.length() > 0) {
      sendTelegram("Tageszusammenfassung (PDF):\n" + pdfResult);
    } else {
      sendTelegram("Tageszusammenfassung konnte nicht erstellt werden.");
    }
    lastDailySummaryDayOfYear = timeInfo.tm_yday;
  }
}

void sendTelegramTo(const String &chatId, const String &message) {
  if (WiFi.status() != WL_CONNECTED || !bot) {
    return;
  }
  bot->sendMessage(chatId, message, "");
}

void sendTelegram(const String &message) {
  sendTelegramTo(ADMIN_ID, message);
}

void sendAccessRequestToAdmin(const String &requestUserId) {
  String keyboard =
      "["
      "[{\"text\":\"Freigeben\",\"callback_data\":\"req_allow_" + requestUserId + "\"},"
      " {\"text\":\"Ablehnen\",\"callback_data\":\"req_deny_" + requestUserId + "\"}]"
      "]";

  String msg = "Neue Zugriffsanfrage von User-ID: " + requestUserId;
  if (bot) {
    bot->sendMessageWithInlineKeyboard(ADMIN_ID, msg, "", keyboard);
  }
}

void drawStartupTestScreen() {
  int width = gfx->width();
  int height = gfx->height();

  gfx->fillScreen(RGB565_BLUE);
  gfx->drawRect(0, 0, width - 1, height - 1, RGB565_WHITE);
  gfx->drawRect(2, 2, width - 5, height - 5, RGB565_WHITE);
  gfx->fillRect(20, 20, width - 40, 50, RGB565_RED);
  gfx->fillRect(20, 90, width - 40, 50, RGB565_YELLOW);
  gfx->fillRect(20, 160, width - 40, 50, RGB565_GREEN);

  gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
  gfx->setTextSize(3);
  gfx->setCursor(28, 230);
  gfx->print("DISPLAY TEST OK");

  gfx->setTextSize(2);
  gfx->setCursor(28, 270);
  gfx->print("Radar UI startet...");
  gfx->flush();
}

void drawScreenFrame() {
  int width = gfx->width();
  int height = gfx->height();
  gfx->drawRect(0, 0, width - 1, height - 1, RGB565_WHITE);
}

void drawHeader(const char *title) {
  int width = gfx->width();
  gfx->fillRect(0, 0, width, 42, RGB565_DARKGREY);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(8, 12);
  gfx->print(radarTime);
  gfx->setCursor(110, 12);
  gfx->print(title);
  gfx->setCursor(300, 12);
  gfx->print(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi ERR");
  gfx->fillCircle(width - 20, 20, 7, radarDot ? RGB565_GREEN : RGB565_RED);
}

void drawFooter(const char *leftLabel, const char *rightLabel) {
  int height = gfx->height();
  int width = gfx->width();
  gfx->fillRect(0, height - 44, width, 44, RGB565_DARKGREY);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(12, height - 28);
  gfx->print(leftLabel);
  gfx->setCursor(width - 120, height - 28);
  gfx->print(rightLabel);
}

void drawRadarPage() {
  int width = gfx->width();
  int contentBottom = gfx->height() - 44;
  gfx->fillScreen(RGB565_NAVY);
  drawScreenFrame();
  drawHeader("Radar");

  gfx->setTextSize(7);
  gfx->setTextColor((alarmEnabled && radarSpeed > radarLimit) ? RGB565_RED : RGB565_WHITE);
  gfx->setCursor(24, 64);
  gfx->print(radarSpeed);

  gfx->setTextSize(3);
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setCursor(width - 160, 84);
  gfx->print("km/h");

  gfx->setTextColor(RGB565_YELLOW);
  gfx->setCursor(24, 132);
  gfx->print("Limit: ");
  gfx->print(radarLimit);

  gfx->setCursor(24, 166);
  gfx->print("Alarm: ");
  gfx->print(alarmEnabled ? "AN" : "AUS");

  int barWidth = map(constrain(radarSpeed, 0, 180), 0, 180, 0, width - 48);
  int barY = contentBottom - 74;
  gfx->drawRect(24, barY, width - 48, 20, RGB565_WHITE);
  gfx->fillRect(24, barY, barWidth, 20,
                (alarmEnabled && radarSpeed > radarLimit) ? RGB565_RED : RGB565_GREEN);

  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(24, contentBottom - 34);
  gfx->print("IP: ");
  gfx->print(radarIP.length() ? radarIP : "-");

  drawFooter("System", "Menue");
}

void drawSystemPage() {
  gfx->fillScreen(RGB565_DARKCYAN);
  drawScreenFrame();
  drawHeader("System");
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);

  gfx->setCursor(18, 60);
  gfx->print("WiFi: ");
  gfx->print(WiFi.status() == WL_CONNECTED ? "Verbunden" : "Offline");

  gfx->setCursor(18, 95);
  gfx->print("IP: ");
  gfx->print(radarIP.length() ? radarIP : WiFi.localIP().toString());

  gfx->setCursor(18, 130);
  gfx->print("Limit: ");
  gfx->print(radarLimit);

  gfx->setCursor(18, 165);
  gfx->print("Radar: ");
  gfx->print(radarDot ? "ONLINE" : "OFFLINE");

  gfx->setCursor(18, 200);
  gfx->print("Telegram: Polling");

  gfx->setCursor(18, 235);
  gfx->print("Status: ");
  gfx->print(lastStatusMessage);

  drawFooter("Radar", "Menue");
}

void drawDebugPage() {
  gfx->fillScreen(RGB565_DARKGREEN);
  drawScreenFrame();
  drawHeader("Debug");
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE);

  gfx->setCursor(18, 60);
  gfx->print("Speed: ");
  gfx->print(radarSpeed);

  gfx->setCursor(18, 95);
  gfx->print("Time: ");
  gfx->print(radarTime);

  gfx->setCursor(18, 130);
  gfx->print("IP Radar: ");
  gfx->print(radarIP.length() ? radarIP : "-");

  gfx->setCursor(18, 165);
  gfx->print("Last Speed: ");
  gfx->print(lastLoggedSpeed);

  gfx->setCursor(18, 200);
  gfx->print("Touch: ");
  gfx->print(lastTouchX);
  gfx->print(',');
  gfx->print(lastTouchY);

  drawFooter("Radar", "Menue");
}

// --- Touch-Feedback: gedrückter Button wird weiß ---
int lastPressedButtonIdx = -1;
unsigned long lastButtonPressMs = 0;
const unsigned long BUTTON_FEEDBACK_MS = 120;

void drawMenuPage() {
  gfx->fillScreen(RGB565_DARKGREY);
  drawScreenFrame();
  drawHeader("Telegram Menue");
  gfx->setTextSize(2);

  unsigned long now = millis();
  for (int i = 0; i < (int)(sizeof(menuButtons)/sizeof(menuButtons[0])); ++i) {
    const TouchButton &button = menuButtons[i];
    uint16_t color = button.fillColor;
    if (i == lastPressedButtonIdx && now - lastButtonPressMs < BUTTON_FEEDBACK_MS) {
      color = RGB565_WHITE;
    }
    gfx->fillRoundRect(button.x, button.y, button.w, button.h, 8, color);
    gfx->drawRoundRect(button.x, button.y, button.w, button.h, 8, RGB565_WHITE);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(button.x + 12, button.y + 13);
    gfx->print(button.label);
  }

  gfx->setTextColor(RGB565_YELLOW);
  gfx->setCursor(15, 256);
  gfx->print("Limit: ");
  gfx->print(radarLimit);
  gfx->print("  Alarm: ");
  gfx->print(alarmEnabled ? "AN" : "AUS");

  drawFooter("Radar", "Touch aktiv");
}

void renderUi() {
  if (showLast10Window) {
    drawLast10Window();
    return;
  }
  switch (currentPage) {
    case PAGE_RADAR:
      drawRadarPage();
      break;
    case PAGE_SYSTEM:
      drawSystemPage();
      break;
    case PAGE_DEBUG:
      drawDebugPage();
      break;
    case PAGE_MENU:
      drawMenuPage();
      break;
  }
  gfx->flush();
}

void applySwipeNavigation(int16_t dx) {
  if (dx < 0) {
    if (currentPage == PAGE_RADAR) {
      currentPage = PAGE_SYSTEM;
      lastStatusMessage = "Wisch: System";
    } else if (currentPage == PAGE_SYSTEM) {
      currentPage = PAGE_MENU;
      lastStatusMessage = "Wisch: Menue";
    } else if (currentPage == PAGE_MENU) {
      currentPage = PAGE_RADAR;
      lastStatusMessage = "Wisch: Radar";
    } else {
      currentPage = PAGE_RADAR;
      lastStatusMessage = "Wisch: Radar";
    }
  } else {
    if (currentPage == PAGE_RADAR) {
      currentPage = PAGE_MENU;
      lastStatusMessage = "Wisch: Menue";
    } else if (currentPage == PAGE_MENU) {
      currentPage = PAGE_SYSTEM;
      lastStatusMessage = "Wisch: System";
    } else if (currentPage == PAGE_SYSTEM) {
      currentPage = PAGE_RADAR;
      lastStatusMessage = "Wisch: Radar";
    } else {
      currentPage = PAGE_MENU;
      lastStatusMessage = "Wisch: Menue";
    }
  }
  lastUiRefresh = 0;
}

void enterScreenSaver() {
  if (screenSaverActive) {
    return;
  }
  screenSaverActive = true;
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();
  digitalWrite(GFX_BL, LOW);
  lastStatusMessage = "Bildschirmschoner aktiv";
}

void wakeFromScreenSaver() {
  if (!screenSaverActive) {
    return;
  }
  screenSaverActive = false;
  digitalWrite(GFX_BL, HIGH);
  lastUserInteractionMs = millis();
  lastStatusMessage = "Display geweckt";
  lastUiRefresh = 0;

  // Testeintrag mit 77 km/h beim Neustart
  String testQuery = "speed=77&limit=" + String(radarLimit) + "&id=DISPLAY_BOOT&source=DISPLAY_BOOT";
  callGoogleAction("ADD_TEST_ENTRY", testQuery);

  renderUi();
}

void sendAdminMenu(const String &chatId) {
  String keyboard =
      "["
      "[{\"text\":\"L+1\",\"callback_data\":\"limit_up_1\"},"
      " {\"text\":\"L+5\",\"callback_data\":\"limit_up_5\"},"
      " {\"text\":\"L+10\",\"callback_data\":\"limit_up_10\"}],"
      "[{\"text\":\"L-1\",\"callback_data\":\"limit_down_1\"},"
      " {\"text\":\"L-5\",\"callback_data\":\"limit_down_5\"},"
      " {\"text\":\"L-10\",\"callback_data\":\"limit_down_10\"}],"
      "[{\"text\":\"Alarm TOGGLE\",\"callback_data\":\"alarm_toggle\"}],"
        "[{\"text\":\"E-Mail hinterlassen\",\"callback_data\":\"email_prompt\"},"
      " {\"text\":\"Tages-PDF\",\"callback_data\":\"daily_pdf\"}],"
        "[{\"text\":\"2h-PDF\",\"callback_data\":\"two_hour_pdf\"},"
        " {\"text\":\"Test 1km/h\",\"callback_data\":\"test_1kmh\"}],"
      "[{\"text\":\"Radar\",\"callback_data\":\"page_radar\"},"
      " {\"text\":\"System\",\"callback_data\":\"page_system\"},"
      " {\"text\":\"Debug\",\"callback_data\":\"page_debug\"}],"
      "[{\"text\":\"Reboot\",\"callback_data\":\"reboot\"}]"
      "]";

  if (bot) {
    bot->sendMessageWithInlineKeyboard(chatId,
                                      "Admin Menue auf Telegram und Display verfuegbar.",
                                      "",
                                      keyboard);
  }
}

void applyAction(const String &action, bool notifyTelegram) {
  if (action.startsWith("limit_up_")) {
    radarLimit += action.substring(9).toInt();
    lastStatusMessage = "Limit erhoeht";
  } else if (action.startsWith("limit_down_")) {
    radarLimit -= action.substring(11).toInt();
    lastStatusMessage = "Limit gesenkt";
  } else if (action == "alarm_toggle") {
    alarmEnabled = !alarmEnabled;
    lastStatusMessage = alarmEnabled ? "Alarm aktiviert" : "Alarm deaktiviert";
  } else if (action == "page_radar") {
    currentPage = PAGE_RADAR;
    lastStatusMessage = "Radar-Seite";
  } else if (action == "page_system") {
    currentPage = PAGE_SYSTEM;
    lastStatusMessage = "System-Seite";
  } else if (action == "page_debug") {
    currentPage = PAGE_DEBUG;
    lastStatusMessage = "Debug-Seite";
  } else if (action == "test_1kmh") {
    radarSpeed = 1;
    lastStatusMessage = "Test 1 km/h";
    String result = addTestEntryToSheets(ADMIN_ID, "DISPLAY_MENU");
    if (result.length() > 0) {
      lastStatusMessage = "Test gespeichert";
    }
  } else if (action == "reboot") {
    if (notifyTelegram) {
      sendTelegram("Reboot vom Display/Telegram ausgeloest");
    }
    delay(250);
    ESP.restart();
  }

  if (action.startsWith("limit_") || action == "alarm_toggle") {
    logToSheets(action);
  }

  if (notifyTelegram && action != "reboot") {
    sendTelegram(lastStatusMessage + " | Limit=" + String(radarLimit));
  }

  lastUiRefresh = 0;
}

void handleTelegramButton(const String &data, const String &senderId, const String &chatId) {
  if (!isPrivilegedUser(senderId)) {
    sendTelegramTo(chatId, "Keine Rechte.");
    return;
  }

  if (data == "email_prompt") {
    if (setEmailPending(senderId, chatId)) {
      sendTelegramTo(chatId, "Bitte sende jetzt deine E-Mail-Adresse als naechste Nachricht.");
    } else {
      sendTelegramTo(chatId, "E-Mail-Eingabe aktuell nicht moeglich. Bitte spaeter erneut.");
    }
    return;
  }

  if (data == "daily_pdf") {
    String pdfResult = requestDailySummaryPdf(senderId);
    if (pdfResult.length() > 0) {
      sendTelegramTo(chatId, "Tageszusammenfassung (PDF):\n" + pdfResult);
    } else {
      sendTelegramTo(chatId, "PDF konnte nicht erstellt werden.");
    }
    return;
  }

  if (data == "two_hour_pdf") {
    String pdfResult = requestTwoHourSummaryPdf(senderId);
    if (pdfResult.length() > 0) {
      sendTelegramTo(chatId, "2h-Zusammenfassung (PDF):\n" + pdfResult);
    } else {
      sendTelegramTo(chatId, "2h-PDF konnte nicht erstellt werden.");
    }
    return;
  }

  if (data == "test_1kmh") {
    radarSpeed = 1;
    String result = addTestEntryToSheets(senderId, "TELEGRAM_MENU");
    if (result.length() > 0) {
      sendTelegramTo(chatId, "Testeintrag 1 km/h gespeichert.\n" + result);
    } else {
      sendTelegramTo(chatId, "Testeintrag konnte nicht gespeichert werden.");
    }
    return;
  }

  if (data.startsWith("req_allow_")) {
    if (!isAdmin(senderId)) {
      sendTelegramTo(chatId, "Nur Admin darf Anfragen freigeben.");
      return;
    }
    String targetUserId = data.substring(10);
    if (targetUserId.length() == 0) {
      sendTelegramTo(chatId, "Fehlerhafte Anfrage-ID.");
      return;
    }

    addApprovedUser(targetUserId);
    String targetChatId = takePendingChatId(targetUserId);
    if (targetChatId.length() == 0) {
      targetChatId = targetUserId;
    }

    sendTelegramTo(chatId, "User freigegeben: " + targetUserId);
    sendTelegramTo(targetChatId, "Deine Anfrage wurde freigegeben. Nutze /help.");
    return;
  }

  if (data.startsWith("req_deny_")) {
    if (!isAdmin(senderId)) {
      sendTelegramTo(chatId, "Nur Admin darf Anfragen ablehnen.");
      return;
    }
    String targetUserId = data.substring(9);
    String targetChatId = takePendingChatId(targetUserId);

    sendTelegramTo(chatId, "Anfrage abgelehnt: " + targetUserId);
    if (targetChatId.length() > 0) {
      sendTelegramTo(targetChatId, "Deine Anfrage wurde abgelehnt.");
    }
    return;
  }

  applyAction(data, false);
}

void handleTelegramCommand(const String &command, const String &senderId, const String &chatId) {
  bool senderIsAdmin = isAdmin(senderId);
  bool senderIsPrivileged = isPrivilegedUser(senderId);
  String raw = command;
  raw.trim();

  if (isEmailPending(senderId) && !raw.startsWith("/")) {
    String email = raw;
    bool looksLikeEmail = email.indexOf('@') > 0 && email.indexOf('.') > 0 && email.indexOf(' ') < 0;
    if (!looksLikeEmail) {
      sendTelegramTo(chatId, "Bitte eine gueltige E-Mail senden (z.B. name@domain.de).");
      return;
    }

    takeEmailPendingChatId(senderId);
    logToSheets("email_user_" + senderId + "_" + email);
    sendTelegramTo(chatId, "Danke! E-Mail wurde gespeichert.");
    if (!senderIsAdmin) {
      sendTelegramTo(ADMIN_ID, "Neue E-Mail von User " + senderId + ": " + email);
    }
    return;
  }

  String cmd = command;
  cmd.trim();
  int spacePos = cmd.indexOf(' ');
  if (spacePos > 0) {
    cmd = cmd.substring(0, spacePos);
  }
  int atPos = cmd.indexOf('@');
  if (atPos > 0) {
    cmd = cmd.substring(0, atPos);
  }

  if (cmd == "/start" || cmd == "/help") {
    String help = "Verfuegbare Befehle:\n";
    help += "/status - aktueller Radarstatus\n";
    help += "/menu - Display-Menue oeffnen\n";
    help += "/ping - Verbindungstest\n";
    help += "/email - E-Mail-Adresse hinterlassen\n";
    help += "/summary - Tages-PDF erzeugen und Link senden\n";
    help += "/summary2h - 2h-PDF erzeugen und Link senden\n";
    help += "/test1 - Testeintrag 1 km/h speichern\n";
    help += "/admin - Admin-Menue (nur Admin)";
    sendTelegramTo(chatId, help);
    if (senderIsAdmin) {
      sendAdminMenu(chatId);
    }
    return;
  }

  if (cmd == "/admin") {
    if (!senderIsPrivileged) {
      bool isNewPending = addPendingRequest(senderId, chatId);
      if (isNewPending) {
        sendAccessRequestToAdmin(senderId);
        sendTelegramTo(chatId, "Anfrage gesendet. Warte auf Admin-Freigabe.");
      } else {
        sendTelegramTo(chatId, "Anfrage bereits offen. Bitte warten.");
      }
      return;
    }
    sendAdminMenu(chatId);
    return;
  }

  if (cmd == "/ping") {
    sendTelegramTo(chatId, "pong");
    return;
  }

  if (cmd == "/status") {
    String status = "Radar: " + String(radarSpeed) + " km/h\n";
    status += "Limit: " + String(radarLimit) + "\n";
    status += "Alarm: " + String(alarmEnabled ? "AN" : "AUS") + "\n";
    status += "WiFi: " + String(WiFi.status() == WL_CONNECTED ? "OK" : "ERR");
    sendTelegramTo(chatId, status);
    return;
  }

  if (cmd == "/menu") {
    currentPage = PAGE_MENU;
    lastStatusMessage = "Display-Menue geoeffnet";
    sendTelegramTo(chatId, "Display-Menue geoeffnet.");
    return;
  }

  if (cmd == "/email") {
    if (setEmailPending(senderId, chatId)) {
      sendTelegramTo(chatId, "Bitte sende jetzt deine E-Mail-Adresse als naechste Nachricht.");
    } else {
      sendTelegramTo(chatId, "E-Mail-Eingabe aktuell nicht moeglich. Bitte spaeter erneut.");
    }
    return;
  }

  if (cmd == "/summary" || cmd == "/pdf") {
    if (!senderIsPrivileged) {
      sendTelegramTo(chatId, "Keine Rechte. Nutze /admin fuer eine Anfrage.");
      return;
    }
    String pdfResult = requestDailySummaryPdf(senderId);
    if (pdfResult.length() > 0) {
      sendTelegramTo(chatId, "Tageszusammenfassung (PDF):\n" + pdfResult);
    } else {
      sendTelegramTo(chatId, "PDF konnte nicht erstellt werden.");
    }
    return;
  }

  if (cmd == "/summary2h" || cmd == "/pdf2h") {
    if (!senderIsPrivileged) {
      sendTelegramTo(chatId, "Keine Rechte. Nutze /admin fuer eine Anfrage.");
      return;
    }
    String pdfResult = requestTwoHourSummaryPdf(senderId);
    if (pdfResult.length() > 0) {
      sendTelegramTo(chatId, "2h-Zusammenfassung (PDF):\n" + pdfResult);
    } else {
      sendTelegramTo(chatId, "2h-PDF konnte nicht erstellt werden.");
    }
    return;
  }

  if (cmd == "/test1" || cmd == "/test1kmh") {
    if (!senderIsPrivileged) {
      sendTelegramTo(chatId, "Keine Rechte. Nutze /admin fuer eine Anfrage.");
      return;
    }
    radarSpeed = 1;
    String result = addTestEntryToSheets(senderId, "TELEGRAM_CMD");
    if (result.length() > 0) {
      sendTelegramTo(chatId, "Testeintrag 1 km/h gespeichert.\n" + result);
    } else {
      sendTelegramTo(chatId, "Testeintrag konnte nicht gespeichert werden.");
    }
    return;
  }

  if (senderIsPrivileged) {
    sendTelegramTo(chatId, "Unbekannter Befehl. Ich zeige dir direkt das Admin-Menue.");
    sendAdminMenu(chatId);
  } else {
    sendTelegramTo(chatId, "Unbekannter Befehl. Nutze /help oder /admin fuer eine Anfrage.");
  }
}

void pollTelegram() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (now - lastBotCheck < BOT_INTERVAL) {
    return;
  }
  lastBotCheck = now;

  if (!bot) {
    return;
  }

  int count = bot->getUpdates(bot->last_message_received + 1);
  while (count) {
    for (int i = 0; i < count; ++i) {
      if (bot->messages[i].type == "callback_query") {
        handleTelegramButton(bot->messages[i].text, bot->messages[i].from_id, bot->messages[i].chat_id);
      } else {
        handleTelegramCommand(bot->messages[i].text, bot->messages[i].from_id, bot->messages[i].chat_id);
      }
    }
    count = bot->getUpdates(bot->last_message_received + 1);
  }
}


void fetchRadarValuesFromSheet() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  // Trage hier die URL deines Google Sheets Webhooks ein:
  String url = String(SECRET_GOOGLE_URL) + "?action=GET_LAST";
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    // Erwartetes Format: JSON {"speed":42,"limit":30,"time":"12:34","ip":"192.168.1.10","dot":1}
    int idx;
    if ((idx = payload.indexOf("\"speed\":")) >= 0) {
      radarSpeed = payload.substring(idx + 8).toInt();
    }
    if ((idx = payload.indexOf("\"limit\":")) >= 0) {
      radarLimit = payload.substring(idx + 8).toInt();
    }
    if ((idx = payload.indexOf("\"time\":\"") ) >= 0) {
      int start = idx + 8 + 2;
      int end = payload.indexOf('"', start);
      if (end > start) radarTime = payload.substring(start, end);
    }
    if ((idx = payload.indexOf("\"ip\":\"") ) >= 0) {
      int start = idx + 6 + 2;
      int end = payload.indexOf('"', start);
      if (end > start) radarIP = payload.substring(start, end);
    }
    if ((idx = payload.indexOf("\"dot\":")) >= 0) {
      radarDot = payload.substring(idx + 6, idx + 7) == "1";
    }
  }
  http.end();
}

void logRadarChanges() {
  if (radarSpeed != lastLoggedSpeed) {
    lastLoggedSpeed = radarSpeed;
    // Alarm nur bei echter Ueberschreitung > 20% ueber eingestelltem Limit.
    int alarmThreshold = (radarLimit * 120 + 99) / 100;
    bool alarmNow = alarmEnabled && radarSpeed > alarmThreshold;
    // Nur echte Eintraege synchronisieren: Alarm-Start statt permanenter Statusupdates.
    if (alarmNow && !lastAlarmActive) {
      logToSheets("radar_alarm_" + String(radarSpeed));
      sendTelegram("ALARM! Speed: " + String(radarSpeed) +
                   " | Limit: " + String(radarLimit) +
                   " | Schwelle(+20%): " + String(alarmThreshold));
    }
    lastAlarmActive = alarmNow;
  }

  if (radarLimit != lastLoggedLimit) {
    lastLoggedLimit = radarLimit;
  }

  if (radarIP != lastLoggedIP) {
    lastLoggedIP = radarIP;
  }

  if (radarTime != lastLoggedTime) {
    lastLoggedTime = radarTime;
  }

  if (radarDot != lastLoggedDot) {
    lastLoggedDot = radarDot;
  }
}

void handleTouch() {
  uint16_t x = 0;
  uint16_t y = 0;
  bool pressed = getTouchPoint(x, y);

  if (pressed) {
    lastTouchX = x;
    lastTouchY = y;
    if (!touchActive && millis() - lastTouchReleaseCheck > TOUCH_DEBOUNCE_MS) {
      touchActive = true;
      touchStartX = x;
      touchStartY = y;
      touchStartMs = millis();

      if (screenSaverActive) {
        wakeFromScreenSaver();
        return;
      }

      lastUserInteractionMs = millis();

      if (showLast10Window) {
        // Bei Touch im Last10-Fenster: Fenster schließen
        showLast10Window = false;
        lastUiRefresh = 0;
        renderUi();
        return;
      }
      if (currentPage == PAGE_MENU) {
        for (int i = 0; i < (int)(sizeof(menuButtons)/sizeof(menuButtons[0])); ++i) {
          const TouchButton &button = menuButtons[i];
          if (pointInButton(x, y, button)) {
            lastPressedButtonIdx = i;
            lastButtonPressMs = millis();
            renderUi(); // Sofort Feedback anzeigen
            delay(BUTTON_FEEDBACK_MS); // Kurze Pause für Weiß-Effekt
            if (String(button.action) == "show_last10") {
              // Dummy-Daten für Demo, später aus Sheet holen
              last10Entries.clear();
              for (int j = 0; j < 10; ++j) {
                LastEntry e;
                e.time = String("12:") + String(50-j);
                e.speed = 40-j;
                e.limit = 50;
                last10Entries.push_back(e);
              }
              showLast10Window = true;
              renderUi();
              lastPressedButtonIdx = -1;
              lastUiRefresh = 0;
              return;
            }
            applyAction(button.action, true);
            lastPressedButtonIdx = -1;
            lastUiRefresh = 0;
            return;
          }
        }
      }

      if (y >= gfx->height() - 44) {
        if (x < gfx->width() / 2) {
          currentPage = (currentPage == PAGE_RADAR) ? PAGE_SYSTEM : PAGE_RADAR;
          lastStatusMessage = "Seite gewechselt";
        } else {
          currentPage = PAGE_MENU;
          lastStatusMessage = "Menue geoeffnet";
        }
        lastUiRefresh = 0;
      }
    }
  } else {
    if (touchActive) {
      int16_t dx = static_cast<int16_t>(lastTouchX) - static_cast<int16_t>(touchStartX);
      int16_t dy = static_cast<int16_t>(lastTouchY) - static_cast<int16_t>(touchStartY);
      unsigned long dt = millis() - touchStartMs;
      if (dt == 0) {
        dt = 1;
      }
      int speedPxPerS = static_cast<int>((static_cast<unsigned long>(abs(dx)) * 1000UL) / dt);

      if (abs(dx) >= SWIPE_MIN_DX &&
          abs(dy) <= SWIPE_MAX_DY &&
          dt <= SWIPE_MAX_DURATION_MS &&
          speedPxPerS >= SWIPE_MIN_SPEED_PX_PER_S) {
        lastUserInteractionMs = millis();
        applySwipeNavigation(dx);
      }
      lastTouchReleaseCheck = millis();
    }
    touchActive = false;
  }
}

void setupDisplayAndTouch() {
  if (!bus) {
    bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
  }
  if (!panel) {
    panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
  }
  if (!gfx) {
    gfx = new Arduino_Canvas(320, 480, panel, 0, 0, 0);
  }

  gfx->begin();
  gfx->setRotation(1);
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(TOUCH_I2C_CLOCK);
  pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
  pinMode(TOUCH_RST_PIN, OUTPUT);
  digitalWrite(TOUCH_RST_PIN, LOW);
  delay(200);
  digitalWrite(TOUCH_RST_PIN, HIGH);
  delay(200);
}

}  // namespace

::WiFiClientSecure &displaySecureClient = secureClient;

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("[BOOT] touch-only test start");

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  if (!bus) {
    bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
  }
  if (!panel) {
    panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
  }
  if (!gfx) {
    gfx = new Arduino_Canvas(320, 480, panel, 0, 0, 0);
  }

  Serial.println("[BOOT] gfx objects created");
  gfx->begin();
  Serial.println("[BOOT] gfx begin done");
  gfx->setRotation(1);
  Serial.println("[BOOT] rotation set");

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(TOUCH_I2C_CLOCK);
  pinMode(TOUCH_INT_PIN, INPUT_PULLUP);
  pinMode(TOUCH_RST_PIN, OUTPUT);
  digitalWrite(TOUCH_RST_PIN, LOW);
  delay(200);
  digitalWrite(TOUCH_RST_PIN, HIGH);
  delay(200);
  Serial.println("[BOOT] touch init done");

  currentPage = PAGE_RADAR;
  lastUserInteractionMs = millis();
  lastUiRefresh = 0;
  renderUi();
  Serial.println("[BOOT] touch+display ui rendered");

  secureClient.setInsecure();
  Serial.println("[BOOT] secure client ready");
  startWiFiConnect();
  Serial.println("[BOOT] wifi connect scheduled");
}

void loop() {
  updateWiFiConnect();
  handleTouch();

  if (!screenSaverActive && millis() - lastUserInteractionMs > SCREEN_SAVER_TIMEOUT_MS) {
    enterScreenSaver();
  }

  if (!screenSaverActive && millis() - lastUiRefresh > UI_INTERVAL) {
    lastUiRefresh = millis();
    renderUi();
  }

  static unsigned long lastTouchLogMs = 0;
  if (millis() - lastTouchLogMs > 500) {
    lastTouchLogMs = millis();
    Serial.print("[TOUCH] x=");
    Serial.print(lastTouchX);
    Serial.print(" y=");
    Serial.print(lastTouchY);
    Serial.print(" active=");
    Serial.println(touchActive ? 1 : 0);
  }

  delay(5);
}
