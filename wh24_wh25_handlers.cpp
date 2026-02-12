// ============================================================================
// WH24 & WH25 PROTOCOL HANDLERS FOR LACROSSE2MQTT
// ============================================================================
// 
// INSERT THIS CODE IN lacrosse2mqtt.ino:
//   Location: receive() function
//   Position: After W136 handler, before "Unknown frame" handler
//
// Both protocols use mqtt_helper.h for consistent MQTT topic generation
// ============================================================================

        // ========== WH24 PROTOCOL (Outdoor Weather Station) ==========
        if (!frame_valid && config.proto_wh24 && payLoadSize == 17) {
            WH24::Frame wh24_frame;
            wh24_frame.rssi = rssi;
            wh24_frame.rate = rate;
            
            if (WH24::TryHandleData(payload, payLoadSize, &wh24_frame)) {
                WH24::DisplayFrame(payload, payLoadSize, &wh24_frame);
                
                byte ID = wh24_frame.ID;
                int cacheIndex = ID;
                
                if (cacheIndex >= 0 && cacheIndex < SENSOR_NUM) {
                    fcache[cacheIndex].ID = ID;
                    fcache[cacheIndex].temp = wh24_frame.temp;
                    fcache[cacheIndex].humi = wh24_frame.humi;
                    fcache[cacheIndex].pressure = wh24_frame.pressure;
                    fcache[cacheIndex].wind_speed = wh24_frame.wind_speed;
                    fcache[cacheIndex].wind_gust = wh24_frame.wind_gust;
                    fcache[cacheIndex].wind_direction = wh24_frame.wind_bearing;
                    fcache[cacheIndex].rain_total = wh24_frame.rain;
                    fcache[cacheIndex].rssi = rssi;
                    fcache[cacheIndex].rate = rate;
                    fcache[cacheIndex].batlo = wh24_frame.batlo;
                    fcache[cacheIndex].timestamp = millis();
                    strncpy(fcache[cacheIndex].sensorType, "WH24", 15);
                    fcache[cacheIndex].sensorType[15] = '\0';
                }
                
                // MQTT Publishing using mqtt_helper.h
                mqtt_client.publish(getMqttTopic(ID, "temp").c_str(), String(wh24_frame.temp, 1).c_str());
                mqtt_client.publish(getMqttTopic(ID, "humi").c_str(), String(wh24_frame.humi, DEC).c_str());
                mqtt_client.publish(getMqttTopic(ID, "pressure").c_str(), String(wh24_frame.pressure, 1).c_str());
                mqtt_client.publish(getMqttTopic(ID, "wind_speed").c_str(), String(wh24_frame.wind_speed, 2).c_str());
                mqtt_client.publish(getMqttTopic(ID, "wind_gust").c_str(), String(wh24_frame.wind_gust, 2).c_str());
                mqtt_client.publish(getMqttTopic(ID, "wind_bearing").c_str(), String(wh24_frame.wind_bearing, 0).c_str());
                mqtt_client.publish(getMqttTopic(ID, "wind_direction").c_str(), GetWindDirectionText(wh24_frame.wind_bearing));
                mqtt_client.publish(getMqttTopic(ID, "rain").c_str(), String(wh24_frame.rain, 1).c_str());
                mqtt_client.publish(getMqttTopic(ID, "uv_index").c_str(), String(wh24_frame.uv_index, DEC).c_str());
                
                String state = "{\"RSSI\": " + String(rssi) + 
                              ", \"batlo\": " + String(wh24_frame.batlo ? "true" : "false") + 
                              ", \"type\": \"WH24\"}";
                mqtt_client.publish(getMqttTopic(ID, "state").c_str(), state.c_str());
                
                int batteryPercent = wh24_frame.batlo ? 10 : 100;
                mqtt_client.publish(getMqttTopic(ID, "battery").c_str(), String(batteryPercent).c_str());
                
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    pub_hass_config(1, ID, 1);           // Temperature
                    pub_hass_config(0, ID, 1);           // Humidity
                    pub_hass_weather_config(0, ID);      // Wind Speed
                    pub_hass_weather_config(1, ID);      // Wind Direction
                    pub_hass_weather_config(2, ID);      // Wind Gust
                    pub_hass_weather_config(3, ID);      // Rain
                    pub_hass_weather_config(5, ID);      // Wind Bearing
                    pub_hass_battery_config(ID);
                }
                
                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] WH24 ID=%d Name=%s\n", ID, getSensorDisplayName(ID).c_str());
                    Serial.printf("[MQTT] UV Index: %d, Pressure: %.1f hPa\n", wh24_frame.uv_index, wh24_frame.pressure);
                }
            }
        }
        
        // ========== WH25 PROTOCOL (Indoor Sensor) ==========
        if (!frame_valid && config.proto_wh25 && payLoadSize == 10) {
            WH25::Frame wh25_frame;
            wh25_frame.rssi = rssi;
            wh25_frame.rate = rate;
            
            if (WH25::TryHandleData(payload, payLoadSize, &wh25_frame)) {
                WH25::DisplayFrame(payload, payLoadSize, &wh25_frame);
                
                byte ID = wh25_frame.ID;
                int cacheIndex = ID;
                
                if (cacheIndex >= 0 && cacheIndex < SENSOR_NUM) {
                    fcache[cacheIndex].ID = ID;
                    fcache[cacheIndex].temp = wh25_frame.temp;
                    fcache[cacheIndex].humi = wh25_frame.humi;
                    fcache[cacheIndex].pressure = wh25_frame.pressure;
                    fcache[cacheIndex].rssi = rssi;
                    fcache[cacheIndex].rate = rate;
                    fcache[cacheIndex].batlo = wh25_frame.batlo;
                    fcache[cacheIndex].timestamp = millis();
                    strncpy(fcache[cacheIndex].sensorType, "WH25", 15);
                    fcache[cacheIndex].sensorType[15] = '\0';
                }
                
                // MQTT Publishing using mqtt_helper.h
                mqtt_client.publish(getMqttTopic(ID, "temp").c_str(), String(wh25_frame.temp, 1).c_str());
                mqtt_client.publish(getMqttTopic(ID, "humi").c_str(), String(wh25_frame.humi, DEC).c_str());
                mqtt_client.publish(getMqttTopic(ID, "pressure").c_str(), String(wh25_frame.pressure, 1).c_str());
                
                String state = "{\"RSSI\": " + String(rssi) + 
                              ", \"batlo\": " + String(wh25_frame.batlo ? "true" : "false") + 
                              ", \"type\": \"WH25\"}";
                mqtt_client.publish(getMqttTopic(ID, "state").c_str(), state.c_str());
                
                int batteryPercent = wh25_frame.batlo ? 10 : 100;
                mqtt_client.publish(getMqttTopic(ID, "battery").c_str(), String(batteryPercent).c_str());
                
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    pub_hass_config(1, ID, 1);     // Temperature
                    pub_hass_config(0, ID, 1);     // Humidity
                    pub_hass_battery_config(ID);
                }
                
                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] WH25 ID=%d Name=%s\n", ID, getSensorDisplayName(ID).c_str());
                    Serial.printf("[MQTT] Pressure: %.1f hPa\n", wh25_frame.pressure);
                }
            }
        }

// ============================================================================
// END OF WH24 & WH25 HANDLERS
// ============================================================================