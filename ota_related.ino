void processOTACommand(String message) {
  if (otaInProgress) {
    publishOTAStatus("OTA_BUSY", 0);
    Serial.println("OTA already in progress");
    return;
  }
  
  // Debug: Print raw message
  Serial.println("Raw OTA message: " + message);
  
  // Parse JSON message
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.println("Failed to parse OTA JSON: " + String(error.c_str()));
    publishError("Invalid OTA JSON: " + String(error.c_str()));
    return;
  }
  
  // Debug: Print parsed JSON
  String debugJson;
  serializeJson(doc, debugJson);
  Serial.println("Parsed JSON: " + debugJson);
  
  // Check if command exists and is not null
  if (!doc.containsKey("command") || doc["command"].isNull()) {
    Serial.println("Command field is missing or null");
    publishError("OTA command field is missing or null");
    return;
  }
  
  String command = doc["command"].as<String>();  // Use as<String>() for safety
  command.toLowerCase();
  command.trim();  // Remove any whitespace
  
  Serial.println("Processing OTA command: '" + command + "'");
  
  if (command == "update" || command == "force_update") {
    // Check if URL exists
    if (!doc.containsKey("url") || doc["url"].isNull()) {
      Serial.println("URL field is missing or null");
      publishError("OTA URL is required");
      return;
    }
    
    String url = doc["url"].as<String>();
    String version = doc.containsKey("version") ? doc["version"].as<String>() : "unknown";
    
    if (url.length() == 0) {
      publishError("OTA URL is empty");
      return;
    }
    
    // Check if we should update
    if (command == "update" && version == currentVersion) {
      publishOTAStatus("OTA_SKIP", 0);
      Serial.println("Already on version " + version + ", skipping update");
      return;
    }
    
    Serial.println("Starting OTA update...");
    Serial.println("URL: " + url);
    Serial.println("Version: " + version);
    
    performOTA(url, version);
    
  } else if (command == "check_version") {
    publishOTAStatus("VERSION_CHECK", 0);
    Serial.println("Current version: " + currentVersion);
    
  } else if (command == "cancel") {
    if (otaInProgress) {
      otaInProgress = false;
      publishOTAStatus("OTA_CANCELLED", 0);
      Serial.println("OTA update cancelled");
    } else {
      Serial.println("No OTA in progress to cancel");
    }
    
  } else {
    Serial.println("Unknown OTA command: '" + command + "'");
    publishError("Unknown OTA command: " + command);
  }
}

void performOTA(String url, String version) {
  otaInProgress = true;
  otaUrl = url;
  latestVersion = version;
  otaStartTime = millis();
  
  // Reset callback flags
  otaProgressChanged = false;
  otaErrorOccurred = false;
  otaCompleted = false;
  lastOTAProgress = 0;
  otaErrorMessage = "";
  
  Serial.println("Publishing OTA_STARTING status...");
  publishOTAStatus("OTA_STARTING", 0);
  delay(100); // Give time for MQTT message to be sent
  
  // Disable relay during update for safety
  if (relayState) {
    turnRelayOff();
    Serial.println("Relay disabled for safety during OTA");
  }
  
  // Set up progress callback - use flags instead of direct MQTT calls
  ESPhttpUpdate.onProgress([](int cur, int total) {
    static unsigned long lastProgressTime = 0;
    int progress = (cur * 100) / total;
    
    // Only update progress every 10% or every 2 seconds to avoid spam
    if (millis() - lastProgressTime > 2000 || progress == 100 || progress != lastOTAProgress) {
      Serial.printf("OTA Progress: %d%% (%d/%d bytes)\n", progress, cur, total);
      lastOTAProgress = progress;
      otaProgressChanged = true;
      lastProgressTime = millis();
    }
    
    // Blink LED during update
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  });
  
  // Set up error callback - use flags instead of direct MQTT calls
  ESPhttpUpdate.onError([](int error) {
    otaErrorMessage = "OTA Error " + String(error) + ": ";
    switch(error) {
      case HTTP_UE_TOO_LESS_SPACE:
        otaErrorMessage += "Not enough space";
        break;
      case HTTP_UE_SERVER_NOT_REPORT_SIZE:
        otaErrorMessage += "Server doesn't report size";
        break;
      case HTTP_UE_SERVER_FILE_NOT_FOUND:
        otaErrorMessage += "File not found";
        break;
      case HTTP_UE_SERVER_FORBIDDEN:
        otaErrorMessage += "Forbidden";
        break;
      case HTTP_UE_SERVER_WRONG_HTTP_CODE:
        otaErrorMessage += "Wrong HTTP code";
        break;
      case HTTP_UE_SERVER_FAULTY_MD5:
        otaErrorMessage += "Faulty MD5";
        break;
      case HTTP_UE_BIN_VERIFY_HEADER_FAILED:
        otaErrorMessage += "Verify header failed";
        break;
      case HTTP_UE_BIN_FOR_WRONG_FLASH:
        otaErrorMessage += "Wrong flash config";
        break;
      default:
        otaErrorMessage += "Unknown error";
        break;
    }
    
    Serial.println(otaErrorMessage);
    otaErrorOccurred = true;
  });
  
  // Set up end callback - use flags instead of direct MQTT calls
  ESPhttpUpdate.onEnd([]() {
    Serial.println("OTA Update completed successfully!");
    otaCompleted = true;
    totalOTAUpdates++;
  });
  
  // Perform the update
  WiFiClient updateClient;
  t_httpUpdate_return result;
  
  Serial.println("Starting download from: " + url);
  
  if (url.startsWith("https://")) {
    result = ESPhttpUpdate.update(secureClient, url);
  } else {
    result = ESPhttpUpdate.update(updateClient, url);
  }
  
  // Handle result and publish status accordingly
  switch(result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA Update failed. Error (%d): %s\n", 
                   ESPhttpUpdate.getLastError(), 
                   ESPhttpUpdate.getLastErrorString().c_str());
      publishOTAStatus("OTA_FAILED", 0);
      publishError("OTA failed: " + ESPhttpUpdate.getLastErrorString());
      break;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("OTA: No updates available");
      publishOTAStatus("OTA_NO_UPDATE", 0);
      break;
      
    case HTTP_UPDATE_OK:
      Serial.println("OTA Update OK! Publishing success and restarting...");
      publishOTAStatus("OTA_COMPLETE", 100);
      delay(1000);
      ESP.restart();
      break;
  }
  
  otaInProgress = false;
}


// Handle OTA status updates in the main loop
void handleOTACallbacks() {
  if (!otaInProgress) return;
  
  // Handle progress updates
  if (otaProgressChanged) {
    publishOTAStatus("OTA_PROGRESS", lastOTAProgress);
    otaProgressChanged = false;
  }
  
  // Handle errors
  if (otaErrorOccurred) {
    publishOTAStatus("OTA_ERROR", 0);
    publishError(otaErrorMessage);
    otaErrorOccurred = false;
    otaInProgress = false; // Stop OTA process
  }
  
  // Handle completion
  if (otaCompleted) {
    publishOTAStatus("OTA_SUCCESS", 100);
    otaCompleted = false;
  }
}

void checkOTAProgress() {
  if (otaInProgress && (millis() - otaStartTime > otaTimeout)) {
    Serial.println("OTA timeout reached");
    publishOTAStatus("OTA_TIMEOUT", 0);
    publishError("OTA update timeout");
    otaInProgress = false;
  }
}
