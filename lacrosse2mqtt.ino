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
#include <WiFiManager.h>
#include "wifi_functions.h"
#include "webfrontend.h"
#include "globals.h"
#include "lacrosse.h"
#include "SX127x.h"
#include "lacrosse.h"
#include "wh1080.h"
#include "tx38it.h"
#include "tx35it.h"
#include "ws1600.h"
#include "wt440xh.h"

#define FORMAT_LITTLEFS_IF_FAILED false

/* if display is default to off, keep it on for this many seconds after power on
 * or a wifi change event */
#define DISPLAY_TIMEOUT 300

const int interval = 20;   /* toggle interval in seconds */
const int freq = 868290;   /* frequency in kHz, 868300 did not receive all sensors... */

unsigned long last_reconnect;
unsigned long last_switch = 0;
bool littlefs_ok;
bool mqtt_ok;
uint32_t auto_display_on = 0;


Config config;
Cache fcache[SENSOR_NUM]; /* 256 sensor slots */
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
bool showing_starfield = false;  // NEU: Global für receive() und loop()


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
        stars[i].z -= 0.5;
        
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


#define ESP_MANUFACTURER  "ESPRESSIF"
#define ESP_MODEL_NUMBER  "ESP32"
#define ESP_MODEL_NAME    "ESPRESSIF IOT"
#define ESP_DEVICE_NAME   "ESP STATION"


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
    
    // Set Last Will and Testament
    // Wenn die Verbindung abbricht, wird automatisch "offline" publiziert
    if (mqtt_server_set) {
        const char *user = NULL;
        const char *pass = NULL;
        if (config.mqtt_user.length()) {
            user = config.mqtt_user.c_str();
            pass = config.mqtt_pass.c_str();
        }
        
        // Connect mit Last Will
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
        SX.NextDataRate();  // Wechselt automatisch nur durch aktive Raten
        last_switch = now;
    }
    if (config.changed) {
        Serial.println("MQTT config changed. Dis- and reconnecting...");
        config.changed = false;

        bool use_17241 = config.proto_lacrosse;
        bool use_9579 = config.proto_tx35it;
        bool use_8842 = config.proto_tx38it;
        SX.SetActiveDataRates(use_17241, use_9579, use_8842);
        SX.NextDataRate(0);
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
        
        // NEU: Home Assistant Discovery Cache zurücksetzen bei Config-Änderung
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
    static const String value[3] = { "humi", "temp", "temp" };
    static const String dclass[3] = { "humidity", "temperature", "temperature" };
    static const String unit[3] = { "%", "°C", "°C" };
    
    if (!config.ha_discovery)
        return;
    
    byte configMask = (1 << what);
    if (hass_cfg[ID] & configMask)
        return;
    hass_cfg[ID] |= configMask;
    
    String sensorName = id2name[ID].length() > 0 ? id2name[ID] : ("LaCrosse_" + String(ID));
    String channelSuffix = (channel == 2) ? "_ch2" : "";
    
    // NEU: Device ID und Topics basierend auf mqtt_use_names
    String deviceId;
    String uniqueId;
    String configTopic;
    String stateTopic;
    
    if (config.mqtt_use_names && id2name[ID].length() > 0) {
        // Namen-Modus: Verwende Sensor-Namen für alles
        String sensorIdentifier = id2name[ID];
        if (channel == 2) {
            sensorIdentifier += "_Ch2";
        }
        
        // Device ID basiert auf Namen
        deviceId = mqtt_id + "_" + sensorIdentifier;
        uniqueId = deviceId + "_" + value[what];
        
        // Config Topic: homeassistant/sensor/lacrosse2mqtt_XXXXXX_Wohnzimmer/temp/config
        configTopic = hass_base + deviceId + "/" + value[what] + "/config";
        
        // State Topic: climate/Wohnzimmer/temp
        stateTopic = pretty_base + sensorIdentifier + "/" + value[what];
        
    } else {
        // ID-Modus: Verwende ID für alles (bisheriges Verhalten)
        deviceId = mqtt_id + "_" + String(ID);
        uniqueId = deviceId + channelSuffix + "_" + value[what];
        
        // Config Topic: homeassistant/sensor/lacrosse2mqtt_XXXXXX_30/temp/config (oder temp_ch2/config)
        configTopic = hass_base + deviceId + "/" + value[what] + channelSuffix + "/config";
        
        // State Topic: lacrosse/id/30/temp (oder lacrosse/id/30/ch2/temp)
        String idStr = String(ID);
        if (channel == 2)
            idStr += "/ch2";
        stateTopic = pub_base + idStr + "/" + value[what];
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
                "\"url\":\"https://github.com/seyd/lacrosse2mqtt\","
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

    Serial.println("HA Discovery: " + configTopic);
    Serial.println("State Topic: " + stateTopic);
    Serial.println("Msg length: " + String(msg.length()));
    
    mqtt_client.beginPublish(configTopic.c_str(), msg.length(), true);
    mqtt_client.print(msg);
    mqtt_client.endPublish();
}

void pub_hass_battery_config(byte ID)
{
    if (!config.ha_discovery)
        return;
    
    String sensorName = id2name[ID].length() > 0 ? id2name[ID] : ("LaCrosse_" + String(ID));
    
    // NEU: Device ID und Topics basierend auf mqtt_use_names
    String deviceId;
    String uniqueId;
    String configTopic;
    String stateTopic;
    
    if (config.mqtt_use_names && id2name[ID].length() > 0) {
        // Namen-Modus
        String sensorIdentifier = id2name[ID];
        deviceId = mqtt_id + "_" + sensorIdentifier;
        uniqueId = deviceId + "_battery";
        
        // Config Topic: homeassistant/sensor/lacrosse2mqtt_XXXXXX_Wohnzimmer/battery/config
        configTopic = hass_base + deviceId + "/battery/config";
        
        // State Topic: climate/Wohnzimmer/battery
        stateTopic = pretty_base + sensorIdentifier + "/battery";
        
    } else {
        // ID-Modus
        deviceId = mqtt_id + "_" + String(ID);
        uniqueId = deviceId + "_battery";
        
        // Config Topic: homeassistant/sensor/lacrosse2mqtt_XXXXXX_30/battery/config
        configTopic = hass_base + deviceId + "/battery/config";
        
        // State Topic: lacrosse/id/30/battery
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

void publishToMQTT_WH1080(WH1080::Frame *f) {
    String mqttBaseTopic;
    String sensorIdentifier;
    
    if (config.mqtt_use_names && id2name[f->ID].length() > 0) {
        sensorIdentifier = id2name[f->ID];
        mqttBaseTopic = pretty_base + sensorIdentifier + "/";
    } else {
        sensorIdentifier = String(f->ID, DEC);
        mqttBaseTopic = pub_base + sensorIdentifier + "/";
    }
    
    mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(f->temp, 1).c_str());
    mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(f->humi, DEC).c_str());
    mqtt_client.publish((mqttBaseTopic + "rain").c_str(), String(f->rain, 1).c_str());
    mqtt_client.publish((mqttBaseTopic + "wind_speed").c_str(), String(f->wind_speed, 1).c_str());
    mqtt_client.publish((mqttBaseTopic + "wind_gust").c_str(), String(f->wind_gust, 1).c_str());
    mqtt_client.publish((mqttBaseTopic + "wind_dir").c_str(), WH1080::GetWindDirection(f->wind_bearing));
    
    String state = "{"
        "\"RSSI\": " + String(f->rssi, DEC) +
        ", \"type\": \"WH1080\"" +
        ", \"station_id\": " + String(f->station_id) +
        "}";
    mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
}

void publishToMQTT_TX38(TX38IT::Frame *f) {
    String mqttBaseTopic;
    String sensorIdentifier;
    
    if (config.mqtt_use_names && id2name[f->ID].length() > 0) {
        sensorIdentifier = id2name[f->ID];
        mqttBaseTopic = pretty_base + sensorIdentifier + "/";
    } else {
        sensorIdentifier = String(f->ID, DEC);
        mqttBaseTopic = pub_base + sensorIdentifier + "/";
    }
    
    mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(f->temp, 1).c_str());
    
    if (f->humi > 0 && f->humi <= 100) {
        mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(f->humi, DEC).c_str());
    }
    
    String state = "{"
        "\"low_batt\": " + String(f->batlo?"true":"false") +
        ", \"init\": " + String(f->init?"true":"false") +
        ", \"RSSI\": " + String(f->rssi, DEC) +
        ", \"type\": \"TX38IT\"" +
        "}";
    mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
}

