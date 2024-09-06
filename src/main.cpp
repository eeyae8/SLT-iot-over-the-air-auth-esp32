#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* github_raw_url = "https://raw.githubusercontent.com/YOUR_USERNAME/YOUR_REPO/main/firmware_info.json";
const char* current_version = "1.0.0";  // Hardcoded for simplicity

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");\
  }
  Serial.println("Connected to WiFi");
}

void loop() {
  checkForUpdates();
  delay(60000); // Check for updates every minute
}

void checkForUpdates() {
  HTTPClient http;
  http.begin(github_raw_url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    
    const char* new_version = doc["version"];
    const char* firmware_url = doc["url"];
    
    if (strcmp(new_version, current_version) > 0) {
      Serial.println("New firmware version available");
      updateFirmware(firmware_url);
    } else {
      Serial.println("Firmware is up to date");
    }
  } else {
    Serial.println("Failed to check for updates");
  }
  
  http.end();
}

void updateFirmware(const char* firmware_url) {
  HTTPClient http;
  http.begin(firmware_url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    if (contentLength > 0) {
      bool canBegin = Update.begin(contentLength);
      if (canBegin) {
        WiFiClient * stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);
        if (written == contentLength) {
          Serial.println("Written : " + String(written) + " successfully");
        } else {
          Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
        }
        if (Update.end()) {
          Serial.println("OTA done!");
          if (Update.isFinished()) {
            Serial.println("Update successfully completed. Rebooting.");
            ESP.restart();
          } else {
            Serial.println("Update not finished? Something went wrong!");
          }
        } else {
          Serial.println("Error Occurred. Error #: " + String(Update.getError()));
        }
      } else {
        Serial.println("Not enough space to begin OTA");
      }
    } else {
      Serial.println("There was no content in the response");
    }
  } else {
    Serial.println("Firmware download failed");
  }
  
  http.end();
}