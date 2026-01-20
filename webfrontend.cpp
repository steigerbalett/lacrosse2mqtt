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
#define LACROSSE2MQTT_VERSION  "2026.1"
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
                snprintf(tmp, 3, "%02X", fcache[j].data[j]);
                s += String(tmp);
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


void add_header(String &s, String title)
{
    s = "<!DOCTYPE HTML><html><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"description\" content=\"lacrosse sensors to mqtt converter\">"
        "<link rel=\"icon\" href=\"/favicon.ico\" type=\"image/x-icon\">"
        "<style>"
        "body { "
            "font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif; "
            "margin: 0; "
            "padding: 20px; "
            "background-color: #f5f5f5; "
        "}"
        "h1, h2, h3 { "
            "color: #333; "
        "}"
        "h2 { "
            "margin-top: 30px; "
            "border-bottom: 2px solid #4CAF50; "
            "padding-bottom: 10px; "
        "}"
        "table { "
            "border-collapse: collapse; "
            "width: 100%; "
            "margin: 20px 0; "
            "background-color: white; "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.1); "
            "font-size: 14px; "
        "}"
        "thead { "
            "background-color: #4CAF50; "
            "color: white; "
        "}"
        "th { "
            "padding: 12px 8px; "
            "text-align: left; "
            "font-weight: 600; "
            "text-transform: uppercase; "
            "font-size: 12px; "
            "letter-spacing: 0.5px; "
        "}"
        "td { "
            "padding: 10px 8px; "
            "border-bottom: 1px solid #ddd; "
        "}"
        "tbody tr:hover { "
            "background-color: #f0f8ff; "
        "}"
        "tbody tr:nth-child(even) { "
            "background-color: #f9f9f9; "
        "}"
        ".batt-weak { "
            "color: #d32f2f; "
            "font-weight: bold; "
            "background-color: #ffebee; "
        "}"
        ".batt-ok { "
            "color: #388e3c; "
            "font-weight: bold; "
        "}"
        ".init-new { "
            "color: #1976d2; "
            "font-weight: bold; "
        "}"
        ".init-no { "
            "color: #757575; "
        "}"
        ".raw-data { "
            "font-family: 'Courier New', monospace; "
            "font-size: 12px; "
            "background-color: #f5f5f5; "
        "}"
        "form { "
            "background-color: white; "
            "padding: 20px; "
            "margin: 20px 0; "
            "border-radius: 4px; "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.1); "
        "}"
        "input[type='text'], input[type='number'] { "
            "width: 100%; "
            "padding: 8px; "
            "margin: 5px 0; "
            "border: 1px solid #ddd; "
            "border-radius: 4px; "
            "box-sizing: border-box; "
        "}"
        "input[type='submit'], input[type='button'] { "
            "background-color: #4CAF50; "
            "color: white; "
            "padding: 10px 20px; "
            "margin: 10px 5px 0 0; "
            "border: none; "
            "border-radius: 4px; "
            "cursor: pointer; "
            "font-size: 14px; "
        "}"
        "input[type='submit']:hover, input[type='button']:hover { "
            "background-color: #45a049; "
        "}"
        "input[type='radio'] { "
            "margin: 0 5px 0 15px; "
        "}"
        "label { "
            "margin-right: 15px; "
        "}"
        "p { "
            "color: #666; "
        "}"
        "em { "
            "color: #888; "
        "}"
        "a { "
            "color: #4CAF50; "
            "text-decoration: none; "
        "}"
        "a:hover { "
            "text-decoration: underline; "
        "}"
        "@media (max-width: 768px) { "
            "table { font-size: 12px; } "
            "th, td { padding: 8px 4px; } "
        "}"
        "</style>"
        "<title>" + title + "</title></head><body>"
        "<h1>" + title + "</h1>";
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

void add_sysinfo_footer(String &s)
{
    s += "<p>"
        "System information: Uptime " + time_string() + "<br>"
        "Software version: " + LACROSSE2MQTT_VERSION + "<br>"
        "Built: " + __DATE__ + " " + __TIME__ + "<br>"
        "Reset reason: " + ESP32GetResetReason(0) +
        "</p>\n";
}