void publishToMQTT_TX35(TX35IT::Frame *f) {
    String mqttBaseTopic;
    String sensorIdentifier;
    
    if (config.mqtt_use_names && id2name[f->ID].length() > 0) {
        sensorIdentifier = id2name[f->ID];
        mqttBaseTopic = pretty_base + sensorIdentifier + "/";
    } else {
        sensorIdentifier = String(f->ID, DEC);
        mqttBaseTopic = pub_base + sensorIdentifier + "/";
    }
    
    mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(f->temp, 1).c_str());
    
    if (f->humi > 0 && f->humi <= 100) {
        mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(f->humi, DEC).c_str());
    }
    
    String state = "{"
        "\"low_batt\": " + String(f->batlo?"true":"false") +
        ", \"init\": " + String(f->init?"true":"false") +
        ", \"RSSI\": " + String(f->rssi, DEC) +
        ", \"type\": \"TX35-IT\"" +
        "}";
    mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
}

void publishToMQTT_WS1600(WS1600::Frame *f) {
    String mqttBaseTopic;
    String sensorIdentifier;
    
    if (config.mqtt_use_names && id2name[f->ID].length() > 0) {
        sensorIdentifier = id2name[f->ID];
        mqttBaseTopic = pretty_base + sensorIdentifier + "/";
    } else {
        sensorIdentifier = String(f->ID, DEC);
        mqttBaseTopic = pub_base + sensorIdentifier + "/";
    }
    
    mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(f->temp, 1).c_str());
    mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(f->humi, DEC).c_str());
    mqtt_client.publish((mqttBaseTopic + "rain").c_str(), String(f->rain, 1).c_str());
    mqtt_client.publish((mqttBaseTopic + "wind_speed").c_str(), String(f->wind_speed, 1).c_str());
    
    String state = "{"
        "\"low_batt\": " + String(f->batlo?"true":"false") +
        ", \"RSSI\": " + String(f->rssi, DEC) +
        ", \"type\": \"WS1600\"" +
        ", \"channel\": " + String(f->channel) +
        "}";
    mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
}

