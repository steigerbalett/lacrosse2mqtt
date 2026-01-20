#include "webfrontend.h"
#include "lacrosse.h"
#include "globals.h"
#include <HTTPUpdateServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <rom/rtc.h>
#include "WiFi.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Debug-Log Buffer (ringbuffer für letzte 100 Frames)
#define DEBUG_LOG_SIZE 100
struct DebugEntry {
    unsigned long timestamp;
    uint8_t data[FRAME_LENGTH];
    int8_t rssi;
    int rate;
    bool valid;
};
DebugEntry debug_log[DEBUG_LOG_SIZE];
int debug_log_index = 0;
unsigned long debug_log_counter = 0;

void add_debug_log(uint8_t *data, int8_t rssi, int datarate, bool valid) {
    if (!config.debug_mode) return;
    debug_log[debug_log_index].timestamp = millis();
    memcpy(debug_log[debug_log_index].data, data, FRAME_LENGTH);
    debug_log[debug_log_index].rssi = rssi;
    debug_log[debug_log_index].rate = datarate;
    debug_log[debug_log_index].valid = valid;
    debug_log_index = (debug_log_index + 1) % DEBUG_LOG_SIZE;
    debug_log_counter++;
}

extern uint32_t auto_display_on;
extern Adafruit_SSD1306 display; 

/* git version passed by compile.sh */
#ifndef LACROSSE2MQTT_VERSION
#define LACROSSE2MQTT_VERSION  "2026.1.2"
#endif

// 16x16 Pixel Thermometer Favicon
const uint8_t favicon_ico[] PROGMEM = {
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x10, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x28, 0x01, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x99, 0x99, 0x99, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21,
    0x00, 0x00, 0x00, 0x24, 0x20, 0x00, 0x00, 0x24, 0x42, 0x00, 0x00, 0x24,
    0x42, 0x00, 0x00, 0x24, 0x42, 0x00, 0x00, 0x24, 0x42, 0x00, 0x00, 0x14,
    0x41, 0x00, 0x00, 0x04, 0x40, 0x00, 0x00, 0x04, 0x40, 0x00, 0x00, 0x14,
    0x41, 0x00, 0x00, 0x24, 0x42, 0x00, 0x00, 0x22, 0x22, 0x00, 0x00, 0x02,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF,
    0xE7, 0x00, 0x00, 0xFF, 0xC3, 0x00, 0x00, 0xFF, 0xC3, 0x00, 0x00, 0xFF,
    0xC3, 0x00, 0x00, 0xFF, 0xC3, 0x00, 0x00, 0xFF, 0x81, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x81, 0x00, 0x00, 0xFF,
    0xC3, 0x00, 0x00, 0xFF, 0xC3, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF,
    0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00
};

static WebServer server(80);
static HTTPUpdateServer httpUpdater;

int name2id(const char *fname, const int start = 0)
{
    if (strlen(fname) - start != 2) {
        Serial.printf("INVALID idmap file name: %s\r\n", fname);
        return -1;
    }
    char *end;
    errno = 0;
    int id = strtol(fname + start, &end, 16);
    if (*end != '\0' || errno != 0 || end - fname - start != 2) {
        Serial.printf("STRTOL error, %s, errno: %d\r\n", fname, errno);
        return -1;
    }
    return id;
}

String time_string(void)
{
    uint32_t now = uptime_sec();
    char timestr[10];
    String ret = "";
    if (now >= 24*60*60)
        ret += String(now / (24*60*60)) + "d ";
    now %= 24*60*60;
    snprintf(timestr, 10, "%02lu:%02lu:%02lu", now / (60*60), (now % (60*60)) / 60, now % 60);
    ret += String(timestr);
    return ret;
}

String read_file(File &file)
{
    String ret;
    while (file.available())
        ret += String((char)file.read());
    return ret;
}

bool load_idmap()
{
    if (!littlefs_ok)
        return false;
    File idmapdir = LittleFS.open("/idmap");
    if (!idmapdir) {
        Serial.println("/idmap not found");
        return false;
    }
    if (!idmapdir.isDirectory()) {
        Serial.println("/idmap not a directory");
        idmapdir.close();
        return false;
    }
    for (int i = 0; i < SENSOR_NUM; i++)
        id2name[i] = String();
    int found = 0;
    File file = idmapdir.openNextFile();
    while (file) {
        const char *fname = file.name();
        int id = name2id(fname);
        if (id > -1) {
            Serial.printf("reading idmap file %s id:%2d ", fname, id);
            id2name[id] = read_file(file);
            Serial.println("content: " + id2name[id]);
            found++;
        }
        file.close();
        file = idmapdir.openNextFile();
    }
    idmapdir.close();
    return (found > 0);
}

bool load_config()
{
    config.display_on = true; /* default */
    config.ha_discovery = false; /* default */
    config.debug_mode = false; /* default */
    config.screensaver_mode = false; /* default - NEU */
    
    if (!littlefs_ok)
        return false;
    
    File cfg = LittleFS.open("/config.json");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, cfg);
    
    if (error) {
        Serial.println("Failed to read config.json");
        Serial.print("error code: ");
        Serial.println(error.code());
    } else {
        if (doc["mqtt_port"])
            config.mqtt_port = doc["mqtt_port"];
        if (doc["mqtt_server"]) {
            const char *tmp = doc["mqtt_server"];
            config.mqtt_server = String(tmp);
        }
        if (doc["mqtt_user"]) {
            const char *tmp = doc["mqtt_user"];
            config.mqtt_user = String(tmp);
        }
        if (doc["mqtt_pass"]) {
            const char *tmp = doc["mqtt_pass"];
            config.mqtt_pass = String(tmp);
        }
        if (!doc["display_on"].isNull())
            config.display_on = doc["display_on"];
        if (!doc["ha_discovery"].isNull())
            config.ha_discovery = doc["ha_discovery"];
        if (!doc["debug_mode"].isNull())
            config.debug_mode = doc["debug_mode"];
        if (!doc["screensaver_mode"].isNull())  // NEU
            config.screensaver_mode = doc["screensaver_mode"];
            
        Serial.println("result of config.json");
        Serial.println("mqtt_server: " + config.mqtt_server);
        Serial.println("mqtt_port: " + String(config.mqtt_port));
        Serial.println("mqtt_user: " + config.mqtt_user);
        Serial.println("ha_discovery: " + String(config.ha_discovery));
        Serial.println("display_on: " + String(config.display_on));
        Serial.println("debug_mode: " + String(config.debug_mode));
        Serial.println("screensaver_mode: " + String(config.screensaver_mode)); // NEU
    }
    
    cfg.close();
    config.changed = true;
    return !error;
}

