/*
 * lacrosse2mqtt
 * Bridge LaCrosse IT+ sensors to MQTT
 * (C) 2021 Stefan Seyfried
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, got to [https://www.gnu.org/licenses/](https://www.gnu.org/licenses/)
 */

#include <LittleFS.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "webfrontend.h"
#include "globals.h"
#include "lacrosse.h"
#include "wh1080.h"
#include "ws1600.h"
#include "wt440xh.h"
#include "SX127x.h"
#include "tx22it.h"
#include "emt7110.h"
#include "w136.h"
#include "wh24.h"
#include "wh25.h"
#include <WiFiManager.h>
#include <time.h>

// NTP Konfiguration
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "ptbtime1.ptb.de"
#define GMT_OFFSET_SEC 3600        // GMT+1 für Deutschland (Winterzeit)
#define DAYLIGHT_OFFSET_SEC 3600   // +1 Stunde für Sommerzeit

bool ntp_synced = false;
unsigned long last_ntp_check = 0;

#define FORMAT_LITTLEFS_IF_FAILED false
#define DISPLAY_TIMEOUT 300

const int interval = 20;
const int freq = 868290;

unsigned long last_reconnect;
unsigned long last_switch = 0;
bool littlefs_ok;
bool mqtt_ok;
uint32_t auto_display_on = 0;

static wl_status_t last_wifi_status = WL_IDLE_STATUS;

unsigned long loop_count = 0;
unsigned long last_cpu_check = 0;
float cpu_usage = 0.0;

Config config;
Cache fcache[SENSOR_NUM];
String id2name[SENSOR_NUM];
uint8_t hass_cfg[SENSOR_NUM];

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

#define STAR_COUNT 50
struct Star {
    float x, y, z;
};
Star stars[STAR_COUNT];

SX127x SX(LORA_CS, LORA_RST);

int get_current_datarate() {
    return SX.GetDataRate();
}

int get_interval() {
    return interval;
}

String wifi_disp;
bool showing_starfield = false;

unsigned long last_display_update = 0;
#define DISPLAY_UPDATE_INTERVAL 30

void display_set_on() {
    display.ssd1306_command(SSD1306_DISPLAYON);
}

void display_set_off() {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
}

void reset_display_timeout() {
    auto_display_on = uptime_sec();
}

void init_starfield() {
    for (int i = 0; i < STAR_COUNT; i++) {
        stars[i].x = random(-64, 64);
        stars[i].y = random(-32, 32);
        stars[i].z = random(1, 64);
    }
}

void draw_starfield() {
    display.clearDisplay();
    
    for (int i = 0; i < STAR_COUNT; i++) {
        stars[i].z -= 1.0;
        
        if (stars[i].z <= 0) {
            stars[i].x = random(-64, 64);
            stars[i].y = random(-32, 32);
            stars[i].z = 64;
        }
        
        int sx = (int)(64 + (stars[i].x / stars[i].z) * 64);
        int sy = (int)(32 + (stars[i].y / stars[i].z) * 32);
        
        if (sx >= 0 && sx < 128 && sy >= 0 && sy < 64) {
            int size = (int)(3 - stars[i].z / 21);
            if (size < 1) size = 1;
            for (int dx = 0; dx < size; dx++) {
                for (int dy = 0; dy < size; dy++) {
                    if (sx + dx < 128 && sy + dy < 64) {
                        display.drawPixel(sx + dx, sy + dy, SSD1306_WHITE);
                    }
                }
            }
        }
    }    
    display.display();
}

WiFiClient client;
PubSubClient mqtt_client(client);
String mqtt_id;
const String pretty_base = "lacrosse/climate/";
const String pub_base = "lacrosse/id/";
const String hass_base = "homeassistant/sensor/";
bool mqtt_server_set = false;

void setup_mqtt_with_will()
{
    String statusTopic = pub_base + "status";
    
    if (mqtt_server_set) {
        const char *user = NULL;
        const char *pass = NULL;
        if (config.mqtt_user.length()) {
            user = config.mqtt_user.c_str();
            pass = config.mqtt_pass.c_str();
        }
        
        if (mqtt_client.connect(mqtt_id.c_str(), user, pass, 
                                statusTopic.c_str(), 0, true, "offline")) {
            Serial.println("MQTT Connected with LWT");
            mqtt_client.publish(statusTopic.c_str(), "online", true);
            
            for (int i = 0; i < SENSOR_NUM; i++)
                hass_cfg[i] = 0;
        }
    }
}

void setup_ntp() {
    Serial.println("Configuring NTP time synchronization...");
    
    // Konfiguriere NTP mit zwei Servern für Redundanz
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
    
    // Warte bis Zeit synchronisiert ist (max 10 Sekunden)
    int retry = 0;
    const int max_retry = 20;
    
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo) && retry < max_retry) {
        Serial.print(".");
        delay(500);
        retry++;
    }
    
    if (retry < max_retry) {
        ntp_synced = true;
        Serial.println("\nNTP time synchronized successfully!");
        Serial.print("Current time: ");
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
        
        // Zeige auch Unix-Timestamp
        time_t now = time(nullptr);
        Serial.printf("Unix timestamp: %ld\n", now);
    } else {
        Serial.println("\nWARNING: NTP synchronization failed!");
        Serial.println("Certificate validation may not work correctly.");
    }
}

String get_current_time_string() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "Time not synced";
    }
    
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

void check_ntp_sync() {
    unsigned long now = millis();
    
    // Prüfe NTP-Status alle 60 Sekunden
    if (now - last_ntp_check > 60000) {
        last_ntp_check = now;
        
        struct tm timeinfo;
        bool currently_synced = getLocalTime(&timeinfo);
        
        if (currently_synced != ntp_synced) {
            ntp_synced = currently_synced;
            if (ntp_synced) {
                Serial.println("NTP sync restored!");
            } else {
                Serial.println("WARNING: NTP sync lost!");
            }
        }
        
        // Re-sync alle 24 Stunden
        static unsigned long last_full_sync = 0;
        if (now - last_full_sync > 86400000) {
            Serial.println("Performing daily NTP re-sync...");
            configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);
            last_full_sync = now;
        }
    }
}

