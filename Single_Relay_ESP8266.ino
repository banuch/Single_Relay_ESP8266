#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

// WiFiManager instance
WiFiManager wifiManager;

// MQTT Broker settings - these can also be made configurable via WiFiManager
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
String mqtt_client_id = "ESP8266_Relay_";  // Will append chip ID
String device_id;  // Unique device identifier

// Dynamic MQTT topics based on device ID
String mqtt_topic_command;
String mqtt_topic_status;
String mqtt_topic_heartbeat;
String mqtt_topic_connection;
String mqtt_topic_ota;  // New OTA topic

// Pin definitions for ESP8266
const int RELAY_PIN = D1;        // GPIO5 (D1) for relay control
const int LED_PIN = LED_BUILTIN; // Built-in LED (GPIO2)
const int BUTTON_PIN = D3;       // GPIO0 (D3) for manual button

WiFiClient espClient;
WiFiClientSecure secureClient;  // For HTTPS downloads
PubSubClient client(espClient);

// Custom parameters for WiFiManager
char custom_mqtt_server[40] = "broker.emqx.io";
char custom_mqtt_port[6] = "1883";
char custom_device_name[32] = "";

// Flag for saving data
bool shouldSaveConfig = false;

// OTA variables
bool otaInProgress = false;
String currentVersion = "3.2";  // Updated version with OTA support
String latestVersion = "";
String otaUrl = "";
unsigned long otaStartTime = 0;
const unsigned long otaTimeout = 300000; // 5 minutes timeout for OTA

// Function declarations
void setup_wifi_manager();
void saveConfigCallback();
void loadConfig();
void saveConfig();
void resetSettings();
void callback(char* topic, byte* payload, unsigned int length);
void processCommand(String message);
void processOTACommand(String message);
void performOTA(String url, String version);
void checkOTAProgress();
void turnRelayOn();
void turnRelayOff();
void toggleRelay();
void publishStatus(String customStatus = "");
void publishDeviceInfo();
void publishError(String error);
void publishHeartbeat();
void publishOTAStatus(String status, int progress = -1);
void reconnectMQTT();
void checkButton();
void checkEmergencyReset();
void checkConfigReset();

// State variables
bool relayState = false;
unsigned long lastHeartbeat = 0;
unsigned long lastWiFiCheck = 0;
unsigned long bootTime = 0;
const unsigned long heartbeatInterval = 60000; // Send heartbeat every 60 seconds
const unsigned long wifiCheckInterval = 30000; // Check WiFi every 30 seconds

// Statistics
unsigned long totalCommands = 0;
unsigned long totalReconnects = 0;
unsigned long totalOTAUpdates = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  bootTime = millis();
  
  // Initialize EEPROM
  EEPROM.begin(512);
  
  // Generate unique device ID
  device_id = String(ESP.getChipId(), HEX);
  mqtt_client_id += device_id;
  
  // Set default device name if empty
  if (strlen(custom_device_name) == 0) {
    strcpy(custom_device_name, ("ESP8266_" + device_id).c_str());
  }
  
  // Setup dynamic MQTT topics
  mqtt_topic_command = "home/relay/" + device_id + "/command";
  mqtt_topic_status = "home/relay/" + device_id + "/status";
  mqtt_topic_heartbeat = "home/relay/" + device_id + "/heartbeat";
  mqtt_topic_connection = "home/relay/" + device_id + "/connection";
  mqtt_topic_ota = "home/relay/" + device_id + "/ota";  // New OTA topic
  
  // Initialize pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  digitalWrite(RELAY_PIN, LOW);   // Start with relay OFF
  digitalWrite(LED_PIN, HIGH);    // LED OFF initially (inverted logic on ESP8266)
  
  Serial.println();
  Serial.println("=================================");
  Serial.println("ESP8266 MQTT Relay Controller v" + currentVersion);
  Serial.println("With WiFiManager & OTA Support");
  Serial.println("=================================");
  
  // Print device info
  Serial.printf("Device ID: %s\n", device_id.c_str());
  Serial.printf("Chip ID: %08X\n", ESP.getChipId());
  Serial.printf("Flash Size: %u bytes\n", ESP.getFlashChipRealSize());
  Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Boot Time: %lu ms\n", bootTime);
  Serial.printf("Firmware Version: %s\n", currentVersion.c_str());
  
  // Load configuration from EEPROM
  loadConfig();
  
  // Setup WiFi using WiFiManager
  setup_wifi_manager();
  
  // Setup MQTT client with larger buffer for JSON messages
  client.setBufferSize(1024);  // Increased for OTA messages
  client.setServer(custom_mqtt_server, atoi(custom_mqtt_port));
  client.setCallback(callback);
  
  // Setup secure client for HTTPS
  secureClient.setInsecure();  // For simplicity - use proper certificates in production
  
  // Print MQTT topics
  Serial.println("MQTT Configuration:");
  Serial.printf("  Server: %s:%s\n", custom_mqtt_server, custom_mqtt_port);
  Serial.println("  Command Topic: " + mqtt_topic_command);
  Serial.println("  Status Topic: " + mqtt_topic_status);
  Serial.println("  Heartbeat Topic: " + mqtt_topic_heartbeat);
  Serial.println("  OTA Topic: " + mqtt_topic_ota);
  
  Serial.println("Setup completed!");
  Serial.println("=================================");
  Serial.println("Hold button for 10+ seconds to reset WiFi settings");
  Serial.println("OTA Commands: update, check_version, force_update");
  Serial.println("=================================");
}