bool save_config()
{
    bool ret = true;
    LittleFS.remove("/config.json");
    File cfg = LittleFS.open("/config.json", FILE_WRITE);
    
    if (! cfg) {
        Serial.println("Failed to open config.json for writing");
        return false;
    }
    
    JsonDocument doc;
    doc["mqtt_port"] = config.mqtt_port;
    doc["mqtt_server"] = config.mqtt_server;
    doc["mqtt_user"] = config.mqtt_user;
    doc["mqtt_pass"] = config.mqtt_pass;
    doc["display_on"] = config.display_on;
    doc["ha_discovery"] = config.ha_discovery;
    doc["debug_mode"] = config.debug_mode;
    doc["screensaver_mode"] = config.screensaver_mode;  // NEU
    
    if (serializeJson(doc, cfg) == 0) {
        Serial.println("FFailed to write config.json");
        ret = false;
    }
    
    cfg.close();
    Serial.println("---written config.json");
    cfg = LittleFS.open("/config.json");
    Serial.println(read_file(cfg));
    cfg.close();
    Serial.println("---end config.json");
    
    return ret;
}

bool save_idmap()
{
    if (!littlefs_ok)
        return false;
    File idmapdir = LittleFS.open("/idmap");
    if (!idmapdir) {
        Serial.println("SAVE: /idmapdir not found");
        if (LittleFS.mkdir("/idmap"))
            idmapdir = LittleFS.open("/idmap");
        else {
            Serial.println("SAVE: mkdir /idmap failed :-(");
            return false;
        }
    }
    if (!idmapdir.isDirectory()) {
        Serial.println("SAVE: /idmap not a directory");
        idmapdir.close();
        return false;
    }
    File file = idmapdir.openNextFile();
    while (file) {
        int id = name2id(file.name());
        String fullname = "/idmap/" + String(file.name());
        file.close();
        if (id > -1 && id2name[id].length() == 0) {
            Serial.print("removing ");
            Serial.println(fullname);
            if (!LittleFS.remove(fullname))
                Serial.println("failed?");
        }
        file = idmapdir.openNextFile();
    }
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (id2name[i].length() == 0)
            continue;
        String fullname = String("/idmap/") + String((i < 0x10)?"0":"") + String(i, HEX);
        if (LittleFS.exists(fullname)) {
            //Serial.println("Exists: " + fullname);
            File comp = LittleFS.open(fullname);
            if (comp) {
                //Serial.println("open: " + fullname);
                String tmp = read_file(comp);
                comp.close();
                //Serial.print("tmp:");Serial.print(tmp);Serial.println("'");
                //Serial.print("id2:");Serial.print(id2name[i]);Serial.println("'");
                if (tmp == id2name[i])
                    continue; /* skip unchanged settings */
            }
        }
        Serial.println("Writing file " +fullname+" content: " + id2name[i]);
        File file = LittleFS.open(fullname, FILE_WRITE);
        if (! file) {
            Serial.println("file open failed :-(");
            continue;
        }
        file.print(id2name[i]);
    }
    return true;
}

void add_current_table(String &s, bool rawdata)
{
    unsigned long now = millis();
    
    s += "<h2>Current sensor data</h2>\n";
    s += "<table>\n";
    s += "<thead><tr>";
    s += "<th>ID</th>";
    s += "<th>Ch</th>";
    s += "<th>Type</th>";
    s += "<th>Temperature</th>";
    s += "<th>Humidity</th>";
    s += "<th>RSSI</th>";
    s += "<th>Name</th>";
    s += "<th>Age (ms)</th>";
    s += "<th>Battery</th>";
    s += "<th>New Batt</th>";
    if (rawdata)
        s += "<th>Raw Frame Data</th>";
    s += "</tr></thead>\n";
    s += "<tbody>\n";

    int sensorCount = 0;
    
    for (int i = 0; i < SENSOR_NUM; i++)
    {
        // Überspringe leere Einträge
        if (fcache[i].timestamp == 0)
            continue;
        
        // Überspringe ungültige IDs
        if (fcache[i].ID == 0xFF)
            continue;
            
        sensorCount++;

        String name = id2name[fcache[i].ID];
        if (name.length() == 0)
            name = "-";

        // ID ist die Originale
        int displayID = fcache[i].ID;
        
        // Sensor-Typ aus Cache
        String sensorType = String(fcache[i].sensorType);
        if (sensorType.length() == 0)
            sensorType = "LaCrosse";

        s += "<tr>";
        
        // ID
        s += "<td>" + String(displayID) + "</td>";
        
        // Channel
        s += "<td>" + String(fcache[i].channel) + "</td>";
        
        // Type
        s += "<td>" + sensorType + "</td>";

        // Temperatur
        s += "<td>" + String(fcache[i].temp, 1) + " °C</td>";

        // Luftfeuchtigkeit
        if (fcache[i].humi > 0 && fcache[i].humi <= 100) {
            s += "<td>" + String(fcache[i].humi) + " %</td>";
        } else {
            s += "<td>-</td>";
        }

        // RSSI
        s += "<td>" + String(fcache[i].rssi) + "</td>";

        // Name
        s += "<td>" + name + "</td>";

        // Age
        unsigned long age = now - fcache[i].timestamp;
        s += "<td>" + String(age) + "</td>";

        // Battery mit Farbe
        if (fcache[i].batlo) {
            s += "<td class='batt-weak'>weak</td>";
        } else {
            s += "<td class='batt-ok'>ok</td>";
        }

        // Init
        if (fcache[i].init) {
            s += "<td class='init-new'>yes</td>";
        } else {
            s += "<td class='init-no'>no</td>";
        }

        // Raw data (optional)
        if (rawdata) {
            s += "<td class='raw-data'>0x";
            for (int j = 0; j < FRAME_LENGTH; j++) {
            char tmp[3];
            snprintf(tmp, 3, "%02X", fcache[i].data[j]);
            s += String(tmp);
        if (j < FRAME_LENGTH - 1)
            s += " ";
}
            s += "</td>";
        }

        s += "</tr>\n";
    }
    
    s += "</tbody>\n";
    s += "</table>\n";
    
    // Info-Zeile
    if (sensorCount == 0) {
        s += "<p><em>No sensors found. Waiting for data...</em></p>\n";
    } else {
        s += "<p><em>Total sensors: " + String(sensorCount) + "</em></p>\n";
    }
}

