#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

//version1.2

const char* WIFI_FILE = "/wifi_creds.json";
const char* github_raw_url = "https://raw.githubusercontent.com/eeyae8/SLT-iot-over-the-air-auth-esp32/main/firmware_info.json";

void loadWiFiCredentials();
void saveWiFiCredentials(const char* ssid, const char* password);
void connectToWiFi();
void getWiFiCredentials();
void checkForUpdates();
void updateFirmware(const char* firmware_url, const char* new_version);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    Serial.println("Formatting SPIFFS...");
    if (SPIFFS.format()) {
      Serial.println("SPIFFS formatted successfully");
      if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed after formatting");
        return;
      }
    } else {
      Serial.println("SPIFFS formatting failed");
      return;
    }
  }

  loadWiFiCredentials();
  connectToWiFi();

  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
}

void loadWiFiCredentials() {
  if (SPIFFS.exists(WIFI_FILE)) {
    File file = SPIFFS.open(WIFI_FILE, "r");
    if (file) {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (!error) {
        const char* ssid = doc["ssid"];
        const char* password = doc["password"];
        if (ssid && password) {
          WiFi.begin(ssid, password);
          Serial.println("Loaded WiFi credentials:");
          Serial.println("SSID: " + String(ssid));
          Serial.println("Password: [hidden]");
          return;
        }
      }
    }
  }
  
  Serial.println("No valid WiFi credentials found.");
  getWiFiCredentials();
}

void saveWiFiCredentials(const char* ssid, const char* password) {
  File file = SPIFFS.open(WIFI_FILE, "w");
  if (file) {
    StaticJsonDocument<256> doc;
    doc["ssid"] = ssid;
    doc["password"] = password;
    serializeJson(doc, file);
    file.close();
    Serial.println("WiFi credentials saved.");
    Serial.println("SSID: " + String(ssid));
  } else {
    Serial.println("Failed to open file for writing");
  }
}

void getWiFiCredentials() {
  Serial.println("Enter WiFi credentials:");
  
  Serial.print("SSID: ");
  String ssid = "";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') {
        break;
      }
      ssid += c;
    }
  }
  ssid.trim();

  Serial.print("Password: ");
  String password = "";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') {
        break;
      }
      password += c;
    }
  }
  password.trim();

  Serial.println("SSID entered: " + ssid);
  Serial.println("Password entered: " + String(password.length()) + " characters");

  saveWiFiCredentials(ssid.c_str(), password.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
}

void connectToWiFi() {
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.println("IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFailed to connect. Please check your credentials.");
    getWiFiCredentials();
    connectToWiFi();  // Try to connect again with new credentials
  }
}

void checkForUpdates() {
  HTTPClient http;
  http.begin(github_raw_url);

  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      const char* new_version = doc["version"];
      const char* firmware_url = doc["url"];
      // Compare versions and update if necessary
      // This is a simplified version check. You might want to implement a more robust version comparison.
      if (strcmp(new_version, "CURRENT_VERSION") > 0) {
        updateFirmware(firmware_url, new_version);
      }
    }
  }
  http.end();
}

void updateFirmware(const char* firmware_url, const char* new_version) {
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
          if (Update.end()) {
            Serial.println("OTA update complete");
            // Here you could save the new version to SPIFFS if needed
            ESP.restart();
          }
        }
      }
    }
  }
  http.end();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    checkForUpdates();
  } else {
    Serial.println("WiFi connection lost. Reconnecting...");
    connectToWiFi();
  }
  
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  delay(60000); // Check every minute
}