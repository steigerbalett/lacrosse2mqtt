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
#include <SSD1306Wire.h>
#include "wifi_functions.h"
#include "webfrontend.h"
#include "globals.h"

#include "lacrosse.h"
#include "SX127x.h"

#include <WiFiManager.h>

//#define DEBUG_DAVFS

#define FORMAT_LITTLEFS_IF_FAILED false

/* if display is default to off, keep it on for this many seconds after power on
 * or a wifi change event */
#define DISPLAY_TIMEOUT 300

#ifdef DEBUG_DAVFS
WiFiServer tcp(81);
ESPWebDAV dav;
#endif

bool DEBUG = 0;
const int interval = 20;   /* toggle interval in seconds */
const int freq = 868290;   /* frequency in kHz, 868300 did not receive all sensors... */

unsigned long last_reconnect;
unsigned long last_switch = 0;
// unsigned long last_display = 0;
bool littlefs_ok;
bool mqtt_ok;
bool display_on = true;
uint32_t auto_display_on = 0;

Config config;
Cache fcache[SENSOR_NUM]; /* 128 IDs x 2 datarates */
String id2name[SENSOR_NUM];
uint8_t hass_cfg[SENSOR_NUM];

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL);

SX127x SX(LORA_CS, LORA_RST);

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
    static const String name[2] = { "Luftfeuchtigkeit", "Temperatur" };
    static const String value[2] = { "humi", "temp" };
    static const String dclass[2] = { "humidity", "temperature" };
    static const String unit[2] = { "%", "°C" };
    String where = id2name[ID];

    if (!config.ha_discovery)
        return;
    /* only send once */
    if (hass_cfg[ID] & (1 << what))
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
                "\"url\":\"https://github.com/steigerbalett/lacrosse2mqtt\""
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
    if (display_on)
        display.displayOn();
    else if (now < auto_display_on + DISPLAY_TIMEOUT) {
        display.displayOn();
    } else {
        display.displayOff();
        return;
    }
    
    bool show_wifi = (now / 60) & 0x01;
    
    display.clear();
    display.setColor(WHITE);
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    
    if (show_wifi) {
        display.drawString(0, 0, "WiFi Status:");
        display.drawString(0, 14, "SSID:");
        display.drawString(40, 14, WiFi.SSID());
        display.drawString(0, 28, "IP:");
        display.drawString(40, 28, WiFi.localIP().toString());
        display.drawString(0, 42, "Status:");
        display.drawString(50, 42, wifi_disp);
    } else {
        display.drawString(0, 0, "Sensor Data:");
        
        if (frame && frame->valid) {
            if (id2name[frame->ID].length() > 0) {
                display.drawString(0, 14, id2name[frame->ID]);
            } else {
                display.drawString(0, 14, "ID: " + String(frame->ID));
            }
            
            char tempBuf[20];
            snprintf(tempBuf, sizeof(tempBuf), "T:%.1fC H:%d%%", frame->temp, frame->humi);
            display.drawString(0, 28, tempBuf);
            
            // RAW Daten aus Cache holen
            display.drawString(0, 42, "RAW:");
            String rawHex = "";
            for (int i = 0; i < 8 && i < FRAME_LENGTH; i++) {
                if (fcache[frame->ID].data[i] < 16) rawHex += "0";
                rawHex += String(fcache[frame->ID].data[i], HEX);
                if (i < 7) rawHex += " ";
            }
            display.drawString(0, 54, rawHex);
        } else {
            display.drawString(0, 28, "Keine gültigen");
            display.drawString(0, 42, "Daten empfangen");
        }
    }
    
    display.display();
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

    String rawHex = "";
    for (int i = 0; i < 16; i++) {
        if (payload[i] < 16) rawHex += "0";
        rawHex += String(payload[i], HEX);
        rawHex += " ";
    }
    addRawData(rawHex + " RSSI:" + String(rssi) + " Rate:" + String(rate));

    if (DEBUG) {
        Serial.print("\nEnd receiving, HEX raw data: ");
        for (int i = 0; i < 16; i++) {
            Serial.print(payload[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }

    /* check if it can be decoded */
    LaCrosse::Frame frame;
    frame.rate = rate;
    if (LaCrosse::TryHandleData(payload, &frame)) {
        LaCrosse::Frame oldframe;
        byte ID = frame.ID;
        LaCrosse::TryHandleData(fcache[ID].data, &oldframe);
        fcache[ID].rssi = rssi;
        fcache[ID].timestamp = millis();
        memcpy(&fcache[ID].data, payload, FRAME_LENGTH);
        frame.rssi = rssi;
        LaCrosse::DisplayFrame(payload, &frame);
        String pub = pub_base + String(ID, DEC) + "/";
        mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
        if (frame.humi <= 100)
            mqtt_client.publish((pub + "humi").c_str(), String(frame.humi, DEC).c_str());
        String state = "";
        state += "{\"low_batt\": " + String(frame.batlo?"true":"false") +
                 ", \"init\": " + String(frame.init?"true":"false") +
                 ", \"RSSI\": " + String(rssi, DEC) +
                 ", \"baud\": " + String(rate / 1000.0, 3) +
                 "}";
        mqtt_client.publish((pub + "state").c_str(), state.c_str());
        if (id2name[ID].length() > 0) {
            pub = pretty_base + id2name[ID] + "/";
            if (abs(oldframe.temp - frame.temp) > 2.0)
                Serial.println(String("skipping invalid temp diff bigger than 2K: ") + String(oldframe.temp - frame.temp,1));
            else {
                pub_hass_config(1, ID);
                mqtt_client.publish((pub + "temp").c_str(), String(frame.temp, 1).c_str());
            }
            if (frame.humi <= 100) {
                if (abs(oldframe.humi - frame.humi) > 10)
                    Serial.println(String("skipping invalid humi diff > 10%: ") + String(oldframe.humi - frame.humi, DEC));
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

    if (!wifiManager.autoConnect("HeltecLaCrosseAP")) {
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
    display_on = config.display_on;

    pinMode(KEY_BUILTIN, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW); // set GPIO16 low to reset OLED
    delay(50);
    digitalWrite(OLED_RST, HIGH);

    display.init();
    display.setContrast(16); /* it is for debug only, so dimming is ok */
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    delay(1000); /* for Serial to really work */

    Serial.println("TTGO LORA lacrosse2mqtt converter");
    Serial.println(mqtt_id);
#if 0
    Serial.println("LaCrosse::Frame Cache fcache id2name size: ");
    Serial.println(sizeof(LaCrosse::Frame));
    Serial.println(sizeof(Cache));
    Serial.println(sizeof(fcache));
    Serial.println(sizeof(id2name));
#endif
    display.drawString(0,0,"LaCrosse2mqtt");
    display.display();

    last_switch = millis();

    if (!SX.init()) {
        Serial.println("***** SX127x init failed! ****");
        display.drawString(0,24, "***** SX127x init failed! ****");
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
    handle_client();

    uint32_t button_time = check_button();
    if (button_time > 0) {
        Serial.print("button_time: ");
        Serial.println(button_time);
    }
    
    if (button_time > 100 && button_time <= 2000) {
        display_on = ! display_on;
        /* ensure that display can be turned off while timeout is still active */
        auto_display_on = uptime_sec() - DISPLAY_TIMEOUT - 1;
        update_display(NULL);
    }

    receive();
    check_repeatedjobs();
    expire_cache();
    if (last_state != wifi_state) {
        last_state = wifi_state;
        wifi_disp = String(_wifi_state_str[wifi_state]);
        auto_display_on = uptime_sec();
        update_display(NULL);
    }
}