static void add_header(String &s, const String &title)
{
    s = "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        ":root { "
            "--primary-color: #03a9f4; "
            "--accent-color: #ff9800; "
            "--primary-background-color: #111111; "
            "--secondary-background-color: #1c1c1c; "
            "--card-background-color: #1c1c1c; "
            "--primary-text-color: #e1e1e1; "
            "--secondary-text-color: #9b9b9b; "
            "--disabled-text-color: #6f6f6f; "
            "--divider-color: #2f2f2f; "
            "--success-color: #4caf50; "
            "--warning-color: #ff9800; "
            "--error-color: #f44336; "
            "--info-color: #2196f3; "
        "}"
        "[data-theme='light'] { "
            "--primary-color: #1976d2; "
            "--accent-color: #f57c00; "
            "--primary-background-color: #fafafa; "
            "--secondary-background-color: #ffffff; "
            "--card-background-color: #ffffff; "
            "--primary-text-color: #212121; "
            "--secondary-text-color: #757575; "
            "--disabled-text-color: #9e9e9e; "
            "--divider-color: #e0e0e0; "
            "--success-color: #2e7d32; "
            "--warning-color: #f57c00; "
            "--error-color: #c62828; "
            "--info-color: #1976d2; "
        "}"
        "* { "
            "box-sizing: border-box; "
        "}"
        "body { "
            "font-family: 'Roboto', -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Helvetica Neue', Arial, sans-serif; "
            "margin: 0; "
            "padding: 12px; "
            "background-color: var(--primary-background-color); "
            "color: var(--primary-text-color); "
            "line-height: 1.4; "
            "transition: background-color 0.3s, color 0.3s; "
        "}"
        ".header-container { "
            "display: flex; "
            "justify-content: space-between; "
            "align-items: center; "
            "margin-bottom: 16px; "
            "padding-bottom: 8px; "
            "border-bottom: 1px solid var(--divider-color); "
        "}"
        "h1 { "
            "color: var(--primary-text-color); "
            "font-size: 28px; "
            "font-weight: 400; "
            "margin: 0; "
        "}"
        ".theme-toggle { "
            "background-color: var(--card-background-color); "
            "border: 1px solid var(--divider-color); "
            "border-radius: 24px; "
            "padding: 6px 12px; "
            "cursor: pointer; "
            "display: flex; "
            "align-items: center; "
            "gap: 6px; "
            "transition: all 0.3s; "
            "font-size: 13px; "
            "color: var(--primary-text-color); "
        "}"
        ".theme-toggle:hover { "
            "background-color: var(--secondary-background-color); "
            "border-color: var(--primary-color); "
        "}"
        ".theme-icon { "
            "font-size: 16px; "
        "}"
        "h2 { "
            "color: var(--primary-text-color); "
            "font-size: 18px; "
            "font-weight: 500; "
            "margin: 16px 0 12px 0; "
        "}"
        "h3 { "
            "color: var(--primary-text-color); "
            "font-size: 15px; "
            "font-weight: 500; "
            "margin: 16px 0 8px 0; "
        "}"
        ".card-grid { "
            "display: grid; "
            "grid-template-columns: 1fr; "
            "gap: 12px; "
            "margin: 12px 0; "
        "}"
        ".card { "
            "background-color: var(--card-background-color); "
            "border-radius: 8px; "
            "padding: 12px; "
            "margin: 0; "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.1); "
            "transition: background-color 0.3s, box-shadow 0.3s; "
        "}"
        "[data-theme='dark'] .card { "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.3); "
        "}"
        ".card-full { "
            "grid-column: 1 / -1; "
        "}"
        "table { "
            "border-collapse: collapse; "
            "width: 100%; "
            "margin: 12px 0; "
            "background-color: var(--card-background-color); "
            "border-radius: 8px; "
            "overflow: hidden; "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.1); "
            "font-size: 13px; "
            "transition: all 0.3s; "
        "}"
        "[data-theme='dark'] table { "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.3); "
        "}"
        "thead { "
            "background-color: var(--secondary-background-color); "
        "}"
        "th { "
            "padding: 10px 8px; "
            "text-align: left; "
            "font-weight: 500; "
            "color: var(--primary-text-color); "
            "text-transform: uppercase; "
            "font-size: 11px; "
            "letter-spacing: 0.5px; "
            "border-bottom: 1px solid var(--divider-color); "
        "}"
        "td { "
            "padding: 8px; "
            "border-bottom: 1px solid var(--divider-color); "
            "color: var(--primary-text-color); "
        "}"
        "tbody tr:hover { "
            "background-color: rgba(3, 169, 244, 0.08); "
        "}"
        "[data-theme='light'] tbody tr:hover { "
            "background-color: rgba(25, 118, 210, 0.08); "
        "}"
        "tbody tr:last-child td { "
            "border-bottom: none; "
        "}"
        ".batt-weak { "
            "color: var(--error-color); "
            "font-weight: 500; "
            "padding: 3px 6px; "
            "background-color: rgba(244, 67, 54, 0.15); "
            "border-radius: 4px; "
            "display: inline-block; "
        "}"
        "[data-theme='light'] .batt-weak { "
            "background-color: rgba(198, 40, 40, 0.1); "
        "}"
        ".batt-ok { "
            "color: var(--success-color); "
            "font-weight: 500; "
        "}"
        ".init-new { "
            "color: var(--info-color); "
            "font-weight: 500; "
        "}"
        ".init-no { "
            "color: var(--secondary-text-color); "
        "}"
        ".raw-data { "
            "font-family: 'Roboto Mono', 'Courier New', monospace; "
            "font-size: 11px; "
            "color: var(--primary-color); "
            "background-color: rgba(3, 169, 244, 0.08); "
            "padding: 3px 6px; "
            "border-radius: 4px; "
        "}"
        "[data-theme='light'] .raw-data { "
            "background-color: rgba(25, 118, 210, 0.08); "
        "}"
        "form { "
            "background-color: var(--card-background-color); "
            "padding: 16px; "
            "margin: 12px 0; "
            "border-radius: 8px; "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.1); "
            "transition: all 0.3s; "
        "}"
        "[data-theme='dark'] form { "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.3); "
        "}"
        "label { "
            "display: block; "
            "margin: 12px 0 6px 0; "
            "color: var(--primary-text-color); "
            "font-weight: 500; "
            "font-size: 13px; "
        "}"
        "input[type='text'], input[type='number'], input[type='password'], select, textarea { "
            "width: 100%; "
            "padding: 10px; "
            "margin: 4px 0 12px 0; "
            "border: 1px solid var(--divider-color); "
            "border-radius: 4px; "
            "background-color: var(--secondary-background-color) !important; "
            "color: var(--primary-text-color) !important; "
            "font-size: 13px; "
            "font-family: inherit; "
            "transition: all 0.3s; "
            "-webkit-appearance: none; "
            "-moz-appearance: none; "
            "appearance: none; "
        "}"
        "input[type='text']:focus, input[type='number']:focus, input[type='password']:focus, select:focus, textarea:focus { "
            "outline: none; "
            "border-color: var(--primary-color); "
            "background-color: var(--card-background-color) !important; "
        "}"
        "input[type='text']::placeholder, input[type='number']::placeholder, input[type='password']::placeholder, textarea::placeholder { "
            "color: var(--disabled-text-color); "
            "opacity: 0.7; "
        "}"
        "input:-webkit-autofill, "
        "input:-webkit-autofill:hover, "
        "input:-webkit-autofill:focus, "
        "input:-webkit-autofill:active { "
            "-webkit-box-shadow: 0 0 0 1000px var(--secondary-background-color) inset !important; "
            "-webkit-text-fill-color: var(--primary-text-color) !important; "
            "transition: background-color 5000s ease-in-out 0s; "
            "caret-color: var(--primary-text-color); "
        "}"
        "[data-theme='light'] input:-webkit-autofill, "
        "[data-theme='light'] input:-webkit-autofill:hover, "
        "[data-theme='light'] input:-webkit-autofill:focus, "
        "[data-theme='light'] input:-webkit-autofill:active { "
            "-webkit-box-shadow: 0 0 0 1000px var(--secondary-background-color) inset !important; "
            "-webkit-text-fill-color: var(--primary-text-color) !important; "
        "}"
        "input[type='submit'], input[type='button'], button { "
            "background-color: var(--primary-color); "
            "color: white; "
            "padding: 10px 20px; "
            "margin: 6px 6px 0 0; "
            "border: none; "
            "border-radius: 4px; "
            "cursor: pointer; "
            "font-size: 13px; "
            "font-weight: 500; "
            "text-transform: uppercase; "
            "letter-spacing: 0.5px; "
            "transition: background-color 0.2s; "
        "}"
        "input[type='submit']:hover, input[type='button']:hover, button:hover { "
            "background-color: #0288d1; "
        "}"
        "[data-theme='light'] input[type='submit']:hover, "
        "[data-theme='light'] input[type='button']:hover, "
        "[data-theme='light'] button:hover { "
            "background-color: #1565c0; "
        "}"
        "input[type='submit']:active, input[type='button']:active, button:active { "
            "background-color: #01579b; "
        "}"
        "input[type='radio'] { "
            "appearance: none; "
            "-webkit-appearance: none; "
            "-moz-appearance: none; "
            "width: 18px; "
            "height: 18px; "
            "border: 2px solid var(--divider-color); "
            "border-radius: 50%; "
            "margin: 0 6px 0 0; "
            "cursor: pointer; "
            "position: relative; "
            "transition: all 0.2s; "
            "vertical-align: middle; "
            "background-color: var(--secondary-background-color); "
        "}"
        "input[type='radio']:hover { "
            "border-color: var(--primary-color); "
        "}"
        "input[type='radio']:checked { "
            "border-color: var(--primary-color); "
            "background-color: var(--primary-color); "
        "}"
        "input[type='radio']:checked::after { "
            "content: ''; "
            "position: absolute; "
            "top: 50%; "
            "left: 50%; "
            "transform: translate(-50%, -50%); "
            "width: 7px; "
            "height: 7px; "
            "border-radius: 50%; "
            "background-color: white; "
        "}"
        "input[type='checkbox'] { "
            "appearance: none; "
            "-webkit-appearance: none; "
            "-moz-appearance: none; "
            "width: 18px; "
            "height: 18px; "
            "border: 2px solid var(--divider-color); "
            "border-radius: 4px; "
            "margin: 0 6px 0 0; "
            "cursor: pointer; "
            "position: relative; "
            "transition: all 0.2s; "
            "vertical-align: middle; "
            "background-color: var(--secondary-background-color); "
        "}"
        "input[type='checkbox']:hover { "
            "border-color: var(--primary-color); "
        "}"
        "input[type='checkbox']:checked { "
            "border-color: var(--primary-color); "
            "background-color: var(--primary-color); "
        "}"
        "input[type='checkbox']:checked::after { "
            "content: '✓'; "
            "position: absolute; "
            "top: 50%; "
            "left: 50%; "
            "transform: translate(-50%, -50%); "
            "color: white; "
            "font-size: 13px; "
            "font-weight: bold; "
        "}"
        ".radio-group { "
            "display: flex; "
            "flex-direction: column; "
            "gap: 8px; "
            "margin: 12px 0; "
            "padding: 12px; "
            "background-color: var(--secondary-background-color); "
            "border-radius: 8px; "
            "border: 1px solid var(--divider-color); "
        "}"
        ".radio-item { "
            "display: flex; "
            "align-items: center; "
            "padding: 6px; "
            "border-radius: 4px; "
            "cursor: pointer; "
            "transition: background-color 0.2s; "
        "}"
        ".radio-item:hover { "
            "background-color: rgba(3, 169, 244, 0.08); "
        "}"
        "[data-theme='light'] .radio-item:hover { "
            "background-color: rgba(25, 118, 210, 0.08); "
        "}"
        ".radio-item label { "
            "margin: 0; "
            "font-weight: 400; "
            "cursor: pointer; "
            "flex: 1; "
            "display: flex; "
            "align-items: center; "
            "font-size: 13px; "
        "}"
        ".radio-item input { "
            "margin-right: 10px; "
        "}"
        ".option-description { "
            "color: var(--secondary-text-color); "
            "font-size: 11px; "
            "margin-left: 28px; "
            "margin-top: 3px; "
        "}"
        ".action-button { "
            "display: inline-block; "
            "padding: 10px 18px; "
            "margin: 3px; "
            "background-color: var(--primary-color); "
            "color: white; "
            "border-radius: 4px; "
            "text-decoration: none; "
            "font-weight: 500; "
            "font-size: 13px; "
            "text-transform: uppercase; "
            "letter-spacing: 0.5px; "
            "transition: all 0.2s; "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.2); "
        "}"
        ".action-button:hover { "
            "background-color: #0288d1; "
            "transform: translateY(-2px); "
            "box-shadow: 0 4px 8px rgba(0,0,0,0.3); "
            "text-decoration: none; "
            "color: white; "
        "}"
        "[data-theme='light'] .action-button:hover { "
            "background-color: #1565c0; "
        "}"
        ".action-button-warning { "
            "background-color: var(--warning-color); "
        "}"
        ".action-button-warning:hover { "
            "background-color: #e65100; "
        "}"
        ".action-buttons { "
            "display: flex; "
            "flex-wrap: wrap; "
            "gap: 10px; "
            "margin: 12px 0; "
        "}"
        "p { "
            "color: var(--primary-text-color); "
            "margin: 8px 0; "
        "}"
        "em { "
            "color: var(--secondary-text-color); "
            "font-style: normal; "
        "}"
        "a { "
            "color: var(--primary-color); "
            "text-decoration: none; "
            "transition: color 0.2s; "
        "}"
        "a:hover { "
            "color: var(--accent-color); "
            "text-decoration: underline; "
        "}"
        "hr { "
            "border: none; "
            "border-top: 1px solid var(--divider-color); "
            "margin: 24px 0; "
        "}"
        ".footer { "
            "margin-top: 32px; "
            "padding-top: 16px; "
            "border-top: 1px solid var(--divider-color); "
            "color: var(--secondary-text-color); "
            "font-size: 12px; "
            "text-align: center; "
        "}"
        ".status-badge { "
            "display: inline-block; "
            "padding: 3px 10px; "
            "border-radius: 12px; "
            "font-size: 11px; "
            "font-weight: 500; "
            "text-transform: uppercase; "
        "}"
        ".status-ok { "
            "background-color: rgba(76, 175, 80, 0.15); "
            "color: var(--success-color); "
        "}"
        "[data-theme='light'] .status-ok { "
            "background-color: rgba(46, 125, 50, 0.1); "
        "}"
        ".status-error { "
            "background-color: rgba(244, 67, 54, 0.15); "
            "color: var(--error-color); "
        "}"
        "[data-theme='light'] .status-error { "
            "background-color: rgba(198, 40, 40, 0.1); "
        "}"
        ".status-warning { "
            "background-color: rgba(255, 152, 0, 0.15); "
            "color: var(--warning-color); "
        "}"
        "[data-theme='light'] .status-warning { "
            "background-color: rgba(245, 124, 0, 0.1); "
        "}"
        ".info-text { "
            "color: var(--secondary-text-color); "
            "font-size: 12px; "
            "margin: 6px 0; "
        "}"
        "@media (min-width: 768px) { "
            ".card-grid { "
                "grid-template-columns: repeat(2, 1fr); "
            "}"
        "}"
        "@media (min-width: 1200px) { "
            ".card-grid { "
                "grid-template-columns: repeat(3, 1fr); "
            "}"
        "}"
        "@media (min-width: 1600px) { "
            ".card-grid { "
                "grid-template-columns: repeat(4, 1fr); "
            "}"
        "}"
        "@media (max-width: 768px) { "
            "body { padding: 8px; } "
            "table { font-size: 11px; } "
            "th, td { padding: 6px 4px; } "
            "h1 { font-size: 22px; } "
            ".card, form { padding: 10px; } "
            ".header-container { flex-direction: column; align-items: flex-start; gap: 8px; } "
        "}"
        "</style>"
        "<title>" + title + "</title></head>"
        "<body>"
        "<div class='header-container'>"
        "<h1>🌡️ " + title + "</h1>"
        "<div class='theme-toggle' onclick='toggleTheme()'>"
            "<span class='theme-icon' id='theme-icon'>🌙</span>"
            "<span id='theme-text'>Dark Mode</span>"
        "</div>"
        "</div>"
        "<script>"
        "function toggleTheme() {"
            "const body = document.body;"
            "const icon = document.getElementById('theme-icon');"
            "const text = document.getElementById('theme-text');"
            "const currentTheme = body.getAttribute('data-theme');"
            "if (currentTheme === 'light') {"
                "body.setAttribute('data-theme', 'dark');"
                "icon.textContent = '🌙';"
                "text.textContent = 'Dark Mode';"
                "localStorage.setItem('theme', 'dark');"
            "} else {"
                "body.setAttribute('data-theme', 'light');"
                "icon.textContent = '☀️';"
                "text.textContent = 'Light Mode';"
                "localStorage.setItem('theme', 'light');"
            "}"
        "}"
        "window.addEventListener('DOMContentLoaded', (event) => {"
            "const savedTheme = localStorage.getItem('theme') || 'dark';"
            "const icon = document.getElementById('theme-icon');"
            "const text = document.getElementById('theme-text');"
            "document.body.setAttribute('data-theme', savedTheme);"
            "if (savedTheme === 'light') {"
                "icon.textContent = '☀️';"
                "text.textContent = 'Light Mode';"
            "} else {"
                "icon.textContent = '🌙';"
                "text.textContent = 'Dark Mode';"
            "}"
        "});"
        "</script>";
}

