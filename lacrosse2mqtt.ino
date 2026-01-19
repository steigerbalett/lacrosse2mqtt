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

bool DEBUG_MODE = 0;
const int interval = 20;   /* toggle interval in seconds */
const int freq = 868290;   /* frequency in kHz, 868300 did not receive all sensors... */

unsigned long last_reconnect;
unsigned long last_switch = 0;
// unsigned long last_display = 0;
bool littlefs_ok;
bool mqtt_ok;
// bool display_on = true;
uint32_t auto_display_on = 0;

Config config;
Cache fcache[SENSOR_NUM]; /* 128 IDs x 2 datarates */
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
const String pub_base = "lacrosse/id_";
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
            mqtt_server_set = true; /* to avoid trying connection with invalid settings */
        } else
            Serial.println("MQTT server name not configured");
        mqtt_client.setKeepAlive(60); /* same as python's paho.mqtt.client */
        Serial.print("MQTT SERVER: "); Serial.println(config.mqtt_server);
        Serial.print("MQTT PORT:   "); Serial.println(config.mqtt_port);
        last_reconnect = 0; /* trigger connect() */
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
#if 0
    if (now - last_display > 10000) /* update display at least every 10 seconds, even if nothing */
        update_display(NULL);       /* is received. Indicates that the thing is still alive ;-) */
#endif
    mqtt_ok = mqtt_client.connected();
}

void pub_hass_config(int what, byte ID)
{
    static const String name[3] = { "Luftfeuchtigkeit", "Temperatur", "Temperatur 2" };
    static const String value[3] = { "humi", "temp", "temp2" };
    static const String dclass[3] = { "humidity", "temperature", "temperature" };
    static const String unit[3] = { "%", "°C", "°C" };
    String where = id2name[ID];

    if (!config.ha_discovery)
        return;
    /* only send once */
    if (hass_cfg[ID] & (1 << what)) /* what = 0 (humi), 1 (temp), 2 (temp2) */
        return;
    hass_cfg[ID] |= (1 << what);

    String where_lower = where;
    where_lower.toLowerCase();
    
    String uid = mqtt_id + "_" + where_lower + "_" + value[what];
    String topic = hass_base + uid + "/config";

    String device_id = mqtt_id + value[what];

    String msg = "{"
            "\"device\":{"
                "\"identifiers\":[\"" + device_id + "\"],"
                "\"name\":\"" + where + " Sensor\","
                "\"manufacturer\":\"Lacrosse2MQTT\","
                "\"sw_version\":\"v2026\","
                "\"model\":\"heltec_wifi_lora_32_V2\""
            "},"
            "\"origin\":{"
                "\"name\":\"lacrosse2mqtt\","
                "\"url\":\"https://github.com/steigerbalett/lacrosse2mqtt\","
                "\"sw_version\":\"v2026\""
            "},"
            "\"state_class\":\"measurement\","
            "\"device_class\":\"" + dclass[what]+ "\","
            "\"unit_of_measurement\":\"" + unit[what] + "\","
            "\"unique_id\":\"" + uid + "\","
            "\"state_topic\":\"" + pretty_base + where+"/"+value[what]+"\","
            "\"name\":\""+where+"\"" +
        "}";

    Serial.println("HA Discovery: " + topic);
    Serial.println(topic.length());
    Serial.println(msg);
    Serial.println(msg.length());

    mqtt_client.beginPublish(topic.c_str(), msg.length(), true);
    mqtt_client.print(msg);
    mqtt_client.endPublish();
}

void expire_cache()
{
    /* clear all entries older than 300 seconds... */
    unsigned long now = millis();
    for (int i = 0; i < (sizeof(fcache) / sizeof(struct Cache)); i++) {
        if (fcache[i].timestamp > 0 && (now - fcache[i].timestamp) > 300000) {
            memset(&fcache[i], 0, sizeof(struct Cache));
            Serial.print("expired ID ");
            Serial.println(i);
        }
    }
}

