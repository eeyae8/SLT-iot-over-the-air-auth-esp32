#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// version 1.0.3

const char* WIFI_FILE = "/wifi_creds.json";
const char* VERSION_FILE = "/firmware_version.txt";
const char* firmware_info_url = "https://raw.githubusercontent.com/eeyae8/SLT-iot-over-the-air-auth-esp32/main/firmware_info.json";

// Root CA certificate for GitHub
const char* rootCACertificate = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";

void loadWiFiCredentials();
void saveWiFiCredentials(const char* ssid, const char* password);
void connectToWiFi();
void getWiFiCredentials();
bool checkForUpdates();
void updateFirmware(const char* firmware_url, const char* new_version);
String getCurrentVersion();
void saveCurrentVersion(const char* version);
bool getUserConfirmation();
void performUpdate();
bool checkFirmwareSize(size_t firmwareSize);
void setupSecureClient(WiFiClientSecure &client);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect
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

void setupSecureClient(WiFiClientSecure &client) {
  client.setCACert(rootCACertificate);
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
  return "0.0.0";
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
    Serial.println("IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFailed to connect. Please check your credentials.");
    getWiFiCredentials();
    connectToWiFi();
  }
}

bool checkForUpdates() {
  Serial.println("Checking for updates...");
  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) {
    setupSecureClient(*client);
    
    {
      HTTPClient https;
      
      Serial.print("[HTTPS] begin...\n");
      if (https.begin(*client, firmware_info_url)) {
        Serial.print("[HTTPS] GET...\n");
        int httpCode = https.GET();
        if (httpCode > 0) {
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = https.getString();
            Serial.println(payload);
            
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
              const char* new_version = doc["version"];
              const char* firmware_url = doc["url"];
              String current_version = getCurrentVersion();
              Serial.printf("Current version: %s\n", current_version.c_str());
              Serial.printf("Available version: %s\n", new_version);
              
              if (String(new_version) > current_version) {
                Serial.println("New version available.");
                https.end();
                delete client;
                if (getUserConfirmation()) {
                  updateFirmware(firmware_url, new_version);
                  return true;
                }
                return false;
              } else {
                Serial.println("Firmware is up to date.");
              }
            } else {
              Serial.println("JSON parsing failed");
            }
          }
        } else {
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }
        https.end();
      } else {
        Serial.printf("[HTTPS] Unable to connect\n");
      }
    }
    delete client;
  } else {
    Serial.println("Unable to create client");
  }
  return false;
}

void updateFirmware(const char* firmware_url, const char* new_version) {
  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) {
    setupSecureClient(*client);
    
    {
      HTTPClient https;
      if (https.begin(*client, firmware_url)) {
        int httpCode = https.GET();
        if (httpCode > 0) {
          if (httpCode == HTTP_CODE_OK) {
            int contentLength = https.getSize();
            if (contentLength > 0) {
              bool canBegin = Update.begin(contentLength);
              if (canBegin) {
                WiFiClient * stream = https.getStreamPtr();
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
                    saveCurrentVersion(new_version);
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
            Serial.println("Error on HTTP request");
          }
        } else {
          Serial.println("Error on HTTP request");
        }
        https.end();
      }
    }
    delete client;
  }
}

bool checkFirmwareSize(size_t firmwareSize) {
    size_t freeSketchSpace = ESP.getFreeSketchSpace();
    
    Serial.printf("Free Sketch Space: %u bytes\n", freeSketchSpace);
    Serial.printf("New Firmware Size: %u bytes\n", firmwareSize);

    if (firmwareSize > freeSketchSpace) {
        Serial.println("WARNING: New firmware is larger than available space!");
        Serial.printf("Additional space needed: %u bytes\n", firmwareSize - freeSketchSpace);
        return false;
    } else {
        Serial.printf("Available space after update: %u bytes\n", freeSketchSpace - firmwareSize);
        return true;
    }
}

bool getUserConfirmation() {
  Serial.println("A new firmware update is available.");
  Serial.println("Do you want to update? (1 for Yes, 0 for No)");

  while (!Serial.available()) {
    delay(100);
  }

  String input = Serial.readStringUntil('\n');
  input.trim();

  while (input != "0" && input != "1") {
    Serial.println("Invalid input. Please enter 1 for Yes or 0 for No.");
    while (!Serial.available()) {
      delay(100);
    }
    input = Serial.readStringUntil('\n');
    input.trim();
  }

  return (input == "1");
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
  delay(60000); // Check for updates every minute
}