/* from tasmota */
String ESP32GetResetReason(uint32_t cpu_no) {
    // tools\sdk\include\esp32\rom\rtc.h
    switch (rtc_get_reset_reason(cpu_no)) {
        case POWERON_RESET:         return F("Vbat power on reset");
        case SW_RESET:              return F("Software reset digital core");
        case OWDT_RESET:            return F("Legacy watch dog reset digital core");
        case DEEPSLEEP_RESET:       return F("Deep Sleep reset digital core");
        case SDIO_RESET:            return F("Reset by SLC module, reset digital core");
        case TG0WDT_SYS_RESET:      return F("Timer Group0 Watch dog reset digital core");
        case TG1WDT_SYS_RESET:      return F("Timer Group1 Watch dog reset digital core");
        case RTCWDT_SYS_RESET:      return F("RTC Watch dog Reset digital core");
        case INTRUSION_RESET:       return F("Instrusion tested to reset CPU");
        case TGWDT_CPU_RESET:       return F("Time Group reset CPU");
        case SW_CPU_RESET:          return F("Software reset CPU");
        case RTCWDT_CPU_RESET:      return F("RTC Watch dog Reset CPU");
        case EXT_CPU_RESET:         return F("for APP CPU, reseted by PRO CPU");
        case RTCWDT_BROWN_OUT_RESET:return F("Reset when the vdd voltage is not stable");
        case RTCWDT_RTC_RESET:      return F("RTC Watch dog reset digital core and rtc module");
        /* esp32-cX?
        case 17 : return F("Time Group1 reset CPU");                            // 17  -                 TG1WDT_CPU_RESET
        case 18 : return F("Super watchdog reset digital core and rtc module"); // 18  -                 SUPER_WDT_RESET
        case 19 : return F("Glitch reset digital core and rtc module");         // 19  -                 GLITCH_RTC_RESET
        case 20 : return F("Efuse reset digital core");                         // 20                    EFUSE_RESET
        case 21 : return F("Usb uart reset digital core");                      // 21                    USB_UART_CHIP_RESET
        case 22 : return F("Usb jtag reset digital core");                      // 22                    USB_JTAG_CHIP_RESET
        case 23 : return F("Power glitch reset digital core and rtc module");   // 23                    POWER_GLITCH_RESET
         */
        default: break;
    }
    return F("No meaning"); // 0 and undefined
}

