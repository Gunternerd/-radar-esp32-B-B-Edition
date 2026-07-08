#include <SPI.h>
#include <SD.h>

#define SD_CS_PIN 5

void setup() {
  Serial.begin(115200);
  while (!Serial) ;
  Serial.println("SD Diagnose starting...");

  SPI.begin();

  int freqs[] = {4000000, 8000000, 20000000};

  for (int i = 0; i < 3; ++i) {
    int f = freqs[i];
    Serial.print("Try SD.begin with SPI freq: "); Serial.println(f);
    if (SD.begin(SD_CS_PIN, SPI, f)) {
      Serial.println("SD.begin OK");
      break;
    } else {
      Serial.println("SD.begin failed");
    }
  }

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Final SD.begin failed");
    return;
  }

  Serial.print("card type: "); Serial.println(SD.cardType());
  Serial.print("card size: "); Serial.println(SD.cardSize());

  Serial.println("Root files:");
  File root = SD.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    Serial.print(entry.name());
    if (entry.isDirectory()) Serial.println("/ (dir)");
    else {
      Serial.print(" - "); Serial.println(entry.size());
    }
    entry.close();
  }
}

void loop() {
  delay(1000);
}
