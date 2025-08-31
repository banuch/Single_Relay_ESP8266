void setup_wifi_manager() {
  // Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println("Connect to WiFi: " + String(myWiFiManager->getConfigPortalSSID()));
    Serial.println("Open: http://192.168.4.1");
    
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
  
  Serial.println("Starting WiFiManager...");
  Serial.printf("AP Name: %s\n", ap_name.c_str());
  Serial.printf("AP Password: %s\n", ap_pass.c_str());
  
  // Try to connect to WiFi, if it fails start configuration portal
  if (!wifiManager.autoConnect(ap_name.c_str(), ap_pass.c_str())) {
    Serial.println("Failed to connect and hit timeout");
    digitalWrite(LED_PIN, LOW); // Turn on LED to show error
    delay(3000);
    ESP.restart();
  }
  
  // If we get here, WiFi is connected
  digitalWrite(LED_PIN, HIGH); // Turn off LED when connected
  Serial.println("WiFi connected successfully!");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal Strength (RSSI): ");
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