static void add_sysinfo_footer(String &s)
{
    s += "<div class='footer'>"
         "<p>Powered by LaCrosse2MQTT | "
         "<a href='/'>🏠 Home</a> | "
         "<a href='/config.html'>⚙️ Configuration</a> | "
         "<a href='/update'>📦 Update</a>"
         "</p></div>"
         "</body></html>";
}

void handle_index()
{
    String index;
    add_header(index, "LaCrosse2MQTT Gateway");
    
    index += "<div class='card-grid'>";
    
    index += "<div class='card'>";
    index += "<h2>System Status</h2>";
    index += "<p>";
    if (mqtt_ok) {
        index += "<span class='status-badge status-ok'>✓ MQTT Connected</span> ";
    } else {
        index += "<span class='status-badge status-error'>✗ MQTT Disconnected</span> ";
    }
    if (WiFi.status() == WL_CONNECTED) {
        index += "<span class='status-badge status-ok'>✓ WiFi Connected</span>";
    } else {
        index += "<span class='status-badge status-error'>✗ WiFi Disconnected</span>";
    }
    index += "</p>";
    index += "<p class='info-text'>SSID: " + WiFi.SSID() + "</p>";
    index += "<p class='info-text'>IP: " + WiFi.localIP().toString() + "</p>";
    index += "<p class='info-text'>Uptime: " + time_string() + "</p>";
    index += "<p class='info-text'>Software: " + String(LACROSSE2MQTT_VERSION) + "</p>";
    index += "<p class='info-text'>Built: " + String(__DATE__) + " " + String(__TIME__) + "</p>";
    index += "<p class='info-text'>Reset reason: " + ESP32GetResetReason(0) + "</p>";
    index += "</div>";
    
    index += "<div class='card'>";
    index += "<h2>Quick Actions</h2>";
    index += "<div class='action-buttons'>";
    index += "<a href='/config.html' class='action-button'>⚙️ Configuration</a>";
    index += "<a href='/update' class='action-button'>📦 Update Firmware</a>";
    if (config.debug_mode) {
        index += "<a href='/debug.html' class='action-button action-button-warning'>🐛 Debug Log</a>";
    }
    index += "</div>";
    index += "</div>";
    
    index += "</div>";
    
    index += "<div class='card card-full'>";
    add_current_table(index, true);
    index += "</div>";
    
    add_sysinfo_footer(index);
    server.send(200, "text/html", index);
}

