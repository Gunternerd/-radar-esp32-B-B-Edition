#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) ;
  Serial.println("WiFi scan start...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int n = WiFi.scanNetworks();
  Serial.print("Networks found: ");
  Serial.println(n);
  for (int i = 0; i < n; ++i) {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (RSSI ");
    Serial.print(WiFi.RSSI(i));
    Serial.print(") Channel:");
    Serial.print(WiFi.channel(i));
    Serial.print(" Encryption:");
    Serial.println(WiFi.encryptionType(i));
    delay(10);
  }
}

void loop() {
  delay(1000);
}
