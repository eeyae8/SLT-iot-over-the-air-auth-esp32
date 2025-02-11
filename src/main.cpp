#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <MQTT.h>

// version 1.0.7

const char* WIFI_FILE = "/wifi_creds.json"; // File to store WiFi credentials
const char* VERSION_FILE = "/firmware_version.txt"; // File to store the current firmware version
const char* firmware_info_url = "https://raw.githubusercontent.com/eeyae8/SLT-iot-over-the-air-auth-esp32/main/firmware_info.json"; // URL to check for firmware updates

const char* mqtt_server = "test.mosquitto.org"; // MQTT broker
const int mqtt_port = 1883; // MQTT port
const char* mqtt_topic = "esp32/count"; // MQTT topic

WiFiClient net; // WiFi client for MQTT
MQTTClient client; // MQTT client

int count = 0;
unsigned long lastPublishTime = 0;
const long publishInterval = 5000; // Publish every 5 seconds

//list the functions
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
void connectMQTT();
void publishCount();
void messageReceived(String &topic, String &payload);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // Wait for serial port to connect
  }

  if (!SPIFFS.begin(true)) { // Start SPIFFS and check for errors
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

  String current_version = getCurrentVersion(); // Get the current firmware version
  Serial.printf("Current firmware version: %s\n", current_version.c_str());

  loadWiFiCredentials(); // Load WiFi credentials from SPIFFS
  connectToWiFi(); // Connect to WiFi

  // Setup MQTT
  client.begin(mqtt_server, mqtt_port, net); // Initialize the MQTT client
  client.onMessage(messageReceived); // Set the callback function

  connectMQTT(); // Connect to the MQTT broker

  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap()); // Print free heap memory
}

String getCurrentVersion() { // Get the current firmware version from SPIFFS
  if (SPIFFS.exists(VERSION_FILE)) {
    File file = SPIFFS.open(VERSION_FILE, "r");
    if (file) {
      String version = file.readStringUntil('\n');
      file.close();
      return version;
    }
  }
  return "0.0.0"; // Return 0.0.0 if version file doesn't exist
}

void saveCurrentVersion(const char* version) { // Save the new firmware version to SPIFFS
  File file = SPIFFS.open(VERSION_FILE, "w");
  if (file) {
    file.println(version);
    file.close();
    Serial.printf("Saved new version: %s\n", version);
  } else {
    Serial.println("Failed to open version file for writing");
  }
}

void loadWiFiCredentials() { // Load WiFi credentials from SPIFFS
  if (SPIFFS.exists(WIFI_FILE)) {
    File file = SPIFFS.open(WIFI_FILE, "r"); // Open the file in read mode
    if (file) {
      StaticJsonDocument<256> doc; // Create a JSON document
      DeserializationError error = deserializeJson(doc, file); // Deserialize the JSON document
      file.close();

      if (!error) { 
        const char* ssid = doc["ssid"];
        const char* password = doc["password"];
        if (ssid && password) {
          WiFi.begin(ssid, password); // Connect to WiFi using the stored credentials
          Serial.println("Loaded WiFi credentials:");
          Serial.println("SSID: " + String(ssid));
          Serial.println("Password: [hidden]");
          return;
        }
      }
    }
  }
  
  Serial.println("No valid WiFi credentials found."); // If no valid credentials are found, get new credentials
  getWiFiCredentials();
}

void saveWiFiCredentials(const char* ssid, const char* password) { // Save WiFi credentials to SPIFFS
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

void getWiFiCredentials() { // Get WiFi credentials from the user
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

void connectToWiFi() { // Connect to WiFi
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
    connectToWiFi();  // Recursive call to try connecting again
  }
}

bool checkForUpdates() { // Check for firmware updates
  Serial.println("Checking for updates...");
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure(); // Skip certificate verification as certificates arent initialised right now. CHANGE IN THE FUTURE

  HTTPClient https;
  https.begin(*client, firmware_info_url);

  int httpCode = https.GET(); // Make a GET request to the update server
  Serial.printf("HTTPS response code: %d\n", httpCode);
  
  if (httpCode == HTTP_CODE_OK) { // Check if the response code is OK
    String payload = https.getString();
    Serial.println("Received payload: " + payload);
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) { // Check if JSON parsing was successful
      const char* new_version = doc["version"];
      const char* firmware_url = doc["url"];
      String current_version = getCurrentVersion();
      Serial.printf("Current version: %s\n", current_version.c_str());
      Serial.printf("Available version: %s\n", new_version);
      
      if (String(new_version) > current_version) { // Compare the current and available versions
        Serial.println("New version available.");
        updateFirmware(firmware_url, new_version);
        https.end();
        delete client;
        return true;
      } else {
        Serial.println("Firmware is up to date.");
      }
    } else {
      Serial.println("JSON parsing failed");
    }
  } else {
    Serial.printf("Failed to connect to update server. Error: %s\n", https.errorToString(httpCode).c_str());
  }
  https.end();
  delete client;
  return false;
}