const String on = "on";
const String off = "off";
const String checked = " checked=\"checked\"";
static bool config_changed = false;

void handle_config() {
    static unsigned long token = millis();
    if (server.hasArg("id") && server.hasArg("name")) {
        String _id = server.arg("id");
        String name = server.arg("name");
        name.trim();
        if (_id[0] >= '0' && _id[0] <= '9') {
            int id = _id.toInt();
            if (id >= 0 && id < SENSOR_NUM) {
                id2name[id] = name;
                config_changed = true;
            }
        }
    }
    if (server.hasArg("mqtt_server")) {
        config.mqtt_server = server.arg("mqtt_server");
        config.changed = true;
        config_changed = true;
    }
    if (server.hasArg("mqtt_port")) {
        String _port = server.arg("mqtt_port");
        config.mqtt_port = _port.toInt();
        config.changed = true;
        config_changed = true;
    }
    if (server.hasArg("mqtt_user")) {
        config.mqtt_user = server.arg("mqtt_user");
        config.changed = true;
        config_changed = true;
    }
    if (server.hasArg("mqtt_pass")) {
        config.mqtt_pass = server.arg("mqtt_pass");
        config.changed = true;
        config_changed = true;
    }
    if (server.hasArg("save")) {
        if (server.arg("save") == String(token)) {
            Serial.println("SAVE!");
            save_idmap();
            save_config();
            config_changed = false;
        }
    }
    if (server.hasArg("debug_mode")) {
        String _on = server.arg("debug_mode");
        int tmp = _on.toInt();
        if (tmp != config.debug_mode) {
            config_changed = true;
            config.debug_mode = tmp;
            Serial.println("Debug mode changed to: " + String(config.debug_mode));
        }
    }
    if (server.hasArg("screensaver_mode")) {
        String _on = server.arg("screensaver_mode");
        int tmp = _on.toInt();
        if (tmp != config.screensaver_mode) {
            config_changed = true;
            config.screensaver_mode = tmp;
            Serial.println("Screensaver mode changed to: " + String(config.screensaver_mode));
        }
    }
    if (server.hasArg("cancel")) {
        if (server.arg("cancel") == String(token)) {
            load_idmap();
            load_config();
            config_changed = false;
        }
    }
    if (server.hasArg("format")) {
        if (server.arg("format") == String(token)) {
            LittleFS.begin(true);
            ESP.restart();
            while (true)
                delay(100);
        }
    }
    if (server.hasArg("display")) {
        String _on = server.arg("display");
        int tmp = _on.toInt();
        if (tmp != config.display_on) {
            config_changed = true;
            config.display_on = tmp;
        
            if (!config.display_on) {
                display.ssd1306_command(SSD1306_DISPLAYOFF);
            } else {
                display.ssd1306_command(SSD1306_DISPLAYON);
                auto_display_on = uptime_sec();
            }
        }
    }
    if (server.hasArg("ha_disc")) {
        String _on = server.arg("ha_disc");
        int tmp = _on.toInt();
        if (tmp != config.ha_discovery)
            config_changed = true;
        config.ha_discovery = tmp;
    }
    
    String resp;
    add_header(resp, "LaCrosse2MQTT Configuration");
    
    resp += "<div class='card-grid'>";
    
    resp += "<div class='card'>";
    resp += "<h2>System Status</h2>";
    resp += "<p>";
    if (mqtt_ok) {
        resp += "<span class='status-badge status-ok'>✓ MQTT Connected</span> ";
    } else {
        resp += "<span class='status-badge status-error'>✗ MQTT Disconnected</span> ";
    }
    if (WiFi.status() == WL_CONNECTED) {
        resp += "<span class='status-badge status-ok'>✓ WiFi Connected</span>";
    } else {
        resp += "<span class='status-badge status-error'>✗ WiFi Disconnected</span>";
    }
    resp += "</p>";
    resp += "<p class='info-text'>SSID: " + WiFi.SSID() + "</p>";
    resp += "<p class='info-text'>IP: " + WiFi.localIP().toString() + "</p>";
    resp += "<p class='info-text'>Uptime: " + time_string() + "</p>";
    resp += "<p class='info-text'>Software: " + String(LACROSSE2MQTT_VERSION) + "</p>";
    resp += "<p class='info-text'>Built: " + String(__DATE__) + " " + String(__TIME__) + "</p>";
    resp += "<p class='info-text'>Reset reason: " + ESP32GetResetReason(0) + "</p>";
    resp += "</div>";
    
    resp += "<div class='card'>";
    resp += "<h2>Quick Actions</h2>";
    resp += "<div class='action-buttons'>";
    resp += "<a href='/update' class='action-button'>📦 Update Firmware</a>";
    if (config.debug_mode) {
        resp += "<a href='/debug.html' class='action-button action-button-warning'>🐛 Debug Log</a>";
    }
    resp += "<a href='/' class='action-button'>🏠 Main Page</a>";
    resp += "</div>";
    resp += "</div>";
    
    resp += "</div>";
    
    resp += "<div class='card card-full'>";
    add_current_table(resp, true);
    resp += "</div>";
    
    token = millis();
    
    resp += "<div class='card-grid'>";
    
    resp += "<div class='card'>";
    resp += "<h2>Sensor Configuration</h2>";
    resp += "<form action='/config.html'>";
    resp += "<label>ID (0-255):</label>";
    resp += "<input type='number' name='id' min='0' max='255' placeholder='Enter sensor ID'>";
    resp += "<label>Name:</label>";
    resp += "<input type='text' name='name' placeholder='Enter friendly name'>";
    resp += "<button type='submit'>Add/Update Sensor Name</button>";
    resp += "</form>";
    resp += "</div>";
    
    resp += "<div class='card'>";
    resp += "<h2>MQTT Server Configuration</h2>";
    resp += "<form action='/config.html'>";
    resp += "<label>FQDN / IP Address:</label>";
    resp += "<input type='text' name='mqtt_server' value='" + config.mqtt_server + "' placeholder='mqtt.example.com'>";
    resp += "<label>Port:</label>";
    resp += "<input type='number' name='mqtt_port' value='" + String(config.mqtt_port) + "' placeholder='1883'>";
    resp += "<label>Username (optional):</label>";
    resp += "<input type='text' name='mqtt_user' value='" + config.mqtt_user + "' placeholder='Leave empty to disable authentication'>";
    resp += "<label>Password:</label>";
    resp += "<input type='password' name='mqtt_pass' value='" + config.mqtt_pass + "' placeholder='Enter password'>";
    resp += "<button type='submit'>Update MQTT Settings</button>";
    resp += "</form>";
    resp += "</div>";
    
    if (config_changed) {
        resp += "<div class='card' style='background-color: rgba(255, 152, 0, 0.1); border: 1px solid var(--warning-color);'>";
        resp += "<h3>⚠️ Unsaved Changes</h3>";
        resp += "<p>You have unsaved configuration changes. Please save or reload to discard.</p>";
        resp += "<form action='/config.html' style='display: inline; margin-right: 8px;'>";
        resp += "<input type='hidden' name='save' value='" + String(token) + "'>";
        resp += "<button type='submit' style='background-color: var(--success-color);'>💾 Save Configuration</button>";
        resp += "</form>";
        resp += "<form action='/config.html' style='display: inline;'>";
        resp += "<input type='hidden' name='cancel' value='" + String(token) + "'>";
        resp += "<button type='submit' style='background-color: var(--error-color);'>🔄 Discard Changes</button>";
        resp += "</form>";
        resp += "</div>";
    }
    
    if (!littlefs_ok) {
        resp += "<div class='card' style='background-color: rgba(244, 67, 54, 0.1); border: 1px solid var(--error-color);'>";
        resp += "<h3>❌ Filesystem Error</h3>";
        resp += "<p><strong>LittleFS seems damaged. Saving will not work.</strong></p>";
        resp += "<p>This will erase all saved configuration. Continue?</p>";
        resp += "<form action='/config.html'>";
        resp += "<input type='hidden' name='format' value='" + String(token) + "'>";
        resp += "<button type='submit' style='background-color: var(--error-color);'>⚠️ Format Filesystem</button>";
        resp += "</form>";
        resp += "</div>";
    }
    
    resp += "<div class='card'>";
    resp += "<h2>Display Settings</h2>";
    resp += "<form action='/config.html'>";
    resp += "<div class='radio-group'>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='display' value='1'" + (config.display_on ? checked : "") + "/>";
    resp += "      Always On";
    resp += "    </label>";
    resp += "  </div>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='display' value='0'" + (!config.display_on ? checked : "") + "/>";
    resp += "      Auto-Off (5 min timeout)";
    resp += "    </label>";
    resp += "  </div>";
    resp += "</div>";
    resp += "<button type='submit'>Update Display</button>";
    resp += "</form>";
    resp += "</div>";
    
    resp += "<div class='card'>";
    resp += "<h2>Home Assistant</h2>";
    resp += "<form action='/config.html'>";
    resp += "<div class='radio-group'>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='ha_disc' value='1'" + (config.ha_discovery ? checked : "") + "/>";
    resp += "      Enable Auto-Discovery";
    resp += "    </label>";
    resp += "    <div class='option-description'>Automatically register sensors in Home Assistant via MQTT discovery</div>";
    resp += "  </div>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='ha_disc' value='0'" + (!config.ha_discovery ? checked : "") + "/>";
    resp += "      Disable";
    resp += "    </label>";
    resp += "  </div>";
    resp += "</div>";
    resp += "<button type='submit'>Update Home Assistant</button>";
    resp += "</form>";
    resp += "</div>";
    
    resp += "<div class='card'>";
    resp += "<h2>Debug Settings</h2>";
    resp += "<form action='/config.html'>";
    resp += "<div class='radio-group'>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='debug_mode' value='1'" + (config.debug_mode ? checked : "") + "/>";
    resp += "      Enable Debug Mode";
    resp += "    </label>";
    resp += "    <div class='option-description'>Show RAW frame data in serial console for troubleshooting</div>";
    resp += "  </div>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='debug_mode' value='0'" + (!config.debug_mode ? checked : "") + "/>";
    resp += "      Disable";
    resp += "    </label>";
    resp += "  </div>";
    resp += "</div>";
    resp += "<button type='submit'>Update Debug Mode</button>";
    resp += "</form>";
    resp += "</div>";
    
    resp += "<div class='card'>";
    resp += "<h2>Screensaver Settings</h2>";
    resp += "<form action='/config.html'>";
    resp += "<div class='radio-group'>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='screensaver_mode' value='1'" + (config.screensaver_mode ? checked : "") + "/>";
    resp += "      Enable Screensaver";
    resp += "    </label>";
    resp += "    <div class='option-description'>Show starfield animation after 5 minutes of inactivity</div>";
    resp += "  </div>";
    resp += "  <div class='radio-item'>";
    resp += "    <label>";
    resp += "      <input type='radio' name='screensaver_mode' value='0'" + (!config.screensaver_mode ? checked : "") + "/>";
    resp += "      Disable";
    resp += "    </label>";
    resp += "  </div>";
    resp += "</div>";
    resp += "<button type='submit'>Update Screensaver</button>";
    resp += "</form>";
    resp += "</div>";
    
    resp += "</div>";
    
    add_sysinfo_footer(resp);
    server.send(200, "text/html", resp);
}