void setup_wifi_manager() {
  // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("🔧 Entered config mode");
    Serial.println("📶 Connect to WiFi: " + String(myWiFiManager->getConfigPortalSSID()));
    Serial.println("🌐 Open: http://192.168.4.1");
    
    // Blink LED rapidly in config mode
    for (int i = 0; i < 20; i++) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(100);
    }
  });

  // Set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Add custom parameters
  WiFiManagerParameter custom_mqtt_server_param("server", "MQTT Server", custom_mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port_param("port", "MQTT Port", custom_mqtt_port, 6);
  WiFiManagerParameter custom_device_name_param("device_name", "Device Name", custom_device_name, 32);
  
  wifiManager.addParameter(&custom_mqtt_server_param);
  wifiManager.addParameter(&custom_mqtt_port_param);
  wifiManager.addParameter(&custom_device_name_param);

  // Set custom AP name and password
  String ap_name = "ESP8266_Relay_" + device_id;
  String ap_pass = "relay123";  // You can make this more secure
  
  // Set timeouts
  wifiManager.setConfigPortalTimeout(300); // 5 minutes timeout
  wifiManager.setConnectTimeout(30);       // 30 seconds to connect
  
  // Set custom HTML styling
  wifiManager.setCustomHeadElement("<style>.c{text-align: center;} div,input{padding:5px;font-size:1em;} input{width:95%;} body{text-align: center;font-family:verdana;} button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;} .q{float: right;width: 64px;text-align: right;} .l{background: url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAALVBMVEX///8EBwfBwsLw8PAzNjaCg4NTVVUjJiZDRUUUFxdiZGSho6OSk5Pg4eFydHTCjaf3AAAAZElEQVQ4je2NSw7AIAhEBamKn97/uMXEGBvozkWb9C2Zx4xzWykBhFAeYp9gkLyZE0zIMno9n4g19hmdY39scwqVkOXaxph0ZCXQcqxSpgQpONa59wkRDOL93eAXvimwlbPbwwVAegLS1HGfZAAAAABJRU5ErkJggg==') no-repeat left center;background-size: 1em;}</style>");
  
  Serial.println("🔧 Starting WiFiManager...");
  Serial.printf("📶 AP Name: %s\n", ap_name.c_str());
  Serial.printf("🔐 AP Password: %s\n", ap_pass.c_str());
  
  // Try to connect to WiFi, if it fails start configuration portal
  if (!wifiManager.autoConnect(ap_name.c_str(), ap_pass.c_str())) {
    Serial.println("❌ Failed to connect and hit timeout");
    digitalWrite(LED_PIN, LOW); // Turn on LED to show error
    delay(3000);
    ESP.restart();
  }
  
  // If we get here, WiFi is connected
  digitalWrite(LED_PIN, HIGH); // Turn off LED when connected
  Serial.println("✓ WiFi connected successfully!");
  Serial.print("📶 SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("🌐 IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("📶 Signal Strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  
  // Read updated parameters
  strcpy(custom_mqtt_server, custom_mqtt_server_param.getValue());
  strcpy(custom_mqtt_port, custom_mqtt_port_param.getValue());
  strcpy(custom_device_name, custom_device_name_param.getValue());
  
  // Save the custom parameters to EEPROM if they were changed
  if (shouldSaveConfig) {
    saveConfig();
    shouldSaveConfig = false;
  }
}

void saveConfigCallback() {
  Serial.println("💾 Should save config");
  shouldSaveConfig = true;
}

void loadConfig() {
  Serial.println("📖 Loading configuration...");
  
  // Check if config exists
  if (EEPROM.read(0) == 'C' && EEPROM.read(1) == 'F' && EEPROM.read(2) == 'G') {
    Serial.println("✓ Configuration found, loading...");
    
    // Read MQTT server
    for (int i = 0; i < 40; ++i) {
      custom_mqtt_server[i] = EEPROM.read(3 + i);
    }
    
    // Read MQTT port
    for (int i = 0; i < 6; ++i) {
      custom_mqtt_port[i] = EEPROM.read(43 + i);
    }
    
    // Read device name
    for (int i = 0; i < 32; ++i) {
      custom_device_name[i] = EEPROM.read(49 + i);
    }
    
    Serial.printf("📡 MQTT Server: %s\n", custom_mqtt_server);
    Serial.printf("🔢 MQTT Port: %s\n", custom_mqtt_port);
    Serial.printf("🏷️ Device Name: %s\n", custom_device_name);
  } else {
    Serial.println("ℹ️ No configuration found, using defaults");
  }
}

void saveConfig() {
  Serial.println("💾 Saving configuration...");
  
  // Write config marker
  EEPROM.write(0, 'C');
  EEPROM.write(1, 'F');
  EEPROM.write(2, 'G');
  
  // Write MQTT server
  for (int i = 0; i < 40; ++i) {
    EEPROM.write(3 + i, custom_mqtt_server[i]);
  }
  
  // Write MQTT port
  for (int i = 0; i < 6; ++i) {
    EEPROM.write(43 + i, custom_mqtt_port[i]);
  }
  
  // Write device name
  for (int i = 0; i < 32; ++i) {
    EEPROM.write(49 + i, custom_device_name[i]);
  }
  
  EEPROM.commit();
  Serial.println("✓ Configuration saved!");
}

void resetSettings() {
  Serial.println("🔄 Resetting WiFi settings...");
  
  // Clear EEPROM config
  for (int i = 0; i < 100; ++i) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  
  // Reset WiFiManager settings
  wifiManager.resetSettings();
  
  Serial.println("✓ Settings reset! Restarting...");
  publishStatus("WIFI_RESET");
  delay(1000);
  ESP.restart();
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📥 Message received on topic: ");
  Serial.println(topic);
  
  // Convert payload to string
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("📄 Message: ");
  Serial.println(message);
  
  // Process command based on topic
  if (String(topic) == mqtt_topic_command) {
    totalCommands++;
    processCommand(message);
  } else if (String(topic) == mqtt_topic_ota) {
    processOTACommand(message);
  }
}

void processCommand(String message) {
  message.toLowerCase();
  message.trim();
  
  if (message == "on" || message == "1" || message == "true") {
    turnRelayOn();
  } else if (message == "off" || message == "0" || message == "false") {
    turnRelayOff();
  } else if (message == "toggle") {
    toggleRelay();
  } else if (message == "status") {
    publishStatus();
  } else if (message == "info") {
    publishDeviceInfo();
  } else if (message == "restart") {
    Serial.println("🔄 Restart command received");
    publishStatus("RESTARTING");
    delay(1000);
    ESP.restart();
  } else if (message == "reset_wifi") {
    Serial.println("📶 WiFi reset command received");
    resetSettings();
  } else {
    Serial.println("❌ Unknown command: " + message);
    publishError("Unknown command: " + message);
  }
}

void processOTACommand(String message) {
  if (otaInProgress) {
    publishOTAStatus("OTA_BUSY", 0);
    Serial.println("⚠️ OTA already in progress");
    return;
  }
  
  // Parse JSON message
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.println("❌ Failed to parse OTA JSON: " + String(error.c_str()));
    publishError("Invalid OTA JSON: " + String(error.c_str()));
    return;
  }
  
  String command = doc["command"];
  command.toLowerCase();
  
  if (command == "update" || command == "force_update") {
    String url = doc["url"];
    String version = doc["version"];
    
    if (url.length() == 0) {
      publishError("OTA URL is required");
      return;
    }
    
    if (version.length() == 0) {
      version = "unknown";
    }
    
    // Check if we should update
    if (command == "update" && version == currentVersion) {
      publishOTAStatus("OTA_SKIP", 0);
      Serial.println("ℹ️ Already on version " + version + ", skipping update");
      return;
    }
    
    Serial.println("🚀 Starting OTA update...");
    Serial.println("📦 URL: " + url);
    Serial.println("🏷️ Version: " + version);
    
    performOTA(url, version);
    
  } else if (command == "check_version") {
    publishOTAStatus("VERSION_CHECK", 0);
    Serial.println("🔍 Current version: " + currentVersion);
    
  } else if (command == "cancel") {
    if (otaInProgress) {
      otaInProgress = false;
      publishOTAStatus("OTA_CANCELLED", 0);
      Serial.println("🛑 OTA update cancelled");
    }
    
  } else {
    publishError("Unknown OTA command: " + command);
  }
}

void performOTA(String url, String version) {
  otaInProgress = true;
  otaUrl = url;
  latestVersion = version;
  otaStartTime = millis();
  
  publishOTAStatus("OTA_STARTING", 0);
  
  // Disable relay during update for safety
  if (relayState) {
    turnRelayOff();
    Serial.println("🔒 Relay disabled for safety during OTA");
  }
  
  // Set up progress callback
  ESPhttpUpdate.onProgress([](int cur, int total) {
    static unsigned long lastProgress = 0;
    int progress = (cur * 100) / total;
    
    // Only publish progress every 10% to avoid spam
    if (millis() - lastProgress > 2000 || progress == 100) {
      Serial.printf("OTA Progress: %d%% (%d/%d bytes)\n", progress, cur, total);
      publishOTAStatus("OTA_PROGRESS", progress);
      lastProgress = millis();
    }
    
    // Blink LED during update
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  });
  
  // Set up error callback
  ESPhttpUpdate.onError([](int error) {
    String errorMsg = "OTA Error " + String(error) + ": ";
    switch(error) {
      case HTTP_UE_TOO_LESS_SPACE:
        errorMsg += "Not enough space";
        break;
      case HTTP_UE_SERVER_NOT_REPORT_SIZE:
        errorMsg += "Server doesn't report size";
        break;
      case HTTP_UE_SERVER_FILE_NOT_FOUND:
        errorMsg += "File not found";
        break;
      case HTTP_UE_SERVER_FORBIDDEN:
        errorMsg += "Forbidden";
        break;
      case HTTP_UE_SERVER_WRONG_HTTP_CODE:
        errorMsg += "Wrong HTTP code";
        break;
      case HTTP_UE_SERVER_FAULTY_MD5:
        errorMsg += "Faulty MD5";
        break;
      case HTTP_UE_BIN_VERIFY_HEADER_FAILED:
        errorMsg += "Verify header failed";
        break;
      case HTTP_UE_BIN_FOR_WRONG_FLASH:
        errorMsg += "Wrong flash config";
        break;
      default:
        errorMsg += "Unknown error";
        break;
    }
    
    Serial.println("❌ " + errorMsg);
    publishOTAStatus("OTA_ERROR", 0);
    publishError(errorMsg);
  });
  
  // Set up end callback
  ESPhttpUpdate.onEnd([]() {
    Serial.println("✓ OTA Update completed successfully!");
    publishOTAStatus("OTA_SUCCESS", 100);
    totalOTAUpdates++;
  });
  
  // Perform the update
  WiFiClient client;
  t_httpUpdate_return result;
  
  if (url.startsWith("https://")) {
    result = ESPhttpUpdate.update(secureClient, url);
  } else {
    result = ESPhttpUpdate.update(client, url);
  }
  
  // Handle result
  switch(result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("❌ OTA Update failed. Error (%d): %s\n", 
                   ESPhttpUpdate.getLastError(), 
                   ESPhttpUpdate.getLastErrorString().c_str());
      publishOTAStatus("OTA_FAILED", 0);
      publishError("OTA failed: " + ESPhttpUpdate.getLastErrorString());
      break;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("ℹ️ OTA: No updates available");
      publishOTAStatus("OTA_NO_UPDATE", 0);
      break;
      
    case HTTP_UPDATE_OK:
      Serial.println("✓ OTA Update OK! Restarting...");
      publishOTAStatus("OTA_COMPLETE", 100);
      delay(1000);
      ESP.restart();
      break;
  }
  
  otaInProgress = false;
}

void checkOTAProgress() {
  if (otaInProgress && (millis() - otaStartTime > otaTimeout)) {
    Serial.println("⏰ OTA timeout reached");
    publishOTAStatus("OTA_TIMEOUT", 0);
    publishError("OTA update timeout");
    otaInProgress = false;
  }
}

void turnRelayOn() {
  if (otaInProgress) {
    Serial.println("🔒 Relay control disabled during OTA");
    publishError("Relay control disabled during OTA");
    return;
  }
  
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, LOW);  // Turn LED on (inverted logic)
  relayState = true;
  Serial.println("🔛 Relay turned ON");
  publishStatus();
  
  // Visual feedback - quick blinks
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }
}