String wifi_disp;
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
        display.println("****************************");        
        display.println("SSID: " + WiFi.SSID());
        display.println("IP: " + WiFi.localIP().toString());
        display.setCursor(0, 54);
        display.println("MQTT: " + String(mqtt_ok ? "OK" : "---"));
        display.display();
    } else {
        if (frame && frame->valid) {
            if (id2name[frame->ID].length() > 0) {
                display.println(id2name[frame->ID]);
            } else {
                display.println("ID: " + String(frame->ID));
            }
            
            char tempBuf[24];
            snprintf(tempBuf, sizeof(tempBuf), "Temp: %.1f C", frame->temp);
            display.println(tempBuf);
            
            if (frame->humi > 0 && frame->humi <= 100) {
                snprintf(tempBuf, sizeof(tempBuf), "Humi: %d %%", frame->humi);
                display.println(tempBuf);
            }
            
            String rawHex = "RAW: ";
            for (int i = 0; i < 5 && i < FRAME_LENGTH; i++) {
                if (fcache[frame->ID].data[i] < 16) rawHex += "0";
                rawHex += String(fcache[frame->ID].data[i], HEX);
                if (i < 4) rawHex += " ";
            }
            display.setCursor(0, 54);
            display.println(rawHex);
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

    if (DEBUG_MODE) {
        Serial.print("\nEnd receiving, HEX raw data: ");
        for (int i = 0; i < 16; i++) {
            Serial.print(payload[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }

    LaCrosse::Frame frame;
    frame.rate = rate;
    if (LaCrosse::TryHandleData(payload, &frame)) {
        byte ID = frame.ID;
        
        // Erkenne Kanal 2 (ID wurde in TryHandleData um 64 erhöht)
        bool isChannel2 = (ID >= 64);
        byte baseID = isChannel2 ? (ID - 64) : ID;
        
        LaCrosse::Frame oldframe;
        LaCrosse::TryHandleData(fcache[ID].data, &oldframe);
        fcache[ID].rssi = rssi;
        fcache[ID].timestamp = millis();
        memcpy(&fcache[ID].data, payload, FRAME_LENGTH);
        frame.rssi = rssi;
        LaCrosse::DisplayFrame(payload, &frame);
        
        // MQTT: Publiziere mit tatsächlicher ID (inkl. Kanal-Offset)
        String pub = pub_base + String(ID, DEC) + "/";
        mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
        
        if (frame.humi > 0 && frame.humi <= 100)
            mqtt_client.publish((pub + "humi").c_str(), String(frame.humi, DEC).c_str());
        
        String state = "";
        state += "{\"low_batt\": " + String(frame.batlo?"true":"false") +
                 ", \"init\": " + String(frame.init?"true":"false") +
                 ", \"RSSI\": " + String(rssi, DEC) +
                 ", \"baud\": " + String(rate / 1000.0, 3) +
                 ", \"channel\": " + String(isChannel2 ? 2 : 1) +
                 "}";
        mqtt_client.publish((pub + "state").c_str(), state.c_str());
        
        // Pretty names mit Kanal-Erkennung
        if (id2name[baseID].length() > 0) {
            String sensorName = id2name[baseID];
            if (isChannel2) {
                sensorName += "_Ch2";  // z.B. "Wohnzimmer_Ch2"
            }
            
            pub = pretty_base + sensorName + "/";
            
            if (abs(oldframe.temp - frame.temp) > 2.0)
                Serial.println(String("skipping invalid temp diff: ") + String(oldframe.temp - frame.temp,1));
            else {
                pub_hass_config(1, ID);  // Nutze die tatsächliche ID (mit Offset)
                mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
            }
            
            if (frame.humi > 0 && frame.humi <= 100) {
                if (abs(oldframe.humi - frame.humi) > 10)
                    Serial.println(String("skipping invalid humi diff: ") + String(oldframe.humi - frame.humi, DEC));
                else {
                    pub_hass_config(0, ID);
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
    config.mqtt_port = 1883; /* default */
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
    setup_web(); /* also loads config from LittleFS */

    pinMode(KEY_BUILTIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
        Wire.begin(OLED_SDA, OLED_SCL);
    
    // NEU: Display-Initialisierung für Adafruit_SSD1306
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
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
#if 0
    Serial.println("LaCrosse::Frame Cache fcache id2name size: ");
    Serial.println(sizeof(LaCrosse::Frame));
    Serial.println(sizeof(Cache));
    Serial.println(sizeof(fcache));
    Serial.println(sizeof(id2name));
#endif
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LaCrosse2mqtt");
    display.println("Starting...");
    display.display();
    delay(2000);

    last_switch = millis();

    if (!SX.init()) {
        Serial.println("***** SX127x init failed! ****");
        display.println("***** SX127x init failed! ****");
        display.display();
        while(true)
            delay(1000);
    }
    SX.SetupForLaCrosse();
    SX.SetFrequency(freq);
    SX.NextDataRate(0);
    SX.EnableReceiver(true);

#ifdef DEBUG_DAVFS
    tcp.begin();
    dav.begin(&tcp, &LittleFS);
    dav.setTransferStatusCallback([](const char* name, int percent, bool receive)
    {
        Serial.printf("%s: '%s': %d%%\n", receive ? "recv" : "send", name, percent);
    });
#endif
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
    /* if button was pressed and now released, return how long
     * it has been down */
    pressed = false;
    return millis() - low_at;
}

static int last_state = -1;
void loop(void)
{
    static unsigned long last_starfield_update = 0;
    static bool showing_starfield = false;  // NEU: Track Starfield-Status
    
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
        showing_starfield = false;  // NEU: Reset bei Button-Press
        update_display(NULL);
    }

    receive();
    check_repeatedjobs();
    expire_cache();
    
    if (last_state != wifi_state) {
        last_state = wifi_state;
        wifi_disp = String(_wifi_state_str[wifi_state]);
        auto_display_on = uptime_sec();
        showing_starfield = false;  // NEU: Reset bei WiFi-Change
        update_display(NULL);
    }
    
    unsigned long now = millis();
    uint32_t uptime = uptime_sec();
    
    bool display_should_be_on = config.display_on || (uptime < auto_display_on + DISPLAY_TIMEOUT);
    
    if (display_should_be_on) {
        bool show_wifi = (uptime % 70) < 10;
        
        if (!show_wifi) {
            bool has_recent_data = false;
            for (int i = 0; i < SENSOR_NUM; i++) {
                if (fcache[i].timestamp > 0 && (now - fcache[i].timestamp) < 300000) {
                    has_recent_data = true;
                    break;
                }
            }
            
            if (!has_recent_data && (now - last_starfield_update > 50)) {
                last_starfield_update = now;
                showing_starfield = true;
                draw_starfield();  // Direkt aufrufen für Animation
            } else if (has_recent_data && showing_starfield) {
                showing_starfield = false;
                update_display(NULL);
            }
        } else {
            if (showing_starfield) {
                showing_starfield = false;
                update_display(NULL);  // Wechsel zu WiFi-Info
            }
        }
    } else {
        showing_starfield = false;  // Display ist aus
    }
}