void handle_index() {
    // TODO: use server.hostHeader()?
    String IP = WiFi.localIP().toString();
    String index;
    add_header(index, "LaCrosse2mqtt");
    add_current_table(index, false);
    index += "<p><a href=\"/config.html\">Configuration page</a></p>\n";
    add_sysinfo_footer(index);
    index += "</body></html>\n";
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
        name.trim(); /* no leading / trailing whitespace to avoid strange surprises */
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
    // NEU: Screensaver Mode Handler
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
#if 0
            ESP.restart();
            while (true)
                delay(100);
#endif
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
    add_header(resp, "LaCrosse2mqtt Configuration");
    add_current_table(resp, true);
    token = millis();
    resp += "<p>\n"
        "<form action=\"/config.html\">\n"
        "<table>\n"
            " <tr>\n"
                "<td>ID (0-255):</td><td><input type=\"number\" name=\"id\" min=\"0\" max=\"255\"></td>"
                "<td>Name:</td><td><input name=\"name\" value=\"\"></td>"
                "<td><button type=\"submit\">Submit</button></td>\n"
            "</tr>\n"
        "</table>\n"
        "</form>\n"
        "<p></p>\n"
        "MQTT server configuration (Status: connection ";
    if (!mqtt_ok)
        resp += "NOT ";
    resp += "ok)\n"
        "<form action=\"/config.html\">\n"
        "<table>\n"
            "<tr>\n"
                "<td>FQDN / IP address:</td><td><input name=\"mqtt_server\" value=\"" + config.mqtt_server + "\"></td>"
                "<td>Port:</td><td><input type=\"number\" name=\"mqtt_port\" value=\"" + String(config.mqtt_port) + "\"></td>"
            "</tr>\n"
            "<tr>\n"
                "<td>Username (empty to disable):</td><td><input name=\"mqtt_user\" value=\"" + config.mqtt_user + "\"></td>"
                "<td>Password:</td><td><input  name=\"mqtt_pass\" value=\"" + String(config.mqtt_pass) + "\"></td>"
                "<td><button type=\"submit\">Submit</button></td>\n"
            "</tr>\n"
        "</table>\n"
        "</form>\n";
    if (config_changed) {
        resp += "<p></p>\nConfig changed, please save or reload old config.\n"
            "<table>\n<tr>\n<td>"
            "<form action=\"/config.html\">"
            "<input type=\"hidden\" name=\"save\" value=\"" + String(token) + "\"><button type=\"submit\">Save</button>"
            "</form></td>\n<td>"
            "<form action=\"/config.html\">"
            "<input type=\"hidden\" name=\"cancel\" value=\"" + String(token) + "\"><button type=\"submit\">Reload</button>"
            "</form></td>\n</tr>\n</table>\n";
    }
    if (!littlefs_ok) {
        resp += "<p></p>\n"
            "<form action=\"/config.html\">"
            "<strong>LittleFS seems damaged. Saving will not work.</strong> Format it? "
            "<input type=\"hidden\" name=\"format\" value=\"" + String(token) + "\"><button type=\"submit\">Yes, format!</button>"
            "</form>\n";
    }
    resp += "<p></p>\n"
            "<form action=\"/config.html\">"
            "<table><tr>"
            "<td>Display always on</td>"
            "<td><input type=\"radio\" id=\"d_on\" name=\"display\" value=\"1\" " + (config.display_on?checked:String()) + "/>"
            "<label for=\"d_on\">on</label></td>"
            "<td><input type=\"radio\" id=\"d_off\" name=\"display\" value=\"0\"" + (config.display_on?String():checked) + "/>"
            "<label for=\"d_off\">off</label></td>"
            "<td><button type=\"submit\">Submit</button></td>"
            "</tr><tr>"
            "<td>Home Assistant discovery</td>"
            "<td><input type=\"radio\" id=\"ha_on\" name=\"ha_disc\" value=\"1\" " + (config.ha_discovery?checked:String()) + "/>"
            "<label for=\"ha_on\">on</label></td>"
            "<td><input type=\"radio\" id=\"ha_off\" name=\"ha_disc\" value=\"0\"" + (config.ha_discovery?String():checked) + "/>"
            "<label for=\"ha_off\">off</label></td>"
            "<td><button type=\"submit\">Submit</button></td>"
            "</tr><tr>"
            "<td>Debug Mode (RAW frames)</td>"
            "<td><input type=\"radio\" id=\"dbg_on\" name=\"debug_mode\" value=\"1\" " + (config.debug_mode?checked:String()) + "/>"
            "<label for=\"dbg_on\">on</label></td>"
            "<td><input type=\"radio\" id=\"dbg_off\" name=\"debug_mode\" value=\"0\"" + (config.debug_mode?String():checked) + "/>"
            "<label for=\"dbg_off\">off</label></td>"
            "<td><button type=\"submit\">Submit</button></td>"
            "</tr><tr>"  // NEU: Screensaver innerhalb der Tabelle
            "<td>Screensaver after 5min</td>"
            "<td><input type=\"radio\" id=\"ssaver_on\" name=\"screensaver_mode\" value=\"1\"" + (config.screensaver_mode?checked:String()) + "/>"
            "<label for=\"ssaver_on\">on</label></td>"
            "<td><input type=\"radio\" id=\"ssaver_off\" name=\"screensaver_mode\" value=\"0\"" + (config.screensaver_mode?String():checked) + "/>"
            "<label for=\"ssaver_off\">off</label></td>"
            "<td><button type=\"submit\">Submit</button></td>"
            "</tr></table>"
            "</form>\n";
    
    resp += "<p><a href=\"/update\">Update software</a></p>\n";
    if (config.debug_mode) {
        resp += "<p><a href=\"/debug.html\">Debug Log</a></p>\n";
    }
    resp += "<p><a href=\"/\">Main page</a></p>\n";
    add_sysinfo_footer(resp);
    resp += "</body></html>\n";
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