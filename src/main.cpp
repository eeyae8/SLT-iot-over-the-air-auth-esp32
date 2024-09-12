#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

//version 1.0.3

const char* WIFI_FILE = "/wifi_creds.json";
const char* VERSION_FILE = "/firmware_version.txt";
const char* github_raw_url = "https://raw.githubusercontent.com/eeyae8/SLT-iot-over-the-air-auth-esp32/main/firmware_info.json";

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

void setup() {
  Serial.begin(115200); // Initialize the serial communication at a baud rate of 115200
  while (!Serial) {
    ; // Wait for the serial port to connect. This is needed for the native USB port only.
  }

  if (!SPIFFS.begin(true)) { // Mount the SPIFFS file system
    Serial.println("An error occurred while mounting SPIFFS");
    Serial.println("Formatting SPIFFS...");
    if (SPIFFS.format()) { // Format the SPIFFS file system if mounting fails
      Serial.println("SPIFFS formatted successfully");
      if (!SPIFFS.begin(true)) { // Try mounting again after formatting
        Serial.println("SPIFFS mount failed after formatting");
        return;
      }
    } else {
      Serial.println("SPIFFS formatting failed");
      return;
    }
  }

  String current_version = getCurrentVersion(); // Get the current firmware version from a file
  Serial.printf("Current firmware version: %s\n", current_version.c_str());

  loadWiFiCredentials(); // Load the saved WiFi credentials from a file
  connectToWiFi(); // Connect to WiFi using the loaded credentials

  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap()); // Print the amount of free heap memory
}

String getCurrentVersion() {
  if (SPIFFS.exists(VERSION_FILE)) { // Check if the version file exists in the SPIFFS file system
    File file = SPIFFS.open(VERSION_FILE, "r"); // Open the version file in read mode
    if (file) {
      String version = file.readStringUntil('\n'); // Read the version string from the file until a newline character is encountered
      file.close(); // Close the file
      return version; // Return the version string
    }
  }
  return "0.0.0"; // Return a default version if the file doesn't exist
}

void saveCurrentVersion(const char* version) {
  File file = SPIFFS.open(VERSION_FILE, "w"); // Open the version file in write mode
  if (file) {
    file.println(version); // Write the version string to the file
    file.close(); // Close the file
    Serial.printf("Saved new version: %s\n", version);
  } else {
    Serial.println("Failed to open version file for writing");
  }
}

void loadWiFiCredentials() {
  if (SPIFFS.exists(WIFI_FILE)) { // Check if the WiFi credentials file exists in the SPIFFS file system
    File file = SPIFFS.open(WIFI_FILE, "r"); // Open the WiFi credentials file in read mode
    if (file) {
      StaticJsonDocument<256> doc; // Create a JSON document to store the credentials
      DeserializationError error = deserializeJson(doc, file); // Deserialize the JSON data from the file
      file.close(); // Close the file

      if (!error) {
        const char* ssid = doc["ssid"]; // Get the SSID from the JSON document
        const char* password = doc["password"]; // Get the password from the JSON document
        if (ssid && password) {
          WiFi.begin(ssid, password); // Connect to WiFi using the loaded credentials
          Serial.println("Loaded WiFi credentials:");
          Serial.println("SSID: " + String(ssid));
          Serial.println("Password: [hidden]");
          return;
        }
      }
    }
  }
  
  Serial.println("No valid WiFi credentials found.");
  getWiFiCredentials(); // Prompt the user to enter WiFi credentials
}