void check_repeatedjobs()
{
    unsigned long now = millis();
    if (now - last_switch > interval * 1000) {
        SX.NextDataRate();
        last_switch = now;
    }
    if (config.changed) {
        Serial.println("MQTT config changed. Dis- and reconnecting...");
        config.changed = false;
        if (mqtt_ok) {
            String statusTopic = pub_base + "status";
            mqtt_client.publish(statusTopic.c_str(), "offline", true);
            mqtt_client.disconnect();
        }
        if (config.mqtt_server.length() > 0) {
            const char *_server = config.mqtt_server.c_str();
            mqtt_client.setServer(_server, config.mqtt_port);
            mqtt_server_set = true;
        } else
            Serial.println("MQTT server name not configured");
        mqtt_client.setKeepAlive(60);
        Serial.print("MQTT SERVER: "); Serial.println(config.mqtt_server);
        Serial.print("MQTT PORT:   "); Serial.println(config.mqtt_port);
        
        for (int i = 0; i < SENSOR_NUM; i++)
            hass_cfg[i] = 0;
        
        last_reconnect = 0;
    }
    if (!mqtt_client.connected() && now - last_reconnect > 5 * 1000) {
        if (mqtt_server_set) {
            const char *user = NULL;
            const char *pass = NULL;
            if (config.mqtt_user.length()) {
                user = config.mqtt_user.c_str();
                pass = config.mqtt_pass.c_str();
            }
            Serial.print("MQTT RECONNECT...");
            
            String statusTopic = pub_base + "status";
            
            if (mqtt_client.connect(mqtt_id.c_str(), user, pass, 
                                   statusTopic.c_str(), 0, true, "offline")) {
                Serial.println("OK!");
                
                mqtt_client.publish(statusTopic.c_str(), "online", true);
                Serial.println("Published status: online");
                
                for (int i = 0; i < SENSOR_NUM; i++)
                    hass_cfg[i] = 0;
            } else
                Serial.println("FAILED");
        }
        last_reconnect = now;
    }
    mqtt_ok = mqtt_client.connected();
}

