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
#include "SX127x.h"
#include <WiFiManager.h>

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
        
        // Device ID OHNE Kanal-Suffix - beide Kanäle gehören zum selben Gerät
        deviceId = mqtt_id + "_" + sensorIdentifier;
        
        // Unique ID und Topics MIT Kanal-Unterscheidung
        if (channel == 2 && what == 2) {
            uniqueId = deviceId + "_temp_ch2";
            configTopic = hass_base + deviceId + "/temp_ch2/config";
            stateTopic = pretty_base + sensorIdentifier + "_Ch2/temp";
        } else {
            uniqueId = deviceId + "_" + value[what];
            configTopic = hass_base + deviceId + "/" + value[what] + "/config";
            stateTopic = pretty_base + sensorIdentifier + "/" + value[what];
        }
        
    } else {
        // Device ID OHNE Kanal-Suffix
        deviceId = mqtt_id + "_" + String(ID);
        
        if (channel == 2 && what == 2) {
            uniqueId = deviceId + "_temp_ch2";
            configTopic = hass_base + deviceId + "/temp_ch2/config";
            stateTopic = pub_base + String(ID) + "/ch2/temp";
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

void expire_cache()
{
    unsigned long now = millis();
    for (int i = 0; i < SENSOR_NUM; i++) {
        // Kanal 1 Daten
        if (fcache[i].timestamp > 0 && (now - fcache[i].timestamp) > 300000) {
            fcache[i].timestamp = 0;
            fcache[i].temp = 0;
            fcache[i].humi = 0;
            fcache[i].valid = false;
            fcache[i].batlo = false;
            fcache[i].init = false;
        }
        
        // Kanal 2 Daten
        if (fcache[i].timestamp_ch2 > 0 && (now - fcache[i].timestamp_ch2) > 300000) {
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

    LaCrosse::Frame frame;
    frame.rate = rate;
    frame.rssi = rssi;
    
    bool frame_valid = LaCrosse::TryHandleData(payload, &frame);
    add_debug_log(payload, rssi, rate, frame_valid);
    
    if (frame_valid) {
        byte ID = frame.ID;
        byte channel = frame.channel;
        
        const char* sensorType = LaCrosse::GetSensorType(&frame);
        
        // Beide Kanäle verwenden denselben Cache-Index
        int cacheIndex = ID;

        if (cacheIndex >= SENSOR_NUM) {
            digitalWrite(LED_BUILTIN, LOW);
            return;
        }

        // Alte Frame-Daten für Vergleich
        LaCrosse::Frame oldframe;
        if (fcache[cacheIndex].timestamp > 0) {
            LaCrosse::TryHandleData(fcache[cacheIndex].data, &oldframe);
        } else {
            oldframe.valid = false;
        }

        // Gemeinsame Daten (immer aktualisieren)
        fcache[cacheIndex].ID = frame.ID;
        fcache[cacheIndex].rate = frame.rate;
        fcache[cacheIndex].rssi = rssi;
        fcache[cacheIndex].valid = frame.valid;
        fcache[cacheIndex].batlo = frame.batlo;  // Gilt für beide Kanäle!
        fcache[cacheIndex].init = frame.init;    // Gilt für beide Kanäle!
        memcpy(&fcache[cacheIndex].data, payload, FRAME_LENGTH);
        strncpy(fcache[cacheIndex].sensorType, sensorType, 15);
        fcache[cacheIndex].sensorType[15] = '\0';

        // Kanal-spezifische Daten
        if (channel == 2) {
            // Nur Temperatur für Kanal 2
            fcache[cacheIndex].temp_ch2 = frame.temp;
            fcache[cacheIndex].timestamp_ch2 = millis();
        } else {
            // Kanal 1: Temperatur und Humidity
            fcache[cacheIndex].temp = frame.temp;
            fcache[cacheIndex].humi = frame.humi;
            fcache[cacheIndex].timestamp = millis();
            fcache[cacheIndex].channel = frame.channel;
        }
        
        LaCrosse::DisplayFrame(payload, &frame);
        
        // MQTT Topics basierend auf Konfiguration
        String mqttBaseTopic;
        String sensorIdentifier;
        String batteryTopic;

        if (config.mqtt_use_names && id2name[ID].length() > 0) {
            // Verwendung von Namen
            sensorIdentifier = id2name[ID];
            
            if (channel == 2) {
                // Kanal 2: Separates Topic mit _Ch2 Suffix
                mqttBaseTopic = pretty_base + sensorIdentifier + "_Ch2/";
                // Battery Topic bleibt beim Hauptgerät (ohne _Ch2)
                batteryTopic = pretty_base + sensorIdentifier + "/battery";
            } else {
                // Kanal 1: Standard Topic
                mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                batteryTopic = mqttBaseTopic + "battery";
            }
        } else {
            // Verwendung von IDs
            sensorIdentifier = String(ID, DEC);
            
            if (channel == 2) {
                mqttBaseTopic = pub_base + sensorIdentifier + "/ch2/";
                // Battery Topic beim Hauptgerät
                batteryTopic = pub_base + sensorIdentifier + "/battery";
            } else {
                mqttBaseTopic = pub_base + sensorIdentifier + "/";
                batteryTopic = mqttBaseTopic + "battery";
            }
        }
        
        // Temperatur publishen (für beide Kanäle)
        mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(frame.temp, 1).c_str());
        
        // Humidity nur bei Kanal 1 und wenn vorhanden
        if (channel == 1 && frame.humi > 0 && frame.humi <= 100) {
            mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(frame.humi, DEC).c_str());
        }
        
        // State Information
        String state = "{"
            "\"low_batt\": " + String(frame.batlo ? "true" : "false") +
            ", \"init\": " + String(frame.init ? "true" : "false") +
            ", \"RSSI\": " + String(rssi, DEC) +
            ", \"baud\": " + String(frame.rate / 1000.0, 3) +
            ", \"channel\": " + String(frame.channel) +
            ", \"type\": \"" + String(sensorType) + "\"" +
            "}";
        mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
        
        // Battery Status nur bei Kanal 1 publishen (gilt für beide Kanäle!)
        if (channel == 1) {
            int batteryPercent = frame.batlo ? 10 : 100;
            mqtt_client.publish(batteryTopic.c_str(), String(batteryPercent).c_str());
        }
        
        // Home Assistant Discovery nur wenn Name gesetzt ist
        if (config.ha_discovery && id2name[ID].length() > 0) {
            // Alte Frame-Daten für Validierung
            if (oldframe.valid && abs(oldframe.temp - frame.temp) > 2.0) {
                // Skip invalid temperature jump
            } else {
                // Temperatur Config
                int haConfigIndex = (channel == 2) ? 2 : 1;  // 1=temp, 2=temp_ch2
                pub_hass_config(haConfigIndex, ID, channel);
            }
            
            // Humidity Config nur bei Kanal 1
            if (channel == 1 && frame.humi > 0 && frame.humi <= 100) {
                if (oldframe.valid && abs(oldframe.humi - frame.humi) > 10) {
                    // Skip invalid humidity jump
                } else {
                    pub_hass_config(0, ID, channel);  // 0=humidity
                }
            }
            
            // Battery Config nur bei Kanal 1
            if (channel == 1) {
                pub_hass_battery_config(ID);
            }
        }

    } else {
        static unsigned long last;
        LaCrosse::DisplayRaw(last, "Unknown", payload, payLoadSize, rssi, rate);
    }

    if (!showing_starfield) {
        update_display(&frame);
    }
    
    SX.EnableReceiver(true);
    digitalWrite(LED_BUILTIN, LOW);
}

void setup(void)
{
    char tmp[32];
    snprintf(tmp, 31, "lacrosse2mqtt_%06lX", (long)(ESP.getEfuseMac() >> 24));
    mqtt_id = String(tmp);
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
    SX.SetActiveDataRates(use_17241, use_9579, use_8842);

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