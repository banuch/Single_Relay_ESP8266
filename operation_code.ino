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
      Serial.println("WiFi reset in progress...");
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

void checkEmergencyReset() {
  static unsigned long buttonPressStart = 0;
  static bool buttonPressed = false;
  
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressStart = millis();
    } else if (millis() - buttonPressStart > 5000) {
      Serial.println("Emergency reset triggered!");
      publishStatus("EMERGENCY_RESET");
      delay(500);
      ESP.restart();
    }
  } else {
    buttonPressed = false;
  }
}


void checkButton() {
  static unsigned long lastButtonPress = 0;
  static bool lastButtonState = HIGH;
  
  bool buttonState = digitalRead(BUTTON_PIN);
  
  if (buttonState != lastButtonState && buttonState == LOW) {
    if (millis() - lastButtonPress > 300) {  // Debounce
      Serial.println("Manual button pressed");
      
      // Don't allow manual control during OTA
      if (otaInProgress) {
        Serial.println("Manual control disabled during OTA");
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
    Serial.println("Restart command received");
    publishStatus("RESTARTING");
    delay(1000);
    ESP.restart();
  } else if (message == "reset_wifi") {
    Serial.println("WiFi reset command received");
    resetSettings();
  } else {
    Serial.println("Unknown command: " + message);
    publishError("Unknown command: " + message);
  }
}




void turnRelayOn() {
  if (otaInProgress) {
    Serial.println("Relay control disabled during OTA");
    publishError("Relay control disabled during OTA");
    return;
  }
  
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, LOW);  // Turn LED on (inverted logic)
  relayState = true;
  Serial.println("Relay turned ON");
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
  Serial.println("Relay turned OFF");
  publishStatus();
}

void toggleRelay() {
  if (relayState) {
    turnRelayOff();
  } else {
    turnRelayOn();
  }
  Serial.println("Relay toggled");
}


void resetSettings() {
  Serial.println("Resetting WiFi settings...");
  
  // Clear EEPROM config
  for (int i = 0; i < 100; ++i) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  
  // Reset WiFiManager settings
  wifiManager.resetSettings();
  
  Serial.println("Settings reset! Restarting...");
  publishStatus("WIFI_RESET");
  delay(1000);
  ESP.restart();
}












