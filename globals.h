#ifndef _GLOBALS_H
#define _GLOBALS_H

#define LACROSSE2MQTT_VERSION "2026.2.1"

/* if not heltec_lora_32_v2 board... */
#ifndef WIFI_LoRa_32_V2
#ifndef OLED_SDA
// I2C OLED Display works with SSD1306 driver
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
// SPI LoRa Radio
#define LORA_SCK 5 // GPIO5 - SX1276 SCK
#define LORA_MISO 19 // GPIO19 - SX1276 MISO
#define LORA_MOSI 27 // GPIO27 - SX1276 MOSI
#define LORA_CS 18 // GPIO18 - SX1276 CS
#define LORA_RST 14 // GPIO14 - SX1276 RST
#define LORA_IRQ 26 // GPIO26 - SX1276 IRQ (interrupt request)
#define SS 5  // Standardwert für ESP32
static const uint8_t KEY_BUILTIN = 0;
#endif
#ifndef LED_BUILTIN
static const uint8_t LED_BUILTIN = 2;
#define BUILTIN_LED LED_BUILTIN // backward compatibility
#define LED_BUILTIN LED_BUILTIN
#endif
#else
/* heltec_lora_32_v2 board */
#define OLED_SDA SDA_OLED
#define OLED_SCL SCL_OLED
#define OLED_RST RST_OLED
#define LORA_CS SS
#define LORA_RST RST_LoRa
#define LORA_IRQ DIO0
#define LORA_MISO MISO
#define LORA_MOSI MOSI
#define LORA_SCK SCK
#endif

/* how many bytes is our data frame long? */
#define FRAME_LENGTH 5

/* maximum number of sensors: 64 x 2 channels x 2 datarates */
#define SENSOR_NUM 256

#define HASS_CFG_HUMI (1 << 0)
#define HASS_CFG_TEMP (1 << 1)
#define HASS_CFG_TEMP2 (1 << 2)
#define HASS_CFG_WIND_SPEED (1 << 3)      // NEU
#define HASS_CFG_WIND_DIR (1 << 4)        // NEU
#define HASS_CFG_WIND_GUST (1 << 5)       // NEU
#define HASS_CFG_RAIN (1 << 6)            // NEU
#define HASS_CFG_POWER (1 << 7)           // NEU
#define HASS_CFG_ENERGY (1 << 8)          // NEU
#define HASS_CFG_PRESSURE (1 << 9)        // NEU

#define BASE_SENSOR_TIMEOUT 300000   // 5 Minuten Basis-Timeout
#define TIMEOUT_PER_PROTOCOL 60000   // +1 Minute pro aktiviertem Protokoll

struct Cache {
    unsigned long timestamp;
    byte ID;
    byte rate;
    int rssi;
    uint8_t data[FRAME_LENGTH];
    float temp;
    byte humi;
    bool batlo;
    bool init;
    bool valid;
    byte channel;
    char sensorType[16];
    
    // Kanal 2 Daten (nur Temperatur!)
    float temp_ch2;
    unsigned long timestamp_ch2;
    
    // Wetterstation Daten (bestehend)
    float wind_speed;
    int wind_direction;
    float rain;
    float rain_total;
    byte wind_gust;
    unsigned long rain_timestamp;
    unsigned long wind_timestamp;
    
    // Erweiterte Wetterdaten für TX22IT
    float wind_speed_avg;
    float wind_direction_avg;
    float wind_gust_max;
    
    // Energie-Daten für EMT7110
    float power;          // Leistung in Watt
    float energy;         // Energie in kWh
    unsigned long power_timestamp;
    
    // Luftdruck-Daten für WH24/WH25
    float pressure;       // Luftdruck in hPa
    unsigned long pressure_timestamp;
};

// Hilfsfunktion für Cache-Index
static inline int GetCacheIndex(byte ID, byte channel) {
    return ID;
}

struct Config {
    String mqtt_server;
    int mqtt_port;
    String mqtt_user;
    String mqtt_pass;
    bool display_on;
    bool ha_discovery;
    bool debug_mode;
    bool screensaver_mode;
    bool mqtt_use_names;
    bool changed;
    bool proto_lacrosse;
    bool proto_wh1080;
    bool proto_tx38it;
    bool proto_tx35it;
    bool proto_ws1600;
    bool proto_wt440xh;
    bool proto_tx22it;
    bool proto_emt7110;
    bool proto_w136;
    bool proto_wh24;
    bool proto_wh25;
};

extern Config config;
extern Cache fcache[];
extern String id2name[SENSOR_NUM];
extern uint8_t hass_cfg[SENSOR_NUM];
extern bool littlefs_ok;
extern bool mqtt_ok;

static inline uint32_t uptime_sec() { return (esp_timer_get_time()/(int64_t)1000000); }

int get_current_datarate();
int get_interval();

#endif