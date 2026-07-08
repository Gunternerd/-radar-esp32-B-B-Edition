#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

static const int TFT_SCLK_PIN = 18;
static const int TFT_MOSI_PIN = 23;
static const int TFT_CS_PIN   = 5;
static const int TFT_DC_PIN   = 2;
static const int TFT_RST_PIN  = 4;
static const int TFT_BLK_PIN  = 15;

static const int TFT_WIDTH  = 128;
static const int TFT_HEIGHT = 160;

Adafruit_ST7789 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

static void showColor(uint16_t color, const char* label) {
  tft.fillScreen(color);
  tft.setCursor(8, 8);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.print(label);
  Serial.print("[COLORTEST] ");
  Serial.println(label);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[COLORTEST] start");

  pinMode(TFT_BLK_PIN, OUTPUT);
  digitalWrite(TFT_BLK_PIN, HIGH);

  SPI.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, TFT_CS_PIN);
  Serial.println("[COLORTEST] spi begin done");

  tft.init(TFT_WIDTH, TFT_HEIGHT);
  Serial.println("[COLORTEST] init done");

  tft.setRotation(1);
  Serial.println("[COLORTEST] rotation set");

  showColor(ST77XX_RED, "RED");
  delay(1500);
  showColor(ST77XX_GREEN, "GREEN");
  delay(1500);
  showColor(ST77XX_BLUE, "BLUE");
  delay(1500);
  showColor(ST77XX_WHITE, "WHITE");
  delay(1500);
  showColor(ST77XX_BLACK, "BLACK");
  delay(1500);

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(8, 8);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(1);
  tft.println("ST7789 TEST");
  tft.println("128x160");
  tft.println("BLK=GPIO15");
  Serial.println("[COLORTEST] final screen drawn");
}

void loop() {
  static bool invert = false;
  static unsigned long lastMs = 0;
  if (millis() - lastMs > 1000) {
    lastMs = millis();
    invert = !invert;
    tft.invertDisplay(invert);
    Serial.print("[COLORTEST] invert=");
    Serial.println(invert ? 1 : 0);
  }
}