void handle_debug() {
    String resp;
    add_header(resp, "LaCrosse2mqtt Debug Log");
    
    resp += "<p>Debug Mode: <b>" + String(config.debug_mode ? "ENABLED" : "DISABLED") + "</b></p>\n";
    resp += "<p>Total Frames Received: <b>" + String(debug_log_counter) + "</b></p>\n";
    resp += "<p><a href=\"/debug.html\">Refresh</a> | <a href=\"/config.html\">Configuration</a> | <a href=\"/\">Main page</a></p>\n";
    
    // Auto-Refresh alle 5 Sekunden wenn Debug aktiv
    if (config.debug_mode) {
        resp += "<meta http-equiv=\"refresh\" content=\"5\">\n";
    }
    
    resp += "<table class=\"sensors\">\n";
    resp += "<thead><tr>"
            "<th>#</th>"
            "<th>Time (ms)</th>"
            "<th>Raw Frame Data</th>"
            "<th>RSSI</th>"
            "<th>Rate</th>"
            "<th>Valid</th>"
            "</tr></thead>\n<tbody>\n";
    
    // Zeige neueste Einträge zuerst (rückwärts durch Ringbuffer)
    int count = 0;
    int max_display = (debug_log_counter < DEBUG_LOG_SIZE) ? debug_log_counter : DEBUG_LOG_SIZE;
    
    for (int i = 0; i < max_display; i++) {
        int idx = (debug_log_index - 1 - i + DEBUG_LOG_SIZE) % DEBUG_LOG_SIZE;
        if (debug_log[idx].timestamp == 0) continue;
        
        unsigned long age = millis() - debug_log[idx].timestamp;
        
        resp += "<tr>";
        resp += "<td>" + String(debug_log_counter - i) + "</td>";
        resp += "<td>" + String(age) + "</td>";
        
        // Raw Data
        resp += "<td class=\"rawdata\">0x";
        for (int j = 0; j < FRAME_LENGTH; j++) {
            char tmp[3];
            snprintf(tmp, 3, "%02X", debug_log[idx].data[j]);
            resp += String(tmp);
            if (j < FRAME_LENGTH - 1) resp += " ";
        }
        resp += "</td>";
        
        resp += "<td>" + String(debug_log[idx].rssi) + "</td>";
        resp += "<td>" + String(debug_log[idx].rate) + "</td>";
        resp += "<td>" + String(debug_log[idx].valid ? "✓" : "✗") + "</td>";
        resp += "</tr>\n";
        
        count++;
        if (count >= 50) break; // Maximal 50 Einträge anzeigen
    }
    
    resp += "</tbody></table>\n";
    add_sysinfo_footer(resp);
    resp += "</body></html>\n";
    server.send(200, "text/html", resp);
}

void setup_web()
{
    if (!load_idmap())
        Serial.println("setup_web ERROR: load_idmap() failed?");
    if (!load_config())
        Serial.println("setup_web ERROR: load_config() failed?");
    
    server.on("/", handle_index);
    server.on("/index.html", handle_index);
    server.on("/config.html", handle_config);
    server.on("/debug.html", handle_debug);  // NEU
    
    server.on("/favicon.ico", HTTP_GET, []() {
        server.send_P(200, "image/x-icon", (const char*)favicon_ico, sizeof(favicon_ico));
    });

    server.onNotFound([](){
        server.send(404, "text/plain", "The content you are looking for was not found.\n");
        Serial.println("404: " + server.uri());
    });
    httpUpdater.setup(&server);
    server.begin();
}

void handle_client()
{
    server.handleClient();
}