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
 * with this program; if not, got to https://www.gnu.org/licenses/
 */

#include <LittleFS.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "wifi_functions.h"
#include "webfrontend.h"
#include "globals.h"
#include "lacrosse.h"
#include "SX127x.h"
#include <WiFiManager.h>

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
const String pretty_base = "climate/";
const String pub_base = "lacrosse/id/";
const String hass_base = "homeassistant/sensor/";
bool mqtt_server_set = false;

void check_repeatedjobs()
{
    /* Toggle the data rate fast/slow */
    unsigned long now = millis();
    if (now - last_switch > interval * 1000) {
        SX.NextDataRate();
        last_switch = now;
    }
    if (config.changed) {
        Serial.println("MQTT config changed. Dis- and reconnecting...");
        config.changed = false;
        if (mqtt_ok)
            mqtt_client.disconnect();
        if (config.mqtt_server.length() > 0) {
            const char *_server = config.mqtt_server.c_str();
            mqtt_client.setServer(_server, config.mqtt_port);
            mqtt_server_set = true;
        } else
            Serial.println("MQTT server name not configured");
        mqtt_client.setKeepAlive(60);
        Serial.print("MQTT SERVER: "); Serial.println(config.mqtt_server);
        Serial.print("MQTT PORT:   "); Serial.println(config.mqtt_port);
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
            if (mqtt_client.connect(mqtt_id.c_str(), user, pass)) {
                Serial.println("OK!");
                for (int i = 0; i < SENSOR_NUM; i++)
                    hass_cfg[i] = 0;
            } else
                Serial.println("FAILED");
        }
        last_reconnect = now;
    }
    mqtt_ok = mqtt_client.connected();
}

