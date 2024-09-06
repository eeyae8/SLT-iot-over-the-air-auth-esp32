#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Preferences.h>

const char* ssid = "AndroidAPDCE4";
const char* password = "clwu1277";
const char* github_raw_url = "https://raw.githubusercontent.com/eeyae8/SLT-iot-over-the-air-auth-esp32/main/firmware_info.json";

Preferences preferences;
String current_version;

void setup() {
  Serial.begin(115200);
  
  preferences.begin("firmware", false);
  current_version = preferences.getString("version", "1.0.0"); // Default to 1.0.0 if not set
  Serial.println("Current firmware version: " + current_version);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
}

void checkForUpdates() {
  HTTPClient http;
  http.begin(github_raw_url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    
    String new_version = doc["version"].as<String>();
    String firmware_url = doc["url"].as<String>();
    
    Serial.println("Current version: " + current_version);
    Serial.println("Available version: " + new_version);
    
    if (new_version > current_version) {
      Serial.println("New firmware version available");
      updateFirmware(firmware_url, new_version);
    } else {
      Serial.println("Firmware is up to date");
    }
  } else {
    Serial.println("Failed to check for updates");
  }
  
  http.end();
}

void updateFirmware(String firmware_url, String new_version) {
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
            preferences.putString("version", new_version);
            preferences.end();
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

void loop() {
  checkForUpdates();
  delay(60000); // Check for updates every minute
}