const char* GetWindDirectionText(float degrees)
{
    const char* directions[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    
    int index = (int)((degrees + 11.25) / 22.5) % 16;
    return directions[index];
}

void pub_hass_weather_config(int what, byte ID)
{
    static const String name_suffix[6] = { 
        " Wind Speed", " Wind Direction", " Wind Gust", 
        " Rain", " Rain Total", " Wind Direction Degrees" 
    };
    static const String value[6] = { 
        "wind_speed", "wind_direction", "wind_gust", 
        "rain", "rain_total", "wind_bearing" 
    };
    static const String dclass[6] = { 
        "wind_speed", "", "wind_speed", 
        "precipitation", "precipitation", "" 
    };
    static const String unit[6] = { 
        "m/s", "", "m/s", 
        "mm", "mm", "°" 
    };
    static const String icon[6] = {
        "mdi:weather-windy", "mdi:compass", "mdi:weather-windy-variant",
        "mdi:weather-rainy", "mdi:weather-pouring", "mdi:compass-rose"
    };
    
    if (!config.ha_discovery)
        return;
    
    byte configMask = (1 << what);
    if (hass_cfg[ID] & configMask)
        return;
    hass_cfg[ID] |= configMask;
    
    String sensorName = id2name[ID].length() > 0 ? id2name[ID] : ("Weather_" + String(ID));
    
    String deviceId;
    String uniqueId;
    String configTopic;
    String stateTopic;
    
    if (config.mqtt_use_names && id2name[ID].length() > 0) {
        String sensorIdentifier = id2name[ID];
        deviceId = mqtt_id + "_" + sensorIdentifier;
        uniqueId = deviceId + "_" + value[what];
        configTopic = hass_base + deviceId + "/" + value[what] + "/config";
        stateTopic = pretty_base + sensorIdentifier + "/" + value[what];
    } else {
        deviceId = mqtt_id + "_" + String(ID);
        uniqueId = deviceId + "_" + value[what];
        configTopic = hass_base + deviceId + "/" + value[what] + "/config";
        stateTopic = pub_base + String(ID) + "/" + value[what];
    }
    
    String msg = "{"
            "\"device\":{"
                "\"identifiers\":[\"" + deviceId + "\"],"
                "\"name\":\"" + sensorName + "\","
                "\"manufacturer\":\"Weather Station\","
                "\"sw_version\":\"" + String(LACROSSE2MQTT_VERSION) + "\","
                "\"model\":\"WH1080/WS1600/WT440XH\""
            "},"
            "\"origin\":{"
                "\"name\":\"LaCrosse2MQTT\","
                "\"url\":\"https://github.com/steigerbalett/lacrosse2mqtt\","
                "\"sw_version\":\"" + String(LACROSSE2MQTT_VERSION) + "\""
            "},"
            "\"availability\":{"
                "\"topic\":\"" + pub_base + "status\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\""
            "},"
            "\"state_class\":\"measurement\",";
    
    if (dclass[what].length() > 0) {
        msg += "\"device_class\":\"" + dclass[what] + "\",";
    }
    if (unit[what].length() > 0) {
        msg += "\"unit_of_measurement\":\"" + unit[what] + "\",";
    }
    msg += "\"icon\":\"" + icon[what] + "\","
            "\"unique_id\":\"" + uniqueId + "\","
            "\"state_topic\":\"" + stateTopic + "\","
            "\"name\":\"" + sensorName + name_suffix[what] + "\","
            "\"enabled_by_default\":true"
        "}";

    mqtt_client.beginPublish(configTopic.c_str(), msg.length(), true);
    mqtt_client.print(msg);
    mqtt_client.endPublish();
}

void pub_hass_config(int what, byte ID, byte channel)
{
    static const String name_suffix[3] = { " Humidity", " Temperature", " Temperature Ch2" };
    static const String value[3] = { "humi", "temp", "temp_ch2" };
    static const String dclass[3] = { "humidity", "temperature", "temperature" };
    static const String unit[3] = { "%", "°C", "°C" };
    
    if (!config.ha_discovery)
        return;
    
    byte configMask = (1 << what);
    if (hass_cfg[ID] & configMask)
        return;
    hass_cfg[ID] |= configMask;
    
    String sensorName = id2name[ID].length() > 0 ? id2name[ID] : ("LaCrosse_" + String(ID));
    
    // WICHTIG: deviceId bleibt GLEICH für beide Kanäle!
    String deviceId;
    String uniqueId;
    String configTopic;
    String stateTopic;
    
    if (config.mqtt_use_names && id2name[ID].length() > 0) {
        String sensorIdentifier = id2name[ID];
        deviceId = mqtt_id + "_" + sensorIdentifier;
        
        if (channel == 2 && what == 2) {
            uniqueId = deviceId + "_temp_ch2";
            configTopic = hass_base + deviceId + "/temp_ch2/config";
            stateTopic = pretty_base + sensorIdentifier + "/temp_ch2";
        } else {
            uniqueId = deviceId + "_" + value[what];
            configTopic = hass_base + deviceId + "/" + value[what] + "/config";
            stateTopic = pretty_base + sensorIdentifier + "/" + value[what];
        }
        
    } else {
        deviceId = mqtt_id + "_" + String(ID);
        
        if (channel == 2 && what == 2) {
            uniqueId = deviceId + "_temp_ch2";
            configTopic = hass_base + deviceId + "/temp_ch2/config";
            stateTopic = pub_base + String(ID) + "/temp_ch2";
        } else {
            uniqueId = deviceId + "_" + value[what];
            configTopic = hass_base + deviceId + "/" + value[what] + "/config";
            stateTopic = pub_base + String(ID) + "/" + value[what];
        }
    }
    
    String msg = "{"
            "\"device\":{"
                "\"identifiers\":[\"" + deviceId + "\"],"
                "\"name\":\"" + sensorName + "\","
                "\"manufacturer\":\"LaCrosse\","
                "\"sw_version\":\"" + String(LACROSSE2MQTT_VERSION) + "\","
                "\"model\":\"LaCrosse IT+\""
            "},"
            "\"origin\":{"
                "\"name\":\"LaCrosse2MQTT\","
                "\"url\":\"https://github.com/steigerbalett/lacrosse2mqtt\","
                "\"sw_version\":\"" + String(LACROSSE2MQTT_VERSION) + "\""
            "},"
            "\"availability\":{"
                "\"topic\":\"" + pub_base + "status\","
                "\"payload_available\":\"online\","
                "\"payload_not_available\":\"offline\""
            "},"
            "\"state_class\":\"measurement\","
            "\"device_class\":\"" + dclass[what]+ "\","
            "\"unit_of_measurement\":\"" + unit[what] + "\","
            "\"unique_id\":\"" + uniqueId + "\","
            "\"state_topic\":\"" + stateTopic + "\","
            "\"name\":\"" + sensorName + name_suffix[what] + "\","
            "\"enabled_by_default\":true"
        "}";

    mqtt_client.beginPublish(configTopic.c_str(), msg.length(), true);
    mqtt_client.print(msg);
    mqtt_client.endPublish();
}

void pub_hass_battery_config(byte ID)
{
    if (!config.ha_discovery)
        return;
    
    // Prüfe ob Battery Config bereits gesendet wurde
    static byte battery_cfg_sent[SENSOR_NUM] = {0};
    if (battery_cfg_sent[ID])
        return;
    battery_cfg_sent[ID] = 1;
    
    String sensorName = id2name[ID].length() > 0 ? id2name[ID] : ("LaCrosse_" + String(ID));
    
    String deviceId;
    String uniqueId;
    String configTopic;
    String stateTopic;
    
    if (config.mqtt_use_names && id2name[ID].length() > 0) {
        String sensorIdentifier = id2name[ID];
        deviceId = mqtt_id + "_" + sensorIdentifier;
        uniqueId = deviceId + "_battery";
        configTopic = hass_base + deviceId + "/battery/config";
        stateTopic = pretty_base + sensorIdentifier + "/battery";
        
    } else {
        deviceId = mqtt_id + "_" + String(ID);
        uniqueId = deviceId + "_battery";
        configTopic = hass_base + deviceId + "/battery/config";
        stateTopic = pub_base + String(ID) + "/battery";
    }
    
    String msg = "{"
            "\"device\":{"
                "\"identifiers\":[\"" + deviceId + "\"]"
            "},"
            "\"device_class\":\"battery\","
            "\"entity_category\":\"diagnostic\","
            "\"state_class\":\"measurement\","
            "\"unique_id\":\"" + uniqueId + "\","
            "\"state_topic\":\"" + stateTopic + "\","
            "\"name\":\"" + sensorName + " Battery\","
            "\"unit_of_measurement\":\"%\""
        "}";
    
    mqtt_client.beginPublish(configTopic.c_str(), msg.length(), true);
    mqtt_client.print(msg);
    mqtt_client.endPublish();
}

void expire_cache() {
    unsigned long now = millis();
    
    // Berechne dynamischen Timeout basierend auf aktivierten Protokollen
    int active_protocols = 0;
    if (config.proto_lacrosse) active_protocols++;
    if (config.proto_tx35it) active_protocols++;
    if (config.proto_tx38it) active_protocols++;
    if (config.proto_wh1080) active_protocols++;
    if (config.proto_ws1600) active_protocols++;
    if (config.proto_wt440xh) active_protocols++;
    if (config.proto_tx22it) active_protocols++;
    if (config.proto_emt7110) active_protocols++;
    if (config.proto_w136) active_protocols++;
    if (config.proto_wh24) active_protocols++;
    if (config.proto_wh25) active_protocols++;
    
    // Basis-Timeout: 5 Minuten + 1 Minute pro Protokoll
    // Mindestens 5 Minuten, maximal 15 Minuten
    unsigned long sensor_timeout = 300000 + (active_protocols * 60000);
    if (sensor_timeout > 900000) sensor_timeout = 900000; // Max 15 Minuten
    
    if (config.debug_mode && now % 60000 < 100) { // Log alle 60 Sekunden
        Serial.printf("Cache timeout: %lu ms (%d protocols active)\n", sensor_timeout, active_protocols);
    }
    
    for (int i = 0; i < SENSOR_NUM; i++) {
        // Kanal 1 Daten
        if (fcache[i].timestamp > 0 && now - fcache[i].timestamp > sensor_timeout) {
            if (config.debug_mode) {
                Serial.printf("Expiring sensor ID %d (age: %lu ms)\n", fcache[i].ID, now - fcache[i].timestamp);
            }
            
            fcache[i].timestamp = 0;
            fcache[i].temp = 0;
            fcache[i].humi = 0;
            fcache[i].valid = false;
            fcache[i].batlo = false;
            fcache[i].init = false;
            fcache[i].wind_speed = 0;
            fcache[i].wind_direction = -1;
            fcache[i].wind_gust = 0;
            fcache[i].rain = 0;
            fcache[i].rain_total = 0;
            fcache[i].power = 0;
            fcache[i].pressure = 0;
        }
        
        // Kanal 2 Daten
        if (fcache[i].timestamp_ch2 > 0 && now - fcache[i].timestamp_ch2 > sensor_timeout) {
            if (config.debug_mode) {
                Serial.printf("Expiring sensor ID %d CH2 (age: %lu ms)\n", fcache[i].ID, now - fcache[i].timestamp_ch2);
            }
            fcache[i].timestamp_ch2 = 0;
            fcache[i].temp_ch2 = 0;
        }
    }
}

void update_display(LaCrosse::Frame *frame)
{
    unsigned long now = millis();
    if (now - last_display_update < DISPLAY_UPDATE_INTERVAL) {
        return;
    }
    last_display_update = now;
    
    uint32_t uptime = uptime_sec();
    
    if (config.display_on) {
        display.ssd1306_command(SSD1306_DISPLAYON);
    } else {
        if (uptime < auto_display_on + DISPLAY_TIMEOUT) {
            display.ssd1306_command(SSD1306_DISPLAYON);
        } else {
            display.ssd1306_command(SSD1306_DISPLAYOFF);
            return;
        }
    }
    
    bool show_wifi = (uptime % 70) < 10;
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    if (show_wifi) {
        display.println("WiFi Status:");
        display.println("********************");        
        display.println("SSID: " + WiFi.SSID());
        display.println("IP: " + WiFi.localIP().toString());
        display.setCursor(0, 54);
        display.println("MQTT: " + String(mqtt_ok ? "OK" : "---"));
        display.display();
    } else {
        byte newestID = 0;
        unsigned long newestTime = 0;
        
        for (int id = 0; id < SENSOR_NUM; id++) {
            if (fcache[id].timestamp > 0 && fcache[id].valid && fcache[id].timestamp > newestTime) {
                newestTime = fcache[id].timestamp;
                newestID = id;
            }
        }
        
        if (newestTime > 0) {
            byte baseID = newestID;
            String displayName = id2name[baseID].length() > 0 ? id2name[baseID] : ("ID: " + String(baseID));
            
            if (fcache[newestID].channel == 2) {
                displayName += " Ch2";
            }
            
            display.println(displayName);
            display.println("----------------");
            
            char tempBuf[24];
            snprintf(tempBuf, sizeof(tempBuf), "Temp: %.1fC", fcache[newestID].temp);
            display.println(tempBuf);
            
            if (fcache[newestID].humi > 0 && fcache[newestID].humi <= 100) {
                snprintf(tempBuf, sizeof(tempBuf), "Humidity: %d%%", fcache[newestID].humi);
                display.println(tempBuf);
            }
            
            if (fcache[newestID].batlo) {
                display.println("BAT: WEAK!");
            }
            
            String rawHex = "RAW: ";
            for (int i = 0; i < FRAME_LENGTH; i++) {
                if (fcache[newestID].data[i] < 16) rawHex += "0";
                rawHex += String(fcache[newestID].data[i], HEX);
                if (i < FRAME_LENGTH - 1) rawHex += " ";
            }
            display.setCursor(0, 54);
            display.println(rawHex);
            display.display();
        } else {
            display.println("Warte auf Daten...");
            display.display();
        }
    }
}

void receive()
{
    byte *payload;
    byte payLoadSize;
    int rssi, rate;
    if (!SX.Receive(payLoadSize))
        return;

    digitalWrite(LED_BUILTIN, HIGH);
    rssi = SX.GetRSSI();
    rate = SX.GetDataRate();
    payload = SX.GetPayloadPointer();

    if (config.debug_mode) {
        Serial.print("\n[DEBUG] End receiving, HEX raw data: ");
        for (int i = 0; i < payLoadSize; i++) {
            if (payload[i] < 16) Serial.print("0");
            Serial.print(payload[i], HEX);
            Serial.print(" ");
        }
        Serial.print(" RSSI:");
        Serial.print(rssi);
        Serial.print(" Rate:");
        Serial.println(rate);
    }

    bool frame_valid = false;
    LaCrosse::Frame lacrosse_frame;

    // ========== VERSUCHE LACROSSE IT+ PROTOKOLL ==========
    lacrosse_frame.rate = rate;
    lacrosse_frame.rssi = rssi;
    
    frame_valid = LaCrosse::TryHandleData(payload, &lacrosse_frame);
    add_debug_log(payload, rssi, rate, frame_valid);
    
    if (frame_valid) {
        // ========== LACROSSE IT+ HANDLING ==========
        byte ID = lacrosse_frame.ID;
        byte channel = lacrosse_frame.channel;
        
        const char* sensorType = LaCrosse::GetSensorType(&lacrosse_frame);
        int cacheIndex = ID;

        if (cacheIndex >= SENSOR_NUM) {
            digitalWrite(LED_BUILTIN, LOW);
            return;
        }

        LaCrosse::Frame oldframe;
        if (fcache[cacheIndex].timestamp > 0) {
            LaCrosse::TryHandleData(fcache[cacheIndex].data, &oldframe);
        } else {
            oldframe.valid = false;
        }

        // Cache aktualisieren
        fcache[cacheIndex].ID = lacrosse_frame.ID;
        fcache[cacheIndex].rate = lacrosse_frame.rate;
        fcache[cacheIndex].rssi = rssi;
        fcache[cacheIndex].valid = lacrosse_frame.valid;
        fcache[cacheIndex].batlo = lacrosse_frame.batlo;
        fcache[cacheIndex].init = lacrosse_frame.init;
        memcpy(&fcache[cacheIndex].data, payload, FRAME_LENGTH);
        strncpy(fcache[cacheIndex].sensorType, sensorType, 15);
        fcache[cacheIndex].sensorType[15] = '\0';

        if (channel == 2) {
            fcache[cacheIndex].temp_ch2 = lacrosse_frame.temp;
            fcache[cacheIndex].timestamp_ch2 = millis();
        } else {
            fcache[cacheIndex].temp = lacrosse_frame.temp;
            fcache[cacheIndex].humi = lacrosse_frame.humi;
            fcache[cacheIndex].timestamp = millis();
            fcache[cacheIndex].channel = lacrosse_frame.channel;
        }
        
        LaCrosse::DisplayFrame(payload, &lacrosse_frame);
        
        // MQTT Publishing
        String mqttBaseTopic;
        String sensorIdentifier;
        String batteryTopic;
        String tempTopic;

        if (config.mqtt_use_names && id2name[ID].length() > 0) {
            sensorIdentifier = id2name[ID];
            mqttBaseTopic = pretty_base + sensorIdentifier + "/";
            batteryTopic = mqttBaseTopic + "battery";
            tempTopic = mqttBaseTopic + (channel == 2 ? "temp_ch2" : "temp");
        } else {
            sensorIdentifier = String(ID, DEC);
            mqttBaseTopic = pub_base + sensorIdentifier + "/";
            batteryTopic = mqttBaseTopic + "battery";
            tempTopic = mqttBaseTopic + (channel == 2 ? "temp_ch2" : "temp");
        }

        mqtt_client.publish(tempTopic.c_str(), String(lacrosse_frame.temp, 1).c_str());
        
        if (channel == 1 && lacrosse_frame.humi > 0 && lacrosse_frame.humi <= 100) {
            mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(lacrosse_frame.humi, DEC).c_str());
        }
        
        String stateTopic = mqttBaseTopic + (channel == 2 ? "state_ch2" : "state");
        String state = "{"
            "\"low_batt\": " + String(lacrosse_frame.batlo ? "true" : "false") +
            ", \"init\": " + String(lacrosse_frame.init ? "true" : "false") +
            ", \"RSSI\": " + String(rssi, DEC) +
            ", \"baud\": " + String(lacrosse_frame.rate / 1000.0, 3) +
            ", \"channel\": " + String(lacrosse_frame.channel) +
            ", \"type\": \"" + String(sensorType) + "\"" +
            "}";
        mqtt_client.publish(stateTopic.c_str(), state.c_str());
        
        if (channel == 1) {
            int batteryPercent = lacrosse_frame.batlo ? 10 : 100;
            mqtt_client.publish(batteryTopic.c_str(), String(batteryPercent).c_str());
        }
        
        // Home Assistant Discovery
        if (config.ha_discovery && id2name[ID].length() > 0) {
            if (!oldframe.valid || abs(oldframe.temp - lacrosse_frame.temp) <= 2.0) {
                pub_hass_config((channel == 2) ? 2 : 1, ID, channel);
            }
            if (channel == 1 && lacrosse_frame.humi > 0 && lacrosse_frame.humi <= 100) {
                if (!oldframe.valid || abs(oldframe.humi - lacrosse_frame.humi) <= 10) {
                    pub_hass_config(0, ID, channel);
                }
            }
            if (channel == 1) {
                pub_hass_battery_config(ID);
            }
        }
        
        if (config.debug_mode) {
            Serial.printf("[MQTT] LaCrosse ID=%d Ch=%d Name=%s\n", 
                          ID, channel, id2name[ID].length() > 0 ? id2name[ID].c_str() : "none");
            Serial.printf("[MQTT] Topics: %s\n", mqttBaseTopic.c_str());
        }

    } else {
        // ========== VERSUCHE WH1080 PROTOKOLL ==========
        if (config.proto_wh1080 && payLoadSize == 10) {
            WH1080::Frame wh_frame;
            wh_frame.rssi = rssi;
            wh_frame.rate = rate;
            
            if (WH1080::TryHandleData(payload, payLoadSize, &wh_frame)) {
                WH1080::DisplayFrame(payload, payLoadSize, &wh_frame);
                
                byte ID = wh_frame.ID;
                int cacheIndex = ID;
                if (cacheIndex >= 0 && cacheIndex < SENSOR_NUM) {
                    fcache[cacheIndex].ID = ID;
                    fcache[cacheIndex].temp = wh_frame.temp;
                    fcache[cacheIndex].humi = wh_frame.humi;
                    fcache[cacheIndex].wind_speed = wh_frame.wind_speed;
                    fcache[cacheIndex].wind_gust = wh_frame.wind_gust;
                    fcache[cacheIndex].rain_total = wh_frame.rain;
                    fcache[cacheIndex].rssi = rssi;
                    fcache[cacheIndex].rate = rate;
                    fcache[cacheIndex].timestamp = millis();
                    strncpy(fcache[cacheIndex].sensorType, "WH1080", 15);
                    fcache[cacheIndex].sensorType[15] = '\0';
            
                    // ← WICHTIG: Windrichtung NUR setzen wenn gültig!
                    if (wh_frame.wind_bearing >= 0 && wh_frame.wind_bearing <= 15) {
                        fcache[cacheIndex].wind_direction = (int)(wh_frame.wind_bearing * 22.5f);
                    } else {
                        fcache[cacheIndex].wind_direction = -1;
                    }
                }
                
                String mqttBaseTopic;
                String sensorIdentifier;
                
                if (config.mqtt_use_names && id2name[ID].length() > 0) {
                    sensorIdentifier = id2name[ID];
                    mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                } else {
                    sensorIdentifier = String(ID, DEC);
                    mqttBaseTopic = pub_base + sensorIdentifier + "/";
                }
                
                // Publish Weather Data
                mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(wh_frame.temp, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(wh_frame.humi, DEC).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_speed").c_str(), String(wh_frame.wind_speed, 2).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_gust").c_str(), String(wh_frame.wind_gust, 2).c_str());
                
                float wind_degrees = wh_frame.wind_bearing * 22.5f;
                mqtt_client.publish((mqttBaseTopic + "wind_bearing").c_str(), String(wind_degrees, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_direction").c_str(), WH1080::GetWindDirection(wh_frame.wind_bearing));
                mqtt_client.publish((mqttBaseTopic + "rain").c_str(), String(wh_frame.rain, 1).c_str());
                
                String state = "{\"RSSI\": " + String(rssi) + ", \"type\": \"WH1080\"}";
                mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
                
                // Home Assistant Discovery
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    pub_hass_config(1, ID, 1);  // Temperature
                    pub_hass_config(0, ID, 1);  // Humidity
                    pub_hass_weather_config(0, ID);  // Wind Speed
                    pub_hass_weather_config(1, ID);  // Wind Direction
                    pub_hass_weather_config(2, ID);  // Wind Gust
                    pub_hass_weather_config(3, ID);  // Rain
                    pub_hass_weather_config(4, ID);  // Wind Bearing
                }

                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] WH1080 ID=%d Name=%s\n", ID, sensorIdentifier.c_str());
                }
            }
        }
        
        // ========== VERSUCHE WS1600 PROTOKOLL ==========
        if (!frame_valid && config.proto_ws1600 && payLoadSize == 9) {
            WS1600::Frame ws_frame;
            ws_frame.rssi = rssi;
            ws_frame.rate = rate;
            
            if (WS1600::TryHandleData(payload, payLoadSize, &ws_frame)) {
                WS1600::DisplayFrame(payload, payLoadSize, &ws_frame);
                
                byte ID = ws_frame.ID;

            int cacheIndex = ID;
            if (cacheIndex >= 0 && cacheIndex < SENSOR_NUM) {
                fcache[cacheIndex].ID = ID;
                fcache[cacheIndex].channel = ws_frame.channel;
                fcache[cacheIndex].temp = ws_frame.temp;
                fcache[cacheIndex].humi = ws_frame.humi;
                fcache[cacheIndex].wind_speed = ws_frame.wind_speed;
                fcache[cacheIndex].rain_total = ws_frame.rain;
                fcache[cacheIndex].rssi = rssi;
                fcache[cacheIndex].rate = rate;
                fcache[cacheIndex].batlo = ws_frame.batlo;
                fcache[cacheIndex].timestamp = millis();
                strncpy(fcache[cacheIndex].sensorType, "WS1600", 15);
                fcache[cacheIndex].sensorType[15] = '\0';
        
                // Wind Direction: wsframe.winddirection ist 0-15
                if (ws_frame.wind_direction >= 0 && ws_frame.wind_direction <= 15) {
                    fcache[cacheIndex].wind_direction = (int)(ws_frame.wind_direction * 22.5f);
                } else {
                    fcache[cacheIndex].wind_direction = -1;
                }
            }
                String mqttBaseTopic;
                String sensorIdentifier;
                
                if (config.mqtt_use_names && id2name[ID].length() > 0) {
                    sensorIdentifier = id2name[ID];
                    mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                } else {
                    sensorIdentifier = String(ID, DEC);
                    mqttBaseTopic = pub_base + sensorIdentifier + "/";
                }
                
                // Publish Weather Data
                mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(ws_frame.temp, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(ws_frame.humi, DEC).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_speed").c_str(), String(ws_frame.wind_speed, 2).c_str());
                
                float wind_degrees = ws_frame.wind_direction * 22.5f;
                mqtt_client.publish((mqttBaseTopic + "wind_bearing").c_str(), String(wind_degrees, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_direction").c_str(), GetWindDirectionText(wind_degrees));
                mqtt_client.publish((mqttBaseTopic + "rain").c_str(), String(ws_frame.rain, 1).c_str());
                
                String state = "{\"RSSI\": " + String(rssi) + 
                              ", \"batlo\": " + String(ws_frame.batlo ? "true" : "false") + 
                              ", \"type\": \"WS1600\"}";
                mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
                
                // Battery
                int batteryPercent = ws_frame.batlo ? 10 : 100;
                mqtt_client.publish((mqttBaseTopic + "battery").c_str(), String(batteryPercent).c_str());
                
                // Home Assistant Discovery
                if (config.ha_discovery && id2name[ID].length() > 0) {

                    pub_hass_config(1, ID, 1);  // Temperature
                    pub_hass_config(0, ID, 1);  // Humidity
                    pub_hass_weather_config(0, ID);  // Wind Speed
                    pub_hass_weather_config(1, ID);  // Wind Direction
                    pub_hass_weather_config(2, ID);  // Wind Gust
                    pub_hass_weather_config(3, ID);  // Rain
                    pub_hass_weather_config(4, ID);
                    pub_hass_weather_config(5, ID);  // Wind Bearing
                    pub_hass_battery_config(ID);
                }
                
                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] WS1600 ID=%d Ch=%d Name=%s\n", ID, ws_frame.channel, sensorIdentifier.c_str());
                }
            }
        }
        
        // ========== VERSUCHE WT440XH PROTOKOLL ==========
        if (!frame_valid && config.proto_wt440xh && payLoadSize == 4) {
            WT440XH::Frame wt_frame;
            wt_frame.rssi = rssi;
            wt_frame.rate = rate;
            
            if (WT440XH::TryHandleData(payload, &wt_frame)) {
                WT440XH::DisplayFrame(payload, &wt_frame);
                
                byte ID = wt_frame.ID;
                byte channel = wt_frame.channel;
                String mqttBaseTopic;
                String sensorIdentifier;
                String tempTopic;
                
                if (config.mqtt_use_names && id2name[ID].length() > 0) {
                    sensorIdentifier = id2name[ID];
                    mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                    tempTopic = mqttBaseTopic + (channel == 2 ? "temp_ch2" : "temp");
                } else {
                    sensorIdentifier = String(ID, DEC);
                    mqttBaseTopic = pub_base + sensorIdentifier + "/";
                    tempTopic = mqttBaseTopic + (channel == 2 ? "temp_ch2" : "temp");
                }
                
                // Publish Sensor Data
                mqtt_client.publish(tempTopic.c_str(), String(wt_frame.temp, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(wt_frame.humi, DEC).c_str());
                
                String stateTopic = mqttBaseTopic + (channel == 2 ? "state_ch2" : "state");
                String state = "{\"RSSI\": " + String(rssi) + 
                              ", \"batlo\": " + String(wt_frame.batlo ? "true" : "false") +
                              ", \"channel\": " + String(channel) + 
                              ", \"type\": \"WT440XH\"}";
                mqtt_client.publish(stateTopic.c_str(), state.c_str());
                
                // Battery nur bei Kanal 1
                if (channel == 1) {
                    int batteryPercent = wt_frame.batlo ? 10 : 100;
                    mqtt_client.publish((mqttBaseTopic + "battery").c_str(), String(batteryPercent).c_str());
                }
                
                // Home Assistant Discovery
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    pub_hass_config((channel == 2) ? 2 : 1, ID, channel);
                    pub_hass_config(0, ID, channel);
                    if (channel == 1) {
                        pub_hass_battery_config(ID);
                    }
                }
                
                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] WT440XH ID=%d Ch=%d Name=%s\n", ID, channel, sensorIdentifier.c_str());
                }
            }
        }

        // ========== VERSUCHE TX22IT PROTOKOLL ==========
        if (!frame_valid && config.proto_tx22it && payLoadSize == 9) {
            TX22IT::Frame tx22_frame;
            tx22_frame.rssi = rssi;
            tx22_frame.rate = rate;
            
            if (TX22IT::TryHandleData(payload, payLoadSize, &tx22_frame)) {
                TX22IT::DisplayFrame(payload, payLoadSize, &tx22_frame);
                
                byte ID = tx22_frame.ID;

                int cacheIndex = ID;
    
            if (cacheIndex >= 0 && cacheIndex < SENSOR_NUM) {
                fcache[cacheIndex].ID = ID;
                fcache[cacheIndex].channel = 1;
                fcache[cacheIndex].temp = tx22_frame.temp;
                fcache[cacheIndex].humi = tx22_frame.humi;
                fcache[cacheIndex].wind_speed = tx22_frame.wind_speed;
                fcache[cacheIndex].wind_gust = tx22_frame.wind_gust;
                fcache[cacheIndex].rssi = rssi;
                fcache[cacheIndex].rate = rate;
                fcache[cacheIndex].batlo = tx22_frame.batlo;
                fcache[cacheIndex].timestamp = millis();
                strncpy(fcache[cacheIndex].sensorType, "TX22IT", 15);
                fcache[cacheIndex].sensorType[15] = '\0';
        
                // Wind Direction: tx22_frame.winddirection ist direkt 0-360°
                if (tx22_frame.wind_direction >= 0 && tx22_frame.wind_direction <= 360) {
                    fcache[cacheIndex].wind_direction = (int)tx22_frame.wind_direction;
                } else {
                    fcache[cacheIndex].wind_direction = -1;
                }
            }

                String mqttBaseTopic;
                String sensorIdentifier;
                
                if (config.mqtt_use_names && id2name[ID].length() > 0) {
                    sensorIdentifier = id2name[ID];
                    mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                } else {
                    sensorIdentifier = String(ID, DEC);
                    mqttBaseTopic = pub_base + sensorIdentifier + "/";
                }
                
                // Publish Weather Data
                mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(tx22_frame.temp, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(tx22_frame.humi, DEC).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_speed").c_str(), String(tx22_frame.wind_speed, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_gust").c_str(), String(tx22_frame.wind_gust, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_bearing").c_str(), String(tx22_frame.wind_direction, 0).c_str());
                mqtt_client.publish((mqttBaseTopic + "wind_direction").c_str(), GetWindDirectionText(tx22_frame.wind_direction));
                
                String state = "{\"RSSI\": " + String(rssi) + 
                              ", \"batlo\": " + String(tx22_frame.batlo ? "true" : "false") + 
                              ", \"type\": \"TX22IT\"}";
                mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
                
                // Battery
                int batteryPercent = tx22_frame.batlo ? 10 : 100;
                mqtt_client.publish((mqttBaseTopic + "battery").c_str(), String(batteryPercent).c_str());
                
                // Home Assistant Discovery
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    pub_hass_config(1, ID, 1);  // Temperature
                    pub_hass_config(0, ID, 1);  // Humidity
                    pub_hass_weather_config(0, ID);  // Wind Speed
                    pub_hass_weather_config(1, ID);  // Wind Direction
                    pub_hass_weather_config(2, ID);  // Wind Gust
                    pub_hass_weather_config(5, ID);  // Wind Bearing
                    pub_hass_battery_config(ID);
                }
                
                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] TX22IT ID=%d Name=%s\n", ID, sensorIdentifier.c_str());
                }
            }
        }
        
        // ========== VERSUCHE EMT7110 PROTOKOLL ==========
        if (!frame_valid && config.proto_emt7110 && payLoadSize == 9) {
            EMT7110::Frame emt_frame;
            emt_frame.rssi = rssi;
            emt_frame.rate = rate;
            
            if (EMT7110::TryHandleData(payload, payLoadSize, &emt_frame)) {
                EMT7110::DisplayFrame(payload, payLoadSize, &emt_frame);
                
                byte ID = emt_frame.ID;
                String mqttBaseTopic;
                String sensorIdentifier;
                
                if (config.mqtt_use_names && id2name[ID].length() > 0) {
                    sensorIdentifier = id2name[ID];
                    mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                } else {
                    sensorIdentifier = String(ID, DEC);
                    mqttBaseTopic = pub_base + sensorIdentifier + "/";
                }
                
                // Publish Energy Data
                mqtt_client.publish((mqttBaseTopic + "power").c_str(), String(emt_frame.power, 1).c_str());
                mqtt_client.publish((mqttBaseTopic + "energy").c_str(), String(emt_frame.energy, 3).c_str());
                
                String state = "{\"RSSI\": " + String(rssi) + 
                              ", \"batlo\": " + String(emt_frame.batlo ? "true" : "false") + 
                              ", \"type\": \"EMT7110\"}";
                mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
                
                // Battery
                int batteryPercent = emt_frame.batlo ? 10 : 100;
                mqtt_client.publish((mqttBaseTopic + "battery").c_str(), String(batteryPercent).c_str());
                
                // Home Assistant Discovery würde hier weitere Konfigurationen benötigen
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    pub_hass_battery_config(ID);
                }
                
                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] EMT7110 ID=%d Name=%s\n", ID, sensorIdentifier.c_str());
                }
            }
        }
        
        // ========== VERSUCHE W136 PROTOKOLL ==========
        if (!frame_valid && config.proto_w136 && payLoadSize == 6) {
            W136::Frame w136_frame;
            w136_frame.rssi = rssi;
            w136_frame.rate = rate;
            
            if (W136::TryHandleData(payload, payLoadSize, &w136_frame)) {
                W136::DisplayFrame(payload, payLoadSize, &w136_frame);
                
                byte ID = w136_frame.ID;
                String mqttBaseTopic;
                String sensorIdentifier;
                
                if (config.mqtt_use_names && id2name[ID].length() > 0) {
                    sensorIdentifier = id2name[ID];
                    mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                } else {
                    sensorIdentifier = String(ID, DEC);
                    mqttBaseTopic = pub_base + sensorIdentifier + "/";
                }
                
                // Publish Rain Data
                mqtt_client.publish((mqttBaseTopic + "rain").c_str(), String(w136_frame.rain, 1).c_str());
                
                String state = "{\"RSSI\": " + String(rssi) + 
                              ", \"batlo\": " + String(w136_frame.batlo ? "true" : "false") + 
                              ", \"type\": \"W136\"}";
                mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
                
                // Battery
                int batteryPercent = w136_frame.batlo ? 10 : 100;
                mqtt_client.publish((mqttBaseTopic + "battery").c_str(), String(batteryPercent).c_str());
                
                // Home Assistant Discovery
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    pub_hass_weather_config(3, ID);  // Rain
                    pub_hass_battery_config(ID);
                }
                
                frame_valid = true;
                
                if (config.debug_mode) {
                    Serial.printf("[MQTT] W136 ID=%d Name=%s\n", ID, sensorIdentifier.c_str());
                }
            }
        }
        
        // Falls kein Protokoll erkannt wurde
        if (!frame_valid) {
            static unsigned long last;
            LaCrosse::DisplayRaw(last, "Unknown", payload, payLoadSize, rssi, rate);
        }
    }

    if (!showing_starfield) {
        update_display(&lacrosse_frame);
    }
    
    SX.EnableReceiver(true);
    digitalWrite(LED_BUILTIN, LOW);
}