void turnRelayOff() {
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH); // Turn LED off (inverted logic)
  relayState = false;
  Serial.println("🔴 Relay turned OFF");
  publishStatus();
}

void toggleRelay() {
  if (relayState) {
    turnRelayOff();
  } else {
    turnRelayOn();
  }
  Serial.println("🔄 Relay toggled");
}

void publishStatus(String customStatus) {
  if (!client.connected()) {
    Serial.println("❌ Cannot publish status - MQTT not connected");
    return;
  }
  
  StaticJsonDocument<400> doc;
  doc["device_id"] = device_id;
  doc["device_name"] = custom_device_name;
  doc["relay"] = relayState ? "ON" : "OFF";
  doc["uptime"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["ssid"] = WiFi.SSID();
  doc["firmware_version"] = currentVersion;
  doc["ota_in_progress"] = otaInProgress;
  doc["timestamp"] = millis();
  
  if (customStatus != "") {
    doc["status"] = customStatus;
  }
  
  String statusMessage;
  serializeJson(doc, statusMessage);
  
  client.publish(mqtt_topic_status.c_str(), statusMessage.c_str());
  Serial.println("📤 Status published: " + statusMessage);
}

void publishDeviceInfo() {
  if (!client.connected()) return;
  
  StaticJsonDocument<550> doc;
  doc["device_id"] = device_id;
  doc["device_name"] = custom_device_name;
  doc["chip_id"] = String(ESP.getChipId(), HEX);
  doc["flash_size"] = ESP.getFlashChipRealSize();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["boot_time"] = bootTime;
  doc["uptime"] = millis() / 1000;
  doc["wifi_ssid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["mac"] = WiFi.macAddress();
  doc["rssi"] = WiFi.RSSI();
  doc["mqtt_server"] = custom_mqtt_server;
  doc["mqtt_port"] = atoi(custom_mqtt_port);
  doc["relay_state"] = relayState ? "ON" : "OFF";
  doc["total_commands"] = totalCommands;
  doc["total_reconnects"] = totalReconnects;
  doc["total_ota_updates"] = totalOTAUpdates;
  doc["firmware_version"] = currentVersion;
  doc["ota_capable"] = true;
  doc["ota_in_progress"] = otaInProgress;
  
  String infoMessage;
  serializeJson(doc, infoMessage);
  
  client.publish((mqtt_topic_status + "/info").c_str(), infoMessage.c_str());
  Serial.println("📊 Device info published");
}

void publishError(String error) {
  if (!client.connected()) return;
  
  StaticJsonDocument<250> doc;
  doc["device_id"] = device_id;
  doc["device_name"] = custom_device_name;
  doc["error"] = error;
  doc["firmware_version"] = currentVersion;
  doc["timestamp"] = millis();
  
  String errorMessage;
  serializeJson(doc, errorMessage);
  
  client.publish((mqtt_topic_status + "/error").c_str(), errorMessage.c_str());
  Serial.println("❌ Error published: " + error);
}

void publishHeartbeat() {
  if (!client.connected()) return;
  
  StaticJsonDocument<350> doc;
  doc["device_id"] = device_id;
  doc["device_name"] = custom_device_name;
  doc["status"] = "alive";
  doc["uptime"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  doc["relay"] = relayState ? "ON" : "OFF";
  doc["commands_processed"] = totalCommands;
  doc["firmware_version"] = currentVersion;
  doc["ota_updates"] = totalOTAUpdates;
  doc["ota_in_progress"] = otaInProgress;
  doc["timestamp"] = millis();
  
  String heartbeat;
  serializeJson(doc, heartbeat);
  
  client.publish(mqtt_topic_heartbeat.c_str(), heartbeat.c_str());
  Serial.println("💓 Heartbeat sent");
}

void publishOTAStatus(String status, int progress) {
  if (!client.connected()) return;
  
  StaticJsonDocument<300> doc;
  doc["device_id"] = device_id;
  doc["device_name"] = custom_device_name;
  doc["ota_status"] = status;
  doc["current_version"] = currentVersion;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["timestamp"] = millis();
  
  if (progress >= 0) {
    doc["progress"] = progress;
  }
  
  if (latestVersion.length() > 0) {
    doc["target_version"] = latestVersion;
  }
  
  if (otaUrl.length() > 0) {
    doc["ota_url"] = otaUrl;
  }
  
  String otaMessage;
  serializeJson(doc, otaMessage);
  
  client.publish(mqtt_topic_ota.c_str(), otaMessage.c_str());
  Serial.println("🚀 OTA Status: " + status + (progress >= 0 ? " (" + String(progress) + "%)" : ""));
}

void reconnectMQTT() {
  int attempts = 0;
  while (!client.connected() && attempts < 5) {
    Serial.print("🔄 Attempting MQTT connection... ");
    
    if (client.connect(mqtt_client_id.c_str())) {
      totalReconnects++;
      Serial.println("✓ Connected to MQTT broker!");
      Serial.println("Client ID: " + mqtt_client_id);
      
      // Subscribe to command topics
      client.subscribe(mqtt_topic_command.c_str());
      client.subscribe(mqtt_topic_ota.c_str());  // Subscribe to OTA topic
      Serial.println("📝 Subscribed to: " + mqtt_topic_command);
      Serial.println("📝 Subscribed to: " + mqtt_topic_ota);
      
      // Publish connection status
      StaticJsonDocument<300> doc;
      doc["device_id"] = device_id;
      doc["device_name"] = custom_device_name;
      doc["status"] = "connected";
      doc["ip"] = WiFi.localIP().toString();
      doc["uptime"] = millis() / 1000;
      doc["firmware_version"] = currentVersion;
      doc["ota_capable"] = true;
      
      String connectMsg;
      serializeJson(doc, connectMsg);
      client.publish(mqtt_topic_connection.c_str(), connectMsg.c_str());
      
      // Publish initial status
      publishStatus();
      
    } else {
      Serial.print("❌ Failed to connect, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      
      // Blink LED to show connection attempt
      for (int i = 0; i < 10; i++) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(250);
      }
      attempts++;
    }
  }
}

void checkButton() {
  static unsigned long lastButtonPress = 0;
  static bool lastButtonState = HIGH;
  
  bool buttonState = digitalRead(BUTTON_PIN);
  
  if (buttonState != lastButtonState && buttonState == LOW) {
    if (millis() - lastButtonPress > 300) {  // Debounce
      Serial.println("🔘 Manual button pressed");
      
      // Don't allow manual control during OTA
      if (otaInProgress) {
        Serial.println("🔒 Manual control disabled during OTA");
        // Quick error blink
        for (int i = 0; i < 6; i++) {
          digitalWrite(LED_PIN, !digitalRead(LED_PIN));
          delay(100);
        }
      } else {
        toggleRelay();
      }
      
      lastButtonPress = millis();
    }
  }
  
  lastButtonState = buttonState;
}

void checkEmergencyReset() {
  static unsigned long buttonPressStart = 0;
  static bool buttonPressed = false;
  
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressStart = millis();
    } else if (millis() - buttonPressStart > 5000) {
      Serial.println("🚨 Emergency reset triggered!");
      publishStatus("EMERGENCY_RESET");
      delay(500);
      ESP.restart();
    }
  } else {
    buttonPressed = false;
  }
}

void checkConfigReset() {
  static unsigned long buttonPressStart = 0;
  static bool buttonPressed = false;
  static bool resetWarningShown = false;
  
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      resetWarningShown = false;
      buttonPressStart = millis();
    } else if (millis() - buttonPressStart > 10000 && !resetWarningShown) {
      Serial.println("🔧 WiFi reset in progress...");
      publishStatus("WIFI_RESETTING");
      resetWarningShown = true;
      
      // Rapid LED blinking for feedback
      for (int i = 0; i < 20; i++) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(50);
      }
      
      resetSettings();
    }
  } else {
    buttonPressed = false;
    resetWarningShown = false;
  }
}