// KORRIGIERT: Home Assistant Discovery Config
void pub_hass_config(int what, byte ID, byte channel)
{
    // what: 0 = humidity, 1 = temp (channel 1), 2 = temp (channel 2)
    static const String name_suffix[3] = { " Humidity", " Temperature", " Temperature Ch2" };
    static const String value[3] = { "humi", "temp", "temp" };
    static const String dclass[3] = { "humidity", "temperature", "temperature" };
    static const String unit[3] = { "%", "°C", "°C" };
    
    if (!config.ha_discovery)
        return;
    
    // Prüfe ob schon konfiguriert
    byte configMask = (1 << what);
    if (hass_cfg[ID] & configMask)
        return;
    hass_cfg[ID] |= configMask;
    
    String sensorName = id2name[ID].length() > 0 ? id2name[ID] : ("LaCrosse_" + String(ID));
    String channelSuffix = (channel == 2) ? "_ch2" : "";
    String uniqueId = mqtt_id + "_" + String(ID) + channelSuffix + "_" + value[what];
    
    String topic = hass_base + uniqueId + "/config";
    String stateTopic = pub_base + String(ID) + channelSuffix + "/" + value[what];
    
    String msg = "{"
            "\"device\":{"
                "\"identifiers\":[\"" + mqtt_id + "_" + String(ID) + "\"],"
                "\"name\":\"" + sensorName + "\","
                "\"manufacturer\":\"LaCrosse\","
                "\"sw_version\":\"v2026.1\","
                "\"model\":\"LaCrosse IT+\""
            "},"
            "\"origin\":{"
                "\"name\":\"lacrosse2mqtt\","
                "\"url\":\"https://github.com/seyd/lacrosse2mqtt\","
                "\"sw_version\":\"v2026.1\""
            "},"
            "\"state_class\":\"measurement\","
            "\"device_class\":\"" + dclass[what]+ "\","
            "\"unit_of_measurement\":\"" + unit[what] + "\","
            "\"unique_id\":\"" + uniqueId + "\","
            "\"state_topic\":\"" + stateTopic + "\","
            "\"name\":\"" + sensorName + name_suffix[what] + "\""
        "}";

    Serial.println("HA Discovery: " + topic);
    Serial.println("Msg length: " + String(msg.length()));
    
    mqtt_client.beginPublish(topic.c_str(), msg.length(), true);
    mqtt_client.print(msg);
    mqtt_client.endPublish();
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

String wifi_disp;

// KORRIGIERT: Display-Update Funktion
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

    LaCrosse::Frame frame;
    frame.rate = rate;
    frame.rssi = rssi;
    
    bool frame_valid = LaCrosse::TryHandleData(payload, &frame);
    add_debug_log(payload, rssi, rate, frame_valid);
    
    if (frame_valid) {
        byte ID = frame.ID;
        byte channel = frame.channel;
        
        // NEU: Berechne Cache-Index basierend auf ID + Kanal
        int cacheIndex = GetCacheIndex(ID, channel);
        
        if (cacheIndex >= SENSOR_NUM) {
            Serial.printf("ERROR: Cache index %d out of bounds for ID%d Ch%d\n", 
                         cacheIndex, ID, channel);
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
        fcache[cacheIndex].ID = frame.ID;          // Originale ID
        fcache[cacheIndex].rate = frame.rate;
        fcache[cacheIndex].rssi = rssi;
        fcache[cacheIndex].timestamp = millis();
        memcpy(&fcache[cacheIndex].data, payload, FRAME_LENGTH);
        fcache[cacheIndex].temp = frame.temp;
        fcache[cacheIndex].humi = frame.humi;
        fcache[cacheIndex].batlo = frame.batlo;
        fcache[cacheIndex].init = frame.init;
        fcache[cacheIndex].valid = frame.valid;
        fcache[cacheIndex].channel = frame.channel;
        
        // Sensor-Typ speichern
        const char* sensorType = LaCrosse::GetSensorType(&frame);
        strncpy(fcache[cacheIndex].sensorType, sensorType, 15);
        fcache[cacheIndex].sensorType[15] = '\0';
        
        LaCrosse::DisplayFrame(payload, &frame);
        
        // MQTT Topic: ID30/ch2 oder ID30
        String idStr = String(ID, DEC);
        if (channel == 2)
            idStr += "/ch2";
        
        String pub = pub_base + idStr + "/";
        
        mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
        
        if (frame.humi > 0 && frame.humi <= 100) {
            mqtt_client.publish((pub + "humi").c_str(), String(frame.humi, DEC).c_str());
        }
        
        // MQTT state
        String state = ""
            "{\"low_batt\": " + String(frame.batlo?"true":"false") +
             ", \"init\": " + String(frame.init?"true":"false") +
             ", \"RSSI\": " + String(rssi, DEC) +
             ", \"baud\": " + String(frame.rate / 1000.0, 3) +
             ", \"channel\": " + String(frame.channel) +
             ", \"type\": \"" + String(sensorType) + "\"" +
             "}";
        mqtt_client.publish((pub + "state").c_str(), state.c_str());
        
        // Pretty names mit ID
        if (id2name[ID].length() > 0) {
            String sensorName = id2name[ID];
            if (channel == 2) {
                sensorName += "_Ch2";
            }
            
            pub = pretty_base + sensorName + "/";
            
            if (oldframe.valid && abs(oldframe.temp - frame.temp) > 2.0) {
                Serial.println(String("skipping invalid temp diff: ") + String(oldframe.temp - frame.temp, 1));
            } else {
                int haConfig = (channel == 2) ? 2 : 1;
                pub_hass_config(haConfig, ID, channel);
                mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
            }
            
            if (frame.humi > 0 && frame.humi <= 100) {
                if (oldframe.valid && abs(oldframe.humi - frame.humi) > 10) {
                    Serial.println(String("skipping invalid humi diff: ") + String(oldframe.humi - frame.humi, DEC));
                } else {
                    pub_hass_config(0, ID, channel);
                    mqtt_client.publish((pub + "humi").c_str(), String(frame.humi, DEC).c_str());
                }
            }
        }

    } else {
        static unsigned long last;
        LaCrosse::DisplayRaw(last, "Unknown", payload, payLoadSize, rssi, rate);
        Serial.println();
    }

    update_display(&frame);
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
    while (millis() - boot_animation_start < 10000) {
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
    SX.SetFrequency(freq);
    SX.NextDataRate(0);
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
    static bool showing_starfield = false;
    static uint32_t last_data_received = 0;  // NEU: Track letzten Datenempfang
    
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
        update_display(NULL);
    }

    receive();
    check_repeatedjobs();
    expire_cache();
    
    if (last_state != wifi_state) {
        last_state = wifi_state;
        wifi_disp = String(_wifi_state_str[wifi_state]);
        auto_display_on = uptime_sec();
        showing_starfield = false;
        update_display(NULL);
    }
    
    unsigned long now = millis();
    uint32_t uptime = uptime_sec();
    
    // NEU: Prüfe ob kürzlich Daten empfangen wurden
    bool has_recent_data = false;
    bool has_error = false;
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (fcache[i].timestamp > 0 && (now - fcache[i].timestamp) < 300000) {
            has_recent_data = true;
            last_data_received = uptime;
            
            // Prüfe auf Fehler (schwache Batterie)
            if (fcache[i].batlo) {
                has_error = true;
            }
        }
    }
    
    // Prüfe MQTT Fehler
    if (!mqtt_ok) {
        has_error = true;
    }
    
    bool display_should_be_on = config.display_on || (uptime < auto_display_on + DISPLAY_TIMEOUT);
    
    if (display_should_be_on) {
        bool show_wifi = (uptime % 70) < 10;
        
        // NEU: Screensaver Logik
        if (config.screensaver_mode && config.display_on) {
            // Bei "Display always on" + Screensaver Mode:
            // Zeige Screensaver nach 5 Minuten AUSSER bei Fehlern
            uint32_t idle_time = uptime - last_data_received;
            
            if (idle_time > 300 && !has_error) {
                // Mehr als 5 Minuten keine Daten UND kein Fehler -> Screensaver
                if ((now - last_starfield_update > 50)) {
                    last_starfield_update = now;
                    showing_starfield = true;
                    draw_starfield();
                }
            } else {
                // Daten vorhanden ODER Fehler -> Normales Display
                if (showing_starfield) {
                    showing_starfield = false;
                    update_display(NULL);
                }
            }
        } else {
            // Alte Logik: Screensaver nur wenn keine Daten
            if (!show_wifi) {
                if (!has_recent_data && (now - last_starfield_update > 50)) {
                    last_starfield_update = now;
                    showing_starfield = true;
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