void setup(void)
{
    char tmp[32];
    snprintf(tmp, 31, "lacrosse2mqtt_%06lX", (long)(ESP.getEfuseMac() >> 24));
    mqtt_id = String(tmp);
    for (int i = 0; i < SENSOR_NUM; i++) {
        memset(&fcache[i], 0, sizeof(Cache));  // Alles auf 0
        fcache[i].wind_direction = -1;         // ← WICHTIG: Ungültig markieren
        fcache[i].ID = 0xFF;                   // Ungültige ID
    }
    config.mqtt_port = 1883;
    Serial.begin(115200);

    WiFiManager wifiManager;
    if (!wifiManager.autoConnect("Lacrosse2mqttAP")) {
      Serial.println("Failed to connect and hit timeout");
      ESP.restart();
      delay(1000);
    }

    Serial.println("Connected to WiFi!");
    Serial.print("Verbundenes WLAN: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());

    setup_ntp();

    littlefs_ok = LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED);
    if (!littlefs_ok)
        Serial.println("LittleFS Mount Failed");
    setup_web();

    if (config.debug_mode) {
        Serial.println("Debug Mode ENABLED");
    }

    pinMode(KEY_BUILTIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    Wire.begin(OLED_SDA, OLED_SCL);
    
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    delay(1000);

    Serial.println("TTGO LORA lacrosse2mqtt converter");
    Serial.println(mqtt_id);
    
    init_starfield();
    unsigned long boot_animation_start = millis();
    while (millis() - boot_animation_start < 3000) {
        draw_starfield();
        delay(100);
    }

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LaCrosse2mqtt");
    display.println("Starting...");
    display.display();
    delay(2000);

    last_switch = millis();

    if (!SX.init()) {
        Serial.println("** SX127x init failed! **");
        display.println("** SX127x init failed! **");
        display.display();
        while(true)
            delay(1000);
    }

    bool use_17241 = config.proto_lacrosse;
    bool use_9579 = config.proto_tx35it;
    bool use_8842 = config.proto_tx38it;
    bool use_6618 = config.proto_wh1080;
    bool use_4800 = config.proto_tx22it;
    SX.SetActiveDataRates(use_17241, use_9579, use_8842, use_6618, use_4800);

    SX.SetupForLaCrosse();
    SX.SetFrequency(freq);
    SX.NextDataRate(0);
    SX.EnableReceiver(true);
}

uint32_t check_button()
{
    static uint32_t low_at = 0;
    static bool pressed = false;
    static uint32_t last_check = 0;
    
    uint32_t now = millis();
    if (now - last_check < 50) {
        return 0;
    }
    last_check = now;
    
    if (digitalRead(KEY_BUILTIN) == LOW) {
        if (! pressed)
            low_at = now;
        pressed = true;
        return 0;
    }
    if (! pressed)
        return 0;
    pressed = false;
    return now - low_at;
}

void check_wifi_status()
{
    wl_status_t current_status = WiFi.status();
    if (current_status != last_wifi_status) {
        Serial.printf("WiFi status changed: %d -> %d\n", last_wifi_status, current_status);
        
        if (current_status == WL_CONNECTED) {
            Serial.println("WiFi: Connected");
            auto_display_on = uptime_sec();
        } else if (last_wifi_status == WL_CONNECTED) {
            Serial.println("WiFi: Disconnected");
            auto_display_on = uptime_sec();
        }
        
        last_wifi_status = current_status;
    }
}

void loop(void)
{
    static unsigned long last_starfield_update = 0;
    static uint32_t last_interaction = 0;  
    static bool interaction_initialized = false;
    static uint32_t last_wifi_display = 0;
    
    delay(10);
    
    handle_client();

    uint32_t button_time = check_button();
    if (button_time > 100 && button_time <= 2000) {
        if (!config.display_on) {
            auto_display_on = uptime_sec();
        }
        showing_starfield = false;
        last_interaction = uptime_sec();
        update_display(NULL);
    }

    receive();
    check_repeatedjobs();
    expire_cache();
    check_wifi_status();
    check_ntp_sync();
    
    unsigned long now = millis();
    uint32_t uptime = uptime_sec();
    
    if (!interaction_initialized) {
        last_interaction = uptime;
        last_wifi_display = uptime;
        interaction_initialized = true;
    }
    
    bool has_critical_error = false;
    bool has_recent_data = false;
    
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (fcache[i].timestamp > 0) {
            unsigned long age = now - fcache[i].timestamp;
            
            if (age < 300000) {
                has_recent_data = true;
                
                if (fcache[i].batlo) {
                    has_critical_error = true;
                    static bool battery_error_reported[SENSOR_NUM] = {false};
                    if (!battery_error_reported[i]) {
                        battery_error_reported[i] = true;
                        last_interaction = uptime;
                        showing_starfield = false;
                    }
                }
            }
        }
    }
    
    bool display_should_be_on = config.display_on || (uptime < auto_display_on + DISPLAY_TIMEOUT);
    
    if (display_should_be_on) {
        bool show_wifi;
        if (config.screensaver_mode && config.display_on) {
            uint32_t time_since_last_wifi = uptime - last_wifi_display;
            show_wifi = (time_since_last_wifi >= 300) && (time_since_last_wifi < 310);
        } else {
            show_wifi = (uptime % 70) < 10;
        }
        
        if (config.screensaver_mode && config.display_on) {
            uint32_t idle_time = uptime - last_interaction;
            
            bool should_show_screensaver = (idle_time > 300) && 
                                          !has_critical_error && 
                                          !show_wifi;
            
            if (should_show_screensaver) {
                if ((now - last_starfield_update > 100)) {
                    last_starfield_update = now;
                    if (!showing_starfield) {
                        showing_starfield = true;
                    }
                    draw_starfield();
                }
            } else {
                if (showing_starfield) {
                    showing_starfield = false;
                    update_display(NULL);
                } else if (show_wifi) {
                    update_display(NULL);
                    last_wifi_display = uptime;
                }
            }
        } else {
            if (!show_wifi) {
                if (!has_recent_data && (now - last_starfield_update > 100)) {
                    last_starfield_update = now;
                    if (!showing_starfield) {
                        showing_starfield = true;
                    }
                    draw_starfield();
                } else if (has_recent_data && showing_starfield) {
                    showing_starfield = false;
                    update_display(NULL);
                }
            } else {
                if (showing_starfield) {
                    showing_starfield = false;
                    update_display(NULL);
                }
            }
        }
    } else {
        showing_starfield = false;
    }
    
    loop_count++;
    
    if (millis() - last_cpu_check > 1000) {
        if (loop_count >= 80) {
            cpu_usage = 15.0;
        } else if (loop_count >= 60) {
            cpu_usage = 30.0;
        } else if (loop_count >= 40) {
            cpu_usage = 50.0;
        } else if (loop_count >= 20) {
            cpu_usage = 70.0;
        } else {
            cpu_usage = 90.0;
        }
        
        loop_count = 0;
        last_cpu_check = millis();
    }
}