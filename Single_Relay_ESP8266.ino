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
String device_id;                          // Unique device identifier

// Dynamic MQTT topics based on device ID
String mqtt_topic_command;
String mqtt_topic_status;
String mqtt_topic_heartbeat;
String mqtt_topic_connection;
String mqtt_topic_ota;  // New OTA topic

// Pin definitions for ESP8266
const int RELAY_PIN = D1;         // GPIO5 (D1) for relay control
const int LED_PIN = LED_BUILTIN;  // Built-in LED (GPIO2)
const int BUTTON_PIN = D3;        // GPIO0 (D3) for manual button

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
String currentVersion = "3.2";  // Updated version
String latestVersion = "";
String otaUrl = "";
unsigned long otaStartTime = 0;
const unsigned long otaTimeout = 300000;  // 5 minutes timeout for OTA

// Global variables to track OTA status from callbacks
volatile bool otaProgressChanged = false;
volatile int lastOTAProgress = 0;
volatile bool otaErrorOccurred = false;
volatile bool otaCompleted = false;
String otaErrorMessage = "";

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
void handleOTACallbacks();
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
void print_Deviceinfo();
void print_Mqtt_Topics();

// State variables
bool relayState = false;
unsigned long lastHeartbeat = 0;
unsigned long lastWiFiCheck = 0;
unsigned long bootTime = 0;
const unsigned long heartbeatInterval = 60000;  // Send heartbeat every 60 seconds
const unsigned long wifiCheckInterval = 30000;  // Check WiFi every 30 seconds

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

  digitalWrite(RELAY_PIN, LOW);  // Start with relay OFF
  digitalWrite(LED_PIN, HIGH);   // LED OFF initially (inverted logic on ESP8266)
  print_Deviceinfo();

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
  print_Mqtt_Topics();
}
void print_Mqtt_Topics() {
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

void print_Deviceinfo() {
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
}








void loop() {
  // Check OTA progress and timeout
  if (otaInProgress) {
    checkOTAProgress();
    handleOTACallbacks();  // Handle OTA callback flags

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
      Serial.println("WiFi connection lost. Trying to reconnect...");
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
        Serial.println("WiFi reconnected!");
        digitalWrite(LED_PIN, relayState ? LOW : HIGH);
      } else {
        Serial.println();
        Serial.println("Failed to reconnect to WiFi");
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
  if (millis() - lastMemCheck > 60000) {  // Every minute
    Serial.printf("Free Heap: %u bytes, Uptime: %lu seconds, Commands: %lu, OTA Updates: %lu\n",
                  ESP.getFreeHeap(), millis() / 1000, totalCommands, totalOTAUpdates);

    // Check for low memory
    if (ESP.getFreeHeap() < 10000) {
      Serial.println("Warning: Low memory!");
      publishError("Low memory: " + String(ESP.getFreeHeap()) + " bytes");

      // Cancel OTA if in progress due to low memory
      if (otaInProgress) {
        Serial.println("Cancelling OTA due to low memory");
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