void updateFirmware(const char* firmware_url, const char* new_version) { // Update the firmware
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure(); // Skip certificate verification

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Follow redirects for the firmware URL
  https.begin(*client, firmware_url);
  int httpCode = https.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = https.getSize();
    
    if (contentLength <= 0) {
      Serial.println("Error: Invalid content length for firmware");
      https.end();
      delete client;
      return;
    }

    if (!checkFirmwareSize(contentLength)) {
      Serial.println("Error: Not enough space for new firmware");
      https.end();
      delete client;
      return;
    }

    WiFiClient * stream = https.getStreamPtr(); // Get the stream to read the firmware data

    bool canBegin = Update.begin(contentLength); // Begin OTA update
    if (canBegin) {
      Serial.println("Begin OTA update...");
      size_t written = Update.writeStream(*stream);
      if (written == contentLength) {
        Serial.println("OTA update written successfully");
        if (Update.end()) {
          Serial.println("OTA update completed successfully");
          saveCurrentVersion(new_version);
          Serial.println("Rebooting...");
          ESP.restart();
        } else {
          Serial.printf("OTA update failed. Error: %u\n", Update.getError());
        }
      } else {
        Serial.printf("OTA update failed. Written %d / %d bytes\n", written, contentLength);
      }
    } else {
      Serial.println("Not enough space to begin OTA update");
    }
  } else {
    Serial.printf("Firmware download failed, HTTPS error: %s\n", https.errorToString(httpCode).c_str());
  }
  
  https.end();
  delete client;
}

bool checkFirmwareSize(size_t firmwareSize) { // Check if there is enough space for the new firmware
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

bool getUserConfirmation() { // Ask the user for confirmation before updating
  Serial.println("A new firmware update is available.");
  Serial.println("Do you want to update? (1 for Yes, 0 for No)");

  while (!Serial.available()) {
    delay(100);
  }

  String input = Serial.readStringUntil('\n'); // Read the user's input
  input.trim();

  while (input != "0" && input != "1") {
    Serial.println("Invalid input. Please enter 1 for Yes or 0 for No.");
    while (!Serial.available()) {
      delay(100);
    }
    input = Serial.readStringUntil('\n'); // Read the user's input again
    input.trim();
  }

  if (input == "1") {
    Serial.println("Update confirmed by user.");
    return true;
  } else {
    Serial.println("Update declined by user.");
    return false;
  }
}

void connectMQTT() { // Connect to the MQTT broker
  Serial.print("Connecting to MQTT broker...");
  while (!client.connect("ESP32Client")) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("Connected to MQTT broker!");
  
  client.subscribe(mqtt_topic);
}

void messageReceived(String &topic, String &payload) { // Callback function for MQTT messages
  Serial.println("Incoming: " + topic + " - " + payload);
}

void publishCount() { // Publish the count to the MQTT topic
  char msg[10];
  snprintf(msg, 10, "%d", count);
  if (client.publish(mqtt_topic, msg)) {  // QoS 0 is default and sufficient for testing
    Serial.printf("Published count: %d\n", count);
    count = (count % 10) + 1; // Increment count from 1 to 10
  } else {
    Serial.println("Failed to publish message");
  }
}

void loop() { // Main loop
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Reconnecting...");
    connectToWiFi();
  }

  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - lastPublishTime >= publishInterval) {
    lastPublishTime = currentMillis;
    publishCount();
  }

  // Check for updates every 5 minutes
  static unsigned long lastUpdateCheck = 0;
  if (currentMillis - lastUpdateCheck >= 300000) {
    lastUpdateCheck = currentMillis;
    Serial.println("Checking for updates...");
    if (checkForUpdates()) {
      if (getUserConfirmation()) {
        performUpdate();
      }
    }
  }

  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
}

void performUpdate() { // Perform the firmware update
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure(); // Skip certificate verification

  HTTPClient https;
  https.begin(*client, firmware_info_url);
  int httpCode = https.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = https.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      const char* new_version = doc["version"];
      const char* firmware_url = doc["url"];
      Serial.printf("New version: %s\n", new_version);
      Serial.printf("Firmware URL: %s\n", firmware_url);
      updateFirmware(firmware_url, new_version);
    } else {
      Serial.println("Failed to deserialize JSON");
    }
  } else {
    Serial.printf("HTTP GET failed, error: %s\n", https.errorToString(httpCode).c_str());
  }
  
  https.end();
  delete client;
}