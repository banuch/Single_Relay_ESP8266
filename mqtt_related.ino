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
  Serial.println("Device info published");
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
  
  bool published = client.publish((mqtt_topic_status + "/error").c_str(), errorMessage.c_str());
  if (published) {
    Serial.println("Error published: " + error);
  } else {
    Serial.println("Failed to publish error: " + error);
  }
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
  
  bool published = client.publish(mqtt_topic_heartbeat.c_str(), heartbeat.c_str());
  if (published) {
    Serial.println("Heartbeat sent");
  } else {
    Serial.println("Failed to send heartbeat");
  }
}


void publishOTAStatus(String status, int progress) {
  if (!client.connected()) {
    Serial.println("Cannot publish OTA status - MQTT not connected");
    return;
  }
  
  StaticJsonDocument<350> doc;
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
  
  bool published = client.publish(mqtt_topic_ota.c_str(), otaMessage.c_str());
  
  if (published) {
    Serial.println("OTA Status Published: " + status + (progress >= 0 ? " (" + String(progress) + "%)" : ""));
    Serial.println("Published to topic: " + String(mqtt_topic_ota.c_str()));
    Serial.println("Message content: " + otaMessage);
  } else {
    Serial.println("Failed to publish OTA status: " + status);
  }
  
  // Force MQTT to process
  client.loop();
}


void reconnectMQTT() {
  int attempts = 0;
  while (!client.connected() && attempts < 5) {
    Serial.print("Attempting MQTT connection... ");
    
    if (client.connect(mqtt_client_id.c_str())) {
      totalReconnects++;
      Serial.println("Connected to MQTT broker!");
      Serial.println("Client ID: " + mqtt_client_id);
      
      // Subscribe to command topics
      client.subscribe(mqtt_topic_command.c_str());
      client.subscribe(mqtt_topic_ota.c_str());  // Subscribe to OTA topic
      Serial.println("Subscribed to: " + mqtt_topic_command);
      Serial.println("Subscribed to: " + mqtt_topic_ota);
      
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
      Serial.print("Failed to connect, rc=");
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
void publishStatus(String customStatus) {
  if (!client.connected()) {
    Serial.println("Cannot publish status - MQTT not connected");
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
  
  bool published = client.publish(mqtt_topic_status.c_str(), statusMessage.c_str());
  if (published) {
    Serial.println("Status published: " + statusMessage);
  } else {
    Serial.println("Failed to publish status");
  }
}
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  
  // Convert payload to string
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Message: ");
  Serial.println(message);
  
  // Process command based on topic
  if (String(topic) == mqtt_topic_command) {
    totalCommands++;
    processCommand(message);
  } else if (String(topic) == mqtt_topic_ota) {
    processOTACommand(message);
  }
}