void publishToMQTT_WT440(WT440XH::Frame *f) {
    String mqttBaseTopic;
    String sensorIdentifier;
    
    if (config.mqtt_use_names && id2name[f->ID].length() > 0) {
        sensorIdentifier = id2name[f->ID];
        mqttBaseTopic = pretty_base + sensorIdentifier + "/";
    } else {
        sensorIdentifier = String(f->ID, DEC);
        mqttBaseTopic = pub_base + sensorIdentifier + "/";
    }
    
    mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(f->temp, 1).c_str());
    mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(f->humi, DEC).c_str());
    
    String state = "{"
        "\"low_batt\": " + String(f->batlo?"true":"false") +
        ", \"RSSI\": " + String(f->rssi, DEC) +
        ", \"type\": \"WT440XH\"" +
        ", \"channel\": " + String(f->channel) +
        "}";
    mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
}

void expire_cache()
{
    unsigned long now = millis();
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (fcache[i].timestamp > 0 && (now - fcache[i].timestamp) > 300000) {
            memset(&fcache[i], 0, sizeof(struct Cache));
            Serial.print("expired ID ");
            Serial.println(i);
        }
    }
}


void update_display(LaCrosse::Frame *frame)
{
    uint32_t now = uptime_sec();
    
    if (config.display_on) {
        display.ssd1306_command(SSD1306_DISPLAYON);
    } else {
        if (now < auto_display_on + DISPLAY_TIMEOUT) {
            display.ssd1306_command(SSD1306_DISPLAYON);
        } else {
            display.ssd1306_command(SSD1306_DISPLAYOFF);
            return;
        }
    }
    
    bool show_wifi = (uptime_sec() % 70) < 10;
    
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
        // Finde neuesten aktiven Sensor
        byte newestID = 0;
        unsigned long newestTime = 0;
        
        for (int id = 0; id < SENSOR_NUM; id++) {
            if (fcache[id].timestamp > 0 && fcache[id].valid && fcache[id].timestamp > newestTime) {
                newestTime = fcache[id].timestamp;
                newestID = id;
            }
        }
        
        // Zeige Sensor an
        if (newestTime > 0) {
            // Bestimme Base-ID (für Namen)
            byte baseID = newestID;
            
            // Sensor-Name
            String displayName = id2name[baseID].length() > 0 ? id2name[baseID] : ("ID: " + String(baseID));
            
            // Kanal-Info anhängen wenn Kanal 2
            if (fcache[newestID].channel == 2) {
                displayName += " Ch2";
            }
            
            display.println(displayName);
            display.println("----------------");
            
            // Temperatur
            char tempBuf[24];
            snprintf(tempBuf, sizeof(tempBuf), "Temp: %.1fC", fcache[newestID].temp);
            display.println(tempBuf);
            
            // Luftfeuchtigkeit (nur wenn vorhanden)
            if (fcache[newestID].humi > 0 && fcache[newestID].humi <= 100) {
                snprintf(tempBuf, sizeof(tempBuf), "Humidity: %d%%", fcache[newestID].humi);
                display.println(tempBuf);
            }
            
            // Batterie-Status
            if (fcache[newestID].batlo) {
                display.println("BAT: WEAK!");
            }
            
            // RAW Daten
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
    
    // 1. Versuche LaCrosse IT+ (5 Bytes, 17241 bps)
    if (payLoadSize == FRAME_LENGTH && config.proto_lacrosse) {
        LaCrosse::Frame lacrosse_frame;
        lacrosse_frame.rate = rate;
        lacrosse_frame.rssi = rssi;
        
        if (LaCrosse::TryHandleData(payload, &lacrosse_frame)) {
            frame_valid = true;
            add_debug_log(payload, rssi, rate, frame_valid);
            
            byte ID = lacrosse_frame.ID;
            byte channel = lacrosse_frame.channel;
            const char* sensorType = LaCrosse::GetSensorType(&lacrosse_frame);
            
            int cacheIndex = GetCacheIndex(ID, channel);
            
            if (cacheIndex < SENSOR_NUM) {
                LaCrosse::Frame oldframe;
                if (fcache[cacheIndex].timestamp > 0) {
                    LaCrosse::TryHandleData(fcache[cacheIndex].data, &oldframe);
                } else {
                    oldframe.valid = false;
                }
                
                fcache[cacheIndex].ID = lacrosse_frame.ID;
                fcache[cacheIndex].rate = lacrosse_frame.rate;
                fcache[cacheIndex].rssi = rssi;
                fcache[cacheIndex].timestamp = millis();
                memcpy(&fcache[cacheIndex].data, payload, FRAME_LENGTH);
                fcache[cacheIndex].temp = lacrosse_frame.temp;
                fcache[cacheIndex].humi = lacrosse_frame.humi;
                fcache[cacheIndex].batlo = lacrosse_frame.batlo;
                fcache[cacheIndex].init = lacrosse_frame.init;
                fcache[cacheIndex].valid = lacrosse_frame.valid;
                fcache[cacheIndex].channel = lacrosse_frame.channel;
                
                strncpy(fcache[cacheIndex].sensorType, sensorType, 15);
                fcache[cacheIndex].sensorType[15] = '\0';
                
                LaCrosse::DisplayFrame(payload, &lacrosse_frame);
                
                // MQTT Publishing
                String mqttBaseTopic;
                String sensorIdentifier;
                
                if (config.mqtt_use_names && id2name[ID].length() > 0) {
                    sensorIdentifier = id2name[ID];
                    if (channel == 2) {
                        sensorIdentifier += "_Ch2";
                    }
                    mqttBaseTopic = pretty_base + sensorIdentifier + "/";
                } else {
                    sensorIdentifier = String(ID, DEC);
                    if (channel == 2)
                        sensorIdentifier += "/ch2";
                    mqttBaseTopic = pub_base + sensorIdentifier + "/";
                }
                
                mqtt_client.publish((mqttBaseTopic + "temp").c_str(), String(lacrosse_frame.temp, 1).c_str());
                
                if (lacrosse_frame.humi > 0 && lacrosse_frame.humi <= 100) {
                    mqtt_client.publish((mqttBaseTopic + "humi").c_str(), String(lacrosse_frame.humi, DEC).c_str());
                }
                
                String state = "{"
                    "\"low_batt\": " + String(lacrosse_frame.batlo?"true":"false") +
                     ", \"init\": " + String(lacrosse_frame.init?"true":"false") +
                     ", \"RSSI\": " + String(rssi, DEC) +
                     ", \"baud\": " + String(lacrosse_frame.rate / 1000.0, 3) +
                     ", \"channel\": " + String(lacrosse_frame.channel) +
                     ", \"type\": \"" + String(sensorType) + "\"" +
                     "}";
                mqtt_client.publish((mqttBaseTopic + "state").c_str(), state.c_str());
                
                if (config.ha_discovery && id2name[ID].length() > 0) {
                    if (oldframe.valid && abs(oldframe.temp - lacrosse_frame.temp) > 2.0) {
                        Serial.println(String("skipping invalid temp diff: ") + String(oldframe.temp - lacrosse_frame.temp, 1));
                    } else {
                        int haConfig = (channel == 2) ? 2 : 1;
                        pub_hass_config(haConfig, ID, channel);
                    }
                    
                    if (lacrosse_frame.humi > 0 && lacrosse_frame.humi <= 100) {
                        if (oldframe.valid && abs(oldframe.humi - lacrosse_frame.humi) > 10) {
                            Serial.println(String("skipping invalid humi diff: ") + String(oldframe.humi - lacrosse_frame.humi, DEC));
                        } else {
                            pub_hass_config(0, ID, channel);
                        }
                    }
                }
            }
            
            if (!showing_starfield) {
                LaCrosse::Frame dummy_frame;
                update_display(&dummy_frame);
            }
            
            SX.EnableReceiver(true);
            digitalWrite(LED_BUILTIN, LOW);
            return;
        }
        
        // 2. Versuche TX38IT (5 Bytes, 8842 bps)
        if (config.proto_tx38it && rate == 8842) {
            TX38IT::Frame tx38_frame;
            tx38_frame.rssi = rssi;
            tx38_frame.rate = rate;
            
            if (TX38IT::TryHandleData(payload, &tx38_frame)) {
                frame_valid = true;
                add_debug_log(payload, rssi, rate, frame_valid);
                TX38IT::DisplayFrame(payload, &tx38_frame);
                publishToMQTT_TX38(&tx38_frame);
                
                if (!showing_starfield) {
                    LaCrosse::Frame dummy_frame;
                    update_display(&dummy_frame);
                }
                
                SX.EnableReceiver(true);
                digitalWrite(LED_BUILTIN, LOW);
                return;
            }
        }
        
        // 3. Versuche TX35IT (5 Bytes, 9579 bps)
        if (config.proto_tx35it && rate == 9579) {
            TX35IT::Frame tx35_frame;
            tx35_frame.rssi = rssi;
            tx35_frame.rate = rate;
            
            if (TX35IT::TryHandleData(payload, &tx35_frame)) {
                frame_valid = true;
                add_debug_log(payload, rssi, rate, frame_valid);
                TX35IT::DisplayFrame(payload, &tx35_frame);
                publishToMQTT_TX35(&tx35_frame);
                
                if (!showing_starfield) {
                    LaCrosse::Frame dummy_frame;
                    update_display(&dummy_frame);
                }
                
                SX.EnableReceiver(true);
                digitalWrite(LED_BUILTIN, LOW);
                return;
            }
        }
    }
    
    // 4. Versuche WH1080 (10 Bytes)
    if (payLoadSize == 10 && config.proto_wh1080) {
        WH1080::Frame wh1080_frame;
        wh1080_frame.rssi = rssi;
        wh1080_frame.rate = rate;
        
        if (WH1080::TryHandleData(payload, payLoadSize, &wh1080_frame)) {
            frame_valid = true;
            add_debug_log(payload, rssi, rate, frame_valid);
            WH1080::DisplayFrame(payload, payLoadSize, &wh1080_frame);
            publishToMQTT_WH1080(&wh1080_frame);
            
            if (!showing_starfield) {
                LaCrosse::Frame dummy_frame;
                update_display(&dummy_frame);
            }
            
            SX.EnableReceiver(true);
            digitalWrite(LED_BUILTIN, LOW);
            return;
        }
    }
    
    // 5. Versuche WS1600 (9 Bytes)
    if (payLoadSize == 9 && config.proto_ws1600) {
        WS1600::Frame ws1600_frame;
        ws1600_frame.rssi = rssi;
        ws1600_frame.rate = rate;
        
        if (WS1600::TryHandleData(payload, payLoadSize, &ws1600_frame)) {
            frame_valid = true;
            add_debug_log(payload, rssi, rate, frame_valid);
            WS1600::DisplayFrame(payload, payLoadSize, &ws1600_frame);
            publishToMQTT_WS1600(&ws1600_frame);
            
            if (!showing_starfield) {
                LaCrosse::Frame dummy_frame;
                update_display(&dummy_frame);
            }
            
            SX.EnableReceiver(true);
            digitalWrite(LED_BUILTIN, LOW);
            return;
        }
    }
    
    // 6. Versuche WT440XH (4 Bytes)
    if (payLoadSize == 4 && config.proto_wt440xh) {
        WT440XH::Frame wt440_frame;
        wt440_frame.rssi = rssi;
        wt440_frame.rate = rate;
        
        if (WT440XH::TryHandleData(payload, &wt440_frame)) {
            frame_valid = true;
            add_debug_log(payload, rssi, rate, frame_valid);
            WT440XH::DisplayFrame(payload, &wt440_frame);
            publishToMQTT_WT440(&wt440_frame);
            
            if (!showing_starfield) {
                LaCrosse::Frame dummy_frame;
                update_display(&dummy_frame);
            }
            
            SX.EnableReceiver(true);
            digitalWrite(LED_BUILTIN, LOW);
            return;
        }
    }
    
    // Unbekanntes Protokoll
    static unsigned long last;
    LaCrosse::DisplayRaw(last, "Unknown", payload, payLoadSize, rssi, rate);
    Serial.println();
    add_debug_log(payload, rssi, rate, false);
    
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
    
    // Starfield-Animation beim Booten (10 Sekunden)
    init_starfield();
    unsigned long boot_animation_start = millis();
    while (millis() - boot_animation_start < 5000) {
        draw_starfield();
        delay(50);
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
    SX.SetupForLaCrosse();
    // Konfiguriere aktive Datenraten basierend auf Protokollen
    bool use_17241 = config.proto_lacrosse;
    bool use_9579 = config.proto_tx35it;
    bool use_8842 = config.proto_tx38it;
    
    SX.SetActiveDataRates(use_17241, use_9579, use_8842);
    SX.SetFrequency(freq);
    SX.NextDataRate(0);  // Starte mit erster Rate
    SX.EnableReceiver(true);

}


uint32_t check_button()
{
    static uint32_t low_at = 0;
    static bool pressed = false;
    if (digitalRead(KEY_BUILTIN) == LOW) {
        if (! pressed)
            low_at = millis();
        pressed = true;
        return 0;
    }
    if (! pressed)
        return 0;
    pressed = false;
    return millis() - low_at;
}

static int last_state = -1;

void loop(void)
{
    static unsigned long last_starfield_update = 0;
    static uint32_t last_interaction = 0;  
    static bool interaction_initialized = false;
    static uint32_t last_wifi_display = 0;
    
    handle_client();

    uint32_t button_time = check_button();
    if (button_time > 0) {
        Serial.print("button_time: ");
        Serial.println(button_time);
    }
    
    if (button_time > 100 && button_time <= 2000) {
        if (!config.display_on) {
            auto_display_on = uptime_sec();
            Serial.println("Display reactivated for 5 minutes");
        } else {
            Serial.println("Display always on (change in webfrontend)");
        }
        showing_starfield = false;
        last_interaction = uptime_sec();
        update_display(NULL);
    }

    receive();
    check_repeatedjobs();
    expire_cache();
    
    // WiFi-Zustandsänderung = Interaktion
    if (last_state != wifi_state) {
        last_state = wifi_state;
        wifi_disp = String(_wifi_state_str[wifi_state]);
        auto_display_on = uptime_sec();
        showing_starfield = false;
        last_interaction = uptime_sec();
        last_wifi_display = uptime_sec();
        update_display(NULL);
    }
    
    unsigned long now = millis();
    uint32_t uptime = uptime_sec();
    
    if (!interaction_initialized) {
        last_interaction = uptime;
        last_wifi_display = uptime;
        interaction_initialized = true;
    }
    
    // Prüfe auf kritische Fehler (NUR Battery-Fehler)
    bool has_critical_error = false;
    bool has_recent_data = false;
    
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (fcache[i].timestamp > 0) {
            unsigned long age = now - fcache[i].timestamp;
            
            // Daten sind aktuell (< 5 Minuten)
            if (age < 300000) {
                has_recent_data = true;
                
                // NUR Battery-Low ist kritischer Fehler
                if (fcache[i].batlo) {
                    has_critical_error = true;
                    // NUR bei NEUEM Battery-Fehler Interaktion setzen
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
        // WiFi-Anzeige-Logik abhängig von Screensaver-Modus
        bool show_wifi;
        if (config.screensaver_mode && config.display_on) {
            // Screensaver-Modus: WiFi nur alle 5 Minuten (300 Sekunden)
            uint32_t time_since_last_wifi = uptime - last_wifi_display;
            show_wifi = (time_since_last_wifi >= 300) && (time_since_last_wifi < 310);
        } else {
            // Normal-Modus: WiFi alle 70 Sekunden für 10 Sekunden
            show_wifi = (uptime % 70) < 10;
        }
        
        // SCREENSAVER LOGIK
        if (config.screensaver_mode && config.display_on) {
            uint32_t idle_time = uptime - last_interaction;
            
            bool should_show_screensaver = (idle_time > 300) && 
                                          !has_critical_error && 
                                          !show_wifi;
            
            if (should_show_screensaver) {
                if ((now - last_starfield_update > 50)) {
                    last_starfield_update = now;
                    if (!showing_starfield) {
                        Serial.println(">>> STARTING SCREENSAVER <<<");
                        showing_starfield = true;
                    }
                    draw_starfield();
                }
            } else {
                if (showing_starfield) {
                    Serial.println(">>> STOPPING SCREENSAVER <<<");
                    showing_starfield = false;
                    update_display(NULL);
                } else if (show_wifi) {
                    update_display(NULL);
                    last_wifi_display = uptime;
                }
            }
        } else {
            if (!show_wifi) {
                if (!has_recent_data && (now - last_starfield_update > 50)) {
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
}