void saveWiFiCredentials(const char* ssid, const char* password) {
  File file = SPIFFS.open(WIFI_FILE, "w"); // Open the WiFi credentials file in write mode
  if (file) {
    StaticJsonDocument<256> doc; // Create a JSON document to store the credentials
    doc["ssid"] = ssid; // Set the SSID in the JSON document
    doc["password"] = password; // Set the password in the JSON document
    serializeJson(doc, file); // Serialize the JSON document and write it to the file
    file.close(); // Close the file
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

  saveWiFiCredentials(ssid.c_str(), password.c_str()); // Save the entered WiFi credentials to a file
  WiFi.begin(ssid.c_str(), password.c_str()); // Connect to WiFi using the entered credentials
}

void connectToWiFi() {
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { // Try to connect to WiFi for a maximum of 20 attempts
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    //Serial.println("IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nFailed to connect. Please check your credentials.");
    getWiFiCredentials(); // Prompt the user to enter WiFi credentials again
    connectToWiFi();  // Try to connect again with new credentials
  }
}
bool checkForUpdates() {
  Serial.println("Checking for updates...");
  HTTPClient http;
  http.begin(github_raw_url); // Connect to the update server

  int httpCode = http.GET(); // Send a GET request to the server
  Serial.printf("HTTP response code: %d\n", httpCode);
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString(); // Get the response payload
    Serial.println("Received payload: " + payload);
    
    StaticJsonDocument<512> doc; // Create a JSON document to parse the payload
    DeserializationError error = deserializeJson(doc, payload); // Deserialize the JSON data from the payload
    
    if (!error) {
      const char* new_version = doc["version"]; // Get the new version from the JSON document
      const char* firmware_url = doc["url"]; // Get the firmware URL from the JSON document
      String current_version = getCurrentVersion(); // Get the current firmware version
      Serial.printf("Current version: %s\n", current_version.c_str());
      Serial.printf("Available version: %s\n", new_version);
      
      if (String(new_version) > current_version) { // Compare the versions to check if an update is available
        Serial.println("New version available.");
        //updateFirmware(firmware_url, new_version); // Uncomment this line to perform the firmware update
        http.end(); // Close the HTTP connection
        return true;
      } else {
        Serial.println("Firmware is up to date.");
      }
    } else {
      Serial.println("JSON parsing failed");
    }
  } else {
    Serial.printf("Failed to connect to update server. Error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end(); // Close the HTTP connection
  return false;
}

void updateFirmware(const char* firmware_url, const char* new_version) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(firmware_url); // Connect to the firmware URL
  int httpCode = http.GET(); // Send a GET request to download the firmware
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize(); // Get the content length of the firmware
    if (contentLength > 0) {
      bool canBegin = Update.begin(contentLength); // Begin the OTA update process
      if (canBegin) {
        Serial.println("Begin OTA update...");
        WiFiClient * stream = http.getStreamPtr(); // Get the HTTP stream
        size_t written = Update.writeStream(*stream); // Write the firmware to the OTA update
        if (written == contentLength) {
          Serial.println("OTA update written successfully");
          if (Update.end()) { // Finish the OTA update process
            Serial.println("OTA update completed successfully");
            saveCurrentVersion(new_version); // Save the new version to a file
            Serial.println("Rebooting...");
            ESP.restart(); // Restart the ESP8266
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
      Serial.println("Error: Firmware content length is 0");
    }
  } else {
    Serial.printf("Firmware download failed, HTTP error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end(); // Close the HTTP connection
}

/*
For the following function getUserConfirmation, I have used a user keyboard input of either 0 or 1 to confirm or decline the update.
In the future this will be replaced with an actual physical button so please adjust the code accordingly.
Thanks.
ASE >:)
*/

bool getUserConfirmation() {
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

void performUpdate() {
  HTTPClient http;
  http.begin(github_raw_url); // Connect to the update server
  int httpCode = http.GET(); // Send a GET request to the server
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString(); // Get the response payload
    StaticJsonDocument<512> doc; // Create a JSON document to parse the payload
    DeserializationError error = deserializeJson(doc, payload); // Deserialize the JSON data from the payload
    
    if (!error) {
      const char* new_version = doc["version"]; // Get the new version from the JSON document
      const char* firmware_url = doc["url"]; // Get the firmware URL from the JSON document
      Serial.printf("New version: %s\n", new_version);
      Serial.printf("Firmware URL: %s\n", firmware_url);
      updateFirmware(firmware_url, new_version); // Perform the firmware update
    } else {
      Serial.println("Failed to deserialize JSON");
    }
  } else {
    Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end(); // Close the HTTP connection
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected. Checking for updates...");
    if(checkForUpdates()) { // Check for firmware updates
      if (getUserConfirmation()) { // Ask the user for confirmation to update
        performUpdate(); // Perform the firmware update
      }
    }
  } else {
    Serial.println("WiFi connection lost. Reconnecting...");
    connectToWiFi(); // Reconnect to WiFi
  }
  
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap()); // Print the amount of free heap memory
  Serial.println("Waiting for next update check...");
  delay(60000); // Check for updates every minute
}