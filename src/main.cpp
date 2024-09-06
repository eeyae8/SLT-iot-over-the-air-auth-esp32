#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

const char* WIFI_FILE = "/wifi_creds.json";
const char* VERSION_FILE = "/firmware_version.txt";
const char* github_raw_url = "https://raw.githubusercontent.com/eeyae8/SLT-iot-over-the-air-auth-esp32/main/firmware_info.json";

void loadWiFiCredentials();
void saveWiFiCredentials(const char* ssid, const char* password);
void connectToWiFi();
void getWiFiCredentials();
void checkForUpdates();
void updateFirmware(const char* firmware_url, const char* new_version);
String getCurrentVersion();
void saveCurrentVersion(const char* version);

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

  String current_version = getCurrentVersion();
  Serial.printf("Current firmware version: %s\n", current_version.c_str());

  loadWiFiCredentials();
  connectToWiFi();

  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
}

String getCurrentVersion() {
  if (SPIFFS.exists(VERSION_FILE)) {
    File file = SPIFFS.open(VERSION_FILE, "r");
    if (file) {
      String version = file.readStringUntil('\n');
      file.close();
      return version;
    }
  }
  return "0.0.0"; // Default version if file doesn't exist
}

void saveCurrentVersion(const char* version) {
  File file = SPIFFS.open(VERSION_FILE, "w");
  if (file) {
    file.println(version);
    file.close();
    Serial.printf("Saved new version: %s\n", version);
  } else {
    Serial.println("Failed to open version file for writing");
  }
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
    //Serial.println("IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFailed to connect. Please check your credentials.");
    getWiFiCredentials();
    connectToWiFi();  // Try to connect again with new credentials
  }
}

void checkForUpdates() {
  Serial.println("Checking for updates...");
  HTTPClient http;
  http.begin(github_raw_url);

  int httpCode = http.GET();
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Received payload: " + payload);
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      const char* new_version = doc["version"];
      const char* firmware_url = doc["url"];
      String current_version = getCurrentVersion();
      Serial.printf("Current version: %s\n", current_version.c_str());
      Serial.printf("Available version: %s\n", new_version);
      
      if (String(new_version) > current_version) {
        Serial.println("New version available. Starting update...");
        updateFirmware(firmware_url, new_version);
      } else {
        Serial.println("Firmware is up to date.");
      }
    } else {
      Serial.println("JSON parsing failed");
    }
  } else {
    Serial.printf("Failed to connect to update server. Error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void updateFirmware(const char* firmware_url, const char* new_version) {
  HTTPClient http;
  http.begin(firmware_url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();
  Serial.printf("Firmware download HTTP response code: %d\n", httpCode);
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("Firmware size: %d bytes\n", contentLength);
    if (contentLength > 0) {
      bool canBegin = Update.begin(contentLength);
      if (canBegin) {
        Serial.println("Beginning OTA update...");
        WiFiClient * stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);
        if (written == contentLength) {
          Serial.println("Written : " + String(written) + " successfully");
          if (Update.end()) {
            Serial.println("OTA update complete");
            saveCurrentVersion(new_version);
            Serial.println("Rebooting...");
            ESP.restart();
          } else {
            Serial.println("Error Occurred. Error #: " + String(Update.getError()));
          }
        } else {
          Serial.println("Write failed. Written only: " + String(written) + "/" + String(contentLength) + " bytes");
        }
      } else {
        Serial.println("Not enough space to begin OTA update");
      }
    } else {
      Serial.println("Error: Firmware file is empty");
    }
  } else {
    Serial.println("Firmware download failed");
  }
  http.end();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected. Checking for updates...");
    checkForUpdates();
  } else {
    Serial.println("WiFi connection lost. Reconnecting...");
    connectToWiFi();
  }
  
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("Waiting for next update check...");
  delay(60000); // Check every minute
}