void loop() {
  // Check OTA progress and timeout
  if (otaInProgress) {
    checkOTAProgress();
    
    // Blink LED slowly during OTA
    static unsigned long lastOTABlink = 0;
    if (millis() - lastOTABlink > 1000) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      lastOTABlink = millis();
    }
  }
  
  // Check WiFi connection periodically
  if (millis() - lastWiFiCheck > wifiCheckInterval) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("📶 WiFi connection lost. Trying to reconnect...");
      digitalWrite(LED_PIN, LOW);
      
      // Try to reconnect without starting config portal
      WiFi.begin();
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(500);
        Serial.print(".");
        attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.println("✓ WiFi reconnected!");
        digitalWrite(LED_PIN, relayState ? LOW : HIGH);
      } else {
        Serial.println();
        Serial.println("❌ Failed to reconnect to WiFi");
      }
    }
    lastWiFiCheck = millis();
  }
  
  // Check MQTT connection
  if (!client.connected()) {
    digitalWrite(LED_PIN, LOW);
    reconnectMQTT();
    if (client.connected()) {
      digitalWrite(LED_PIN, relayState ? LOW : HIGH);
    }
  }
  
  // Process MQTT messages
  client.loop();
  
  // Check manual button (disabled during OTA)
  if (!otaInProgress) {
    checkButton();
    
    // Check for emergency reset (5 seconds)
    checkEmergencyReset();
    
    // Check for WiFi config reset (10 seconds)
    checkConfigReset();
  }
  
  // Send periodic heartbeat
  if (millis() - lastHeartbeat > heartbeatInterval) {
    publishHeartbeat();
    lastHeartbeat = millis();
  }
  
  // Monitor memory usage
  static unsigned long lastMemCheck = 0;
  if (millis() - lastMemCheck > 60000) { // Every minute
    Serial.printf("💾 Free Heap: %u bytes, Uptime: %lu seconds, Commands: %lu, OTA Updates: %lu\n", 
                  ESP.getFreeHeap(), millis()/1000, totalCommands, totalOTAUpdates);
    
    // Check for low memory
    if (ESP.getFreeHeap() < 10000) {
      Serial.println("⚠️ Warning: Low memory!");
      publishError("Low memory: " + String(ESP.getFreeHeap()) + " bytes");
      
      // Cancel OTA if in progress due to low memory
      if (otaInProgress) {
        Serial.println("🛑 Cancelling OTA due to low memory");
        otaInProgress = false;
        publishOTAStatus("OTA_CANCELLED", 0);
        publishError("OTA cancelled due to low memory");
      }
    }
    
    lastMemCheck = millis();
  }
  
  // Watchdog feed
  yield();
  delay(10);
}