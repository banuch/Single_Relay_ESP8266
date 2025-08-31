void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void loadConfig() {
  Serial.println("Loading configuration...");
  
  // Check if config exists
  if (EEPROM.read(0) == 'C' && EEPROM.read(1) == 'F' && EEPROM.read(2) == 'G') {
    Serial.println("Configuration found, loading...");
    
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
    
    Serial.printf("MQTT Server: %s\n", custom_mqtt_server);
    Serial.printf("MQTT Port: %s\n", custom_mqtt_port);
    Serial.printf("Device Name: %s\n", custom_device_name);
  } else {
    Serial.println("No configuration found, using defaults");
  }
}

void saveConfig() {
  Serial.println("Saving configuration...");
  
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
  Serial.println("Configuration saved!");
}
