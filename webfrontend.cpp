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
extern unsigned long loop_count;
extern float cpu_usage;

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
    config.ha_discovery = true; /* default */
    config.debug_mode = false; /* default */
    config.screensaver_mode = true; /* default */
    config.mqtt_use_names = true;
    config.proto_lacrosse = true;
    config.proto_wh1080 = true;
    config.proto_tx38it = false;
    config.proto_tx35it = true;
    config.proto_ws1600 = true;
    config.proto_wt440xh = true;
    
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
        if (!doc["screensaver_mode"].isNull()) {
            config.screensaver_mode = doc["screensaver_mode"];
        }
        if (!doc["mqtt_use_names"].isNull()) {
            config.mqtt_use_names = doc["mqtt_use_names"];
        }
        
        if (!doc["proto_lacrosse"].isNull())
            config.proto_lacrosse = doc["proto_lacrosse"];
        if (!doc["proto_wh1080"].isNull())
            config.proto_wh1080 = doc["proto_wh1080"];
        if (!doc["proto_tx38it"].isNull())
            config.proto_tx38it = doc["proto_tx38it"];
        if (!doc["proto_tx35it"].isNull())
            config.proto_tx35it = doc["proto_tx35it"];
        if (!doc["proto_ws1600"].isNull())
            config.proto_ws1600 = doc["proto_ws1600"];
        if (!doc["proto_wt440xh"].isNull())
            config.proto_wt440xh = doc["proto_wt440xh"];
            
        Serial.println("result of config.json");
        Serial.println("mqtt_server: " + config.mqtt_server);
        Serial.println("mqtt_port: " + String(config.mqtt_port));
        Serial.println("mqtt_user: " + config.mqtt_user);
        Serial.println("mqtt_pass: " + String(config.mqtt_pass.length() > 0 ? "***" : "(not set)"));
        Serial.println("ha_discovery: " + String(config.ha_discovery));
        Serial.println("display_on: " + String(config.display_on));
        Serial.println("debug_mode: " + String(config.debug_mode));
        Serial.println("screensaver_mode: " + String(config.screensaver_mode));
        Serial.println("proto_lacrosse: " + String(config.proto_lacrosse));
        Serial.println("proto_wh1080: " + String(config.proto_wh1080));
        Serial.println("proto_tx38it: " + String(config.proto_tx38it));
        Serial.println("proto_tx35it: " + String(config.proto_tx35it));
        Serial.println("proto_ws1600: " + String(config.proto_ws1600));
        Serial.println("proto_wt440xh: " + String(config.proto_wt440xh));
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
    doc["screensaver_mode"] = config.screensaver_mode;
    doc["mqtt_use_names"] = config.mqtt_use_names;
    doc["proto_lacrosse"] = config.proto_lacrosse;
    doc["proto_wh1080"] = config.proto_wh1080;
    doc["proto_tx38it"] = config.proto_tx38it;
    doc["proto_tx35it"] = config.proto_tx35it;
    doc["proto_ws1600"] = config.proto_ws1600;
    doc["proto_wt440xh"] = config.proto_wt440xh;
    
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
    s += "<table id='sensor-table'>\n";
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
    s += "<tbody id='sensor-tbody'>\n";

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
        s += "<p id='sensor-count'><em>Total sensors: " + String(sensorCount) + "</em></p>\n";
    }
}

// NEU: JSON-Endpoint für Sensordaten
void handle_sensors_json() {
    unsigned long now = millis();
    JsonDocument doc;
    JsonArray sensors = doc["sensors"].to<JsonArray>();
    
    int sensorCount = 0;
    for (int i = 0; i < SENSOR_NUM; i++) {
        if (fcache[i].timestamp == 0 || fcache[i].ID == 0xFF)
            continue;
        
        JsonObject sensor = sensors.add<JsonObject>();
        sensor["id"] = fcache[i].ID;
        sensor["ch"] = fcache[i].channel;
        sensor["type"] = String(fcache[i].sensorType);
        sensor["temp"] = serialized(String(fcache[i].temp, 1));
        sensor["humi"] = fcache[i].humi;
        sensor["rssi"] = fcache[i].rssi;
        sensor["name"] = id2name[fcache[i].ID];
        sensor["age"] = now - fcache[i].timestamp;
        sensor["batlo"] = fcache[i].batlo;
        sensor["init"] = fcache[i].init;
        
        String rawData = "";
        for (int j = 0; j < FRAME_LENGTH; j++) {
            char tmp[3];
            snprintf(tmp, 3, "%02X", fcache[i].data[j]);
            rawData += String(tmp);
            if (j < FRAME_LENGTH - 1)
                rawData += " ";
        }
        sensor["raw"] = rawData;
        
        sensorCount++;
    }
    
    doc["count"] = sensorCount;
//    doc["loop_count"] = loop_count;
    doc["uptime"] = time_string();
    doc["mqtt_ok"] = mqtt_ok;
    doc["wifi_ok"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["cpu_usage"] = serialized(String(cpu_usage, 1));
    
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void add_header(String &s, const String &title)
{
    s = "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    
    if (title.indexOf("Gateway") > -1 || title.indexOf("Configuration") > -1) {
        s += "<script>"
             "let autoRefreshEnabled = true;"
             "let refreshInterval = 5000;"
             "let refreshTimer;"
             
             "function updateSensorData() {"
"  if (!autoRefreshEnabled) return;"
"  fetch('/sensors.json')"
"    .then(response => response.json())"
"    .then(data => {"
"      const tbody = document.getElementById('sensor-tbody');"
"      if (tbody) {"
"        tbody.innerHTML = '';"
"        data.sensors.forEach(sensor => {"
"          const row = tbody.insertRow();"
"          row.innerHTML = '<td>' + sensor.id + '</td>' +"  // ← String-Konkatenation statt Template
"            '<td>' + sensor.ch + '</td>' +"
"            '<td>' + sensor.type + '</td>' +"
"            '<td>' + sensor.temp + ' °C</td>' +"
"            '<td>' + (sensor.humi > 0 && sensor.humi <= 100 ? sensor.humi + ' %' : '-') + '</td>' +"
"            '<td>' + sensor.rssi + '</td>' +"
"            '<td>' + (sensor.name || '-') + '</td>' +"
"            '<td>' + sensor.age + '</td>' +"
"            '<td class=\"' + (sensor.batlo ? 'batt-weak' : 'batt-ok') + '\">' + (sensor.batlo ? 'weak' : 'ok') + '</td>' +"
"            '<td class=\"' + (sensor.init ? 'init-new' : 'init-no') + '\">' + (sensor.init ? 'yes' : 'no') + '</td>' +"
"            '<td class=\"raw-data\">0x' + sensor.raw + '</td>';"
"        });"
"      }"
"      const systemStatus = document.getElementById('system-status');"
"      if (systemStatus) {"
"        let statusHtml = '';"
"        if (data.mqtt_ok) {"
"          statusHtml += '<span class=\"status-badge status-ok\">✓ MQTT Connected</span> ';"
"        } else {"
"          statusHtml += '<span class=\"status-badge status-error\">✗ MQTT Disconnected</span> ';"
"        }"
"        if (data.wifi_ok) {"
"          statusHtml += '<span class=\"status-badge status-ok\">✓ WiFi Connected</span>';"
"        } else {"
"          statusHtml += '<span class=\"status-badge status-error\">✗ WiFi Disconnected</span>';"
"        }"
"        systemStatus.innerHTML = statusHtml;"
"      }"
"      const wifiSsid = document.getElementById('wifi-ssid');"
"      if (wifiSsid && data.wifi_ssid) {"
"        wifiSsid.textContent = 'SSID: ' + data.wifi_ssid;"
"      }"
"      const wifiIp = document.getElementById('wifi-ip');"
"      if (wifiIp && data.wifi_ip) {"
"        wifiIp.textContent = 'IP: ' + data.wifi_ip;"
"      }"
"      const uptime = document.getElementById('system-uptime');"
"      if (uptime && data.uptime) {"
"        uptime.textContent = 'Uptime: ' + data.uptime;"
"      }"
"      const cpuLoad = document.getElementById('cpu-load');"
"      if (cpuLoad && data.cpu_usage) {"
"        cpuLoad.textContent = 'CPU Load: ' + data.cpu_usage + '%';"
"      }"
"      const countElem = document.getElementById('sensor-count');"
"      if (countElem) {"
"        if (data.count === 0) {"
"          countElem.innerHTML = '<em>No sensors found. Waiting for data...</em>';"
"        } else {"
"          countElem.innerHTML = '<em>Total sensors: ' + data.count + ' | Last update: ' + new Date().toLocaleTimeString() + '</em>';"
"        }"
"      }"
"      const refreshStatus = document.getElementById('refresh-status');"
"      if (refreshStatus) {"
"        refreshStatus.textContent = '✓ Live (updated ' + new Date().toLocaleTimeString() + ')';"
"        refreshStatus.style.color = 'var(--success-color)';"
"      }"
"    })"
"    .catch(error => {"
"      console.error('Error:', error);"
"      const refreshStatus = document.getElementById('refresh-status');"
"      if (refreshStatus) {"
"        refreshStatus.textContent = '✗ Error';"
"        refreshStatus.style.color = 'var(--error-color)';"
"      }"
"    });"
"}"

             "function toggleAutoRefresh() {"
             "  autoRefreshEnabled = !autoRefreshEnabled;"
             "  const btn = document.getElementById('auto-refresh-btn');"
             "  const status = document.getElementById('refresh-status');"
             "  "
             "  if (autoRefreshEnabled) {"
             "    btn.textContent = '⏸️ Pause Auto-Refresh';"
             "    btn.style.backgroundColor = 'var(--warning-color)';"
             "    status.textContent = '⏳ Starting...';"
             "    status.style.color = 'var(--info-color)';"
             "    startAutoRefresh();"
             "    updateSensorData();"
             "  } else {"
             "    btn.textContent = '▶️ Resume Auto-Refresh';"
             "    btn.style.backgroundColor = 'var(--success-color)';"
             "    status.textContent = '⏸️ Paused';"
             "    status.style.color = 'var(--warning-color)';"
             "    if (refreshTimer) clearInterval(refreshTimer);"
             "  }"
             "}"
             
             "function startAutoRefresh() {"
             "  if (refreshTimer) clearInterval(refreshTimer);"
             "  refreshTimer = setInterval(updateSensorData, refreshInterval);"
             "}"
             
             "window.addEventListener('DOMContentLoaded', () => {"
             "  startAutoRefresh();"
             "  setTimeout(updateSensorData, 1000);"
             "});"
             "</script>";
    }
    
    s += "<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'>"
        "<circle cx='50' cy='30' r='15' fill='%2303a9f4'/>"
        "<rect x='43' y='28' width='14' height='45' rx='7' fill='%2303a9f4'/>"
        "<circle cx='50' cy='70' r='12' fill='%23ff5252'/>"
        "<rect x='46' y='35' width='8' height='30' fill='%23ff5252'/>"
        "</svg>\">";


    s += "<style>"
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
            "font-size: 15px; "
            "transition: all 0.3s; "
        "}"
        "[data-theme='dark'] table { "
            "box-shadow: 0 2px 4px rgba(0,0,0,0.3); "
        "}"
        "thead { "
            "background-color: var(--secondary-background-color); "
        "}"
        "th { "
            "padding: 12px 10px; "
            "text-align: left; "
            "font-weight: 500; "
            "color: var(--primary-text-color); "
            "text-transform: uppercase; "
            "font-size: 12px; "
            "letter-spacing: 0.5px; "
            "border-bottom: 1px solid var(--divider-color); "
        "}"
        "td { "
            "padding: 10px; "
            "border-bottom: 1px solid var(--divider-color); "
            "color: var(--primary-text-color); "
            "font-size: 15px; "
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
            "font-size: 14px; "
            "padding: 4px 8px; "
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
            "font-size: 14px; "
        "}"
        ".init-new { "
            "color: var(--info-color); "
            "font-weight: 500; "
            "font-size: 14px; "
        "}"
        ".init-no { "
            "color: var(--secondary-text-color); "
            "font-size: 14px; "
        "}"
        ".raw-data { "
            "font-family: 'Roboto Mono', 'Courier New', monospace; "
            "font-size: 12px; "
            "color: var(--primary-color); "
            "background-color: rgba(3, 169, 244, 0.08); "
            "padding: 4px 8px; "
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
            "min-width: 18px; "
            "min-height: 18px; "
            "border: 2px solid var(--divider-color); "
            "border-radius: 50%; "
            "margin: 0 8px 0 0; "
            "cursor: pointer; "
            "position: relative; "
            "transition: all 0.2s; "
            "background-color: var(--secondary-background-color); "
            "flex-shrink: 0; "
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
            "min-width: 18px; "
            "min-height: 18px; "
            "border: 2px solid var(--divider-color); "
            "border-radius: 4px; "
            "margin: 0 8px 0 0; "
            "cursor: pointer; "
            "position: relative; "
            "transition: all 0.2s; "
            "background-color: var(--secondary-background-color); "
            "flex-shrink: 0; "
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
            "flex-direction: column; "
            "padding: 6px; "
            "border-radius: 4px; "
            "cursor: pointer; "
            "transition: background-color 0.2s; "
        "}"
        ".radio-item label { "
            "display: flex; "
            "align-items: center; "
            "cursor: pointer; "
            "margin: 0; "
        "}"
        ".option-description { "
            "margin: 4px 0 0 26px; "
            "color: var(--secondary-text-color); "
            "font-size: 11px; "
            "line-height: 1.4; "
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
        "}";

    s += ".refresh-control { "		  
        "background-color: var(--card-background-color); "															  
        "border-radius: 8px; "
        "padding: 12px; " 
        "margin: 12px 0; "
        "display: flex; "
        "align-items: center; "
        "justify-content: space-between; "
        "border: 1px solid var(--divider-color); "
    "}";
    
    s += ".refresh-status { "
        "font-size: 13px; "
        "color: var(--info-color); "
        "font-weight: 500; "
    "}";
    
    s += "</style>";
    s += "<title>" + title + "</title></head>";
    s += "<body>";
    s += "<div class='header-container'>";
    s += "<h1>🌡️ " + title + "</h1>";
    s += "<div class='theme-toggle' onclick='toggleTheme()'>";
    s += "<span class='theme-icon' id='theme-icon'>🌙</span>";
    s += "<span id='theme-text'>Dark Mode</span>";
    s += "</div>";
    s += "</div>";
    s += "<script>"
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

        default: break;
    }
    return F("No meaning"); // 0 and undefined
}

static void add_sysinfo_footer(String &s)
{
    s += "<div class='footer'>"
         "<a href='https://github.com/steigerbalett/lacrosse2mqtt' target='_blank'>Powered by LaCrosse2MQTT</a> | "
         "<a href='/'>🏠 Home</a> | "
         "<a href='/config.html'>⚙️ Configuration</a> | "
         "<a href='/update'>📦 Update</a>"
         "<a href='/licenses.html'>📄 Licenses</a>";
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
    index += "<p id='system-status'>";
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
    
    index += "<p class='info-text' id='wifi-ssid'>SSID: " + WiFi.SSID() + "</p>";
    index += "<p class='info-text' id='wifi-ip'>IP: " + WiFi.localIP().toString() + "</p>";
    index += "<p class='info-text' id='system-uptime'>Uptime: " + time_string() + "</p>";
    index += "<p class='info-text' id='cpu-load'>CPU Load: " + String(cpu_usage, 1) + "%</p>";
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
    
    index += "<div class='card card-full'>";
    index += "<div style='display:flex;justify-content:space-between;align-items:center;padding:8px;background-color:var(--secondary-background-color);border-radius:4px;'>";
    index += "<div>";
    index += "<strong>Automatic Data Refresh</strong><br>";
    index += "<span style='font-size:12px;color:var(--secondary-text-color)'>Sensor data updates every 5 seconds</span><br>";
    index += "<span class='refresh-status' id='refresh-status' style='font-size:12px;'>⏳ Starting...</span>";
    index += "</div>";
    index += "<button id='auto-refresh-btn' onclick='toggleAutoRefresh()' style='background-color:var(--warning-color);min-width:100px;'>⏸️ Pause Auto-Refresh</button>";
    index += "</div>";
    index += "</div>";

    add_sysinfo_footer(index);
    server.send(200, "text/html", index);
}

void handle_licenses()
{
    String page;
    add_header(page, "Licenses & Attributions");
    
    page += "<div class='card card-full'>";
    page += "<h2>📄 Licenses and Attributions</h2>";
    page += "<p class='info-text'>This project uses and builds upon the following open-source components and projects:</p>";
    
    // ========== INSPIRATION & ORIGINAL PROJECTS ==========
    page += "<div class='card'>";
    page += "<h3>🌟 Inspiration & Original Projects</h3>";
    page += "<p class='info-text'>This project was inspired by and builds upon the excellent work of:</p>";
    
    // FHEM
    page += "<div style='margin: 20px 0; padding: 15px; border-left: 4px solid var(--accent-color); background-color: rgba(255, 152, 0, 0.08);'>";
    page += "<h4 style='margin-top: 0; color: var(--accent-color);'>🏠 FHEM - Home Automation Server</h4>";
    page += "<p><strong>Project:</strong> FHEM (Freundliche Hausautomation und Energie-Messung)</p>";
    page += "<p><strong>Description:</strong> FHEM is a powerful Perl-based home automation server used to automate common tasks ";
    page += "in the household and to collect data from various sensors. The LaCrosse protocol implementations in this project ";
    page += "are based on the excellent work done by the FHEM community.</p>";
    page += "<p><strong>Website:</strong> <a href='https://fhem.de/' target='_blank'>fhem.de</a></p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/fhem/fhem-mirror' target='_blank'>github.com/fhem/fhem-mirror</a></p>";
    page += "<p><strong>Relevant Modules:</strong></p>";
    page += "<ul style='margin-top: 8px;'>";
    page += "<li><strong>36_LaCrosse.pm</strong> - LaCrosse IT+ sensor protocol decoder</li>";
    page += "<li><strong>14_SD_WS.pm</strong> - Weather station protocol implementation (WH1080, WS1600, etc.)</li>";
    page += "<li><strong>SIGNALduino</strong> - Radio signal processing foundation</li>";
    page += "</ul>";
    page += "<p><strong>License:</strong> GPL v2</p>";
    page += "<p><strong>Attribution:</strong> Special thanks to the FHEM developers for their comprehensive protocol documentation and reference implementations.</p>";
    page += "</div>";
    
    // LaCrosse2MQTT Original
    page += "<div style='margin: 20px 0; padding: 15px; border-left: 4px solid var(--primary-color); background-color: rgba(3, 169, 244, 0.08);'>";
    page += "<h4 style='margin-top: 0; color: var(--primary-color);'>📡 LaCrosse Gateway (Original)</h4>";
    page += "<p><strong>Author:</strong> Stefan Seyfried</p>";
    page += "<p><strong>Description:</strong> Original LaCrosse gateway project that inspired this MQTT implementation. ";
    page += "Provides the foundation for receiving and decoding LaCrosse IT+ sensor data using ESP32 and LoRa modules.</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/seife/LaCrosseGateway' target='_blank'>github.com/seife/LaCrosseGateway</a></p>";
    page += "<p><strong>License:</strong> GPL-2.0</p>";
    page += "</div>";
    
    // Weather Station Protocol Resources
    page += "<div style='margin: 20px 0; padding: 15px; border-left: 4px solid var(--info-color); background-color: rgba(33, 150, 243, 0.08);'>";
    page += "<h4 style='margin-top: 0; color: var(--info-color);'>🌦️ Weather Station Protocol Documentation</h4>";
    page += "<p><strong>WH1080 Protocol:</strong></p>";
    page += "<ul style='margin: 8px 0;'>";
    page += "<li><a href='http://www.sevenwatt.com/main/wh1080-protocol-v2-fsk/' target='_blank'>SevenWatt WH1080 Protocol Documentation</a></li>";
    page += "<li><a href='https://github.com/NetHome/Coders/tree/master/src/main/java/nu/nethome/coders/decoders' target='_blank'>NetHome Coders - Weather Station Decoders</a></li>";
    page += "</ul>";
    page += "<p><strong>RTL-433 Project:</strong> Comprehensive radio signal decoder</p>";
    page += "<ul style='margin: 8px 0;'>";
    page += "<li><a href='https://github.com/merbanan/rtl_433' target='_blank'>github.com/merbanan/rtl_433</a></li>";
    page += "<li>Protocol documentation for 433MHz sensors including LaCrosse, WH1080, WS1600</li>";
    page += "</ul>";
    page += "</div>";
    
    page += "</div>"; // Ende Inspiration card
    
    // ========== MAIN PROJECT ==========
    page += "<div class='card'>";
    page += "<h3>📦 LaCrosse2MQTT</h3>";
    page += "<p><strong>Copyright:</strong> © 2021 Stefan Seyfried</p>";
    page += "<p><strong>Enhanced by:</strong> Community Contributors (2024-2026)</p>";
    page += "<p><strong>License:</strong> GNU General Public License v2.0 or later (GPL-2.0-or-later)</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/steigerbalett/lacrosse2mqtt' target='_blank'>github.com/steigerbalett/lacrosse2mqtt</a></p>";
    page += "<p><strong>Description:</strong> Bridge for LaCrosse IT+, WH1080, WS1600, WT440XH, and compatible 868MHz weather sensors to MQTT with Home Assistant auto-discovery.</p>";
    page += "<p><strong>Supported Protocols:</strong></p>";
    page += "<ul style='margin-top: 8px;'>";
    page += "<li><strong>LaCrosse IT+</strong> - TX29-IT, TX27-IT, TX25-U, TX29DTH-IT (Temperature/Humidity sensors with multi-channel support)</li>";
    page += "<li><strong>WH1080</strong> - Complete weather stations (Wind, Rain, Temperature, Humidity)</li>";
    page += "<li><strong>WS1600</strong> - Weather sensors with multi-channel capability</li>";
    page += "<li><strong>WT440XH</strong> - Compact temperature/humidity sensors</li>";
    page += "<li><strong>TX35-IT/TX35DTH-IT</strong> - Additional sensor variants (9.579 kbps)</li>";
    page += "<li><strong>TX38-IT</strong> - Indoor temperature sensors (8.842 kbps)</li>";
    page += "</ul>";
    page += "</div>";
    
    // ========== THIRD-PARTY LIBRARIES ==========
    page += "<div class='card'>";
    page += "<h3>📚 Third-Party Libraries</h3>";
    
    // Arduino ESP32 Core
    page += "<div style='margin: 15px 0; padding: 10px; border-left: 3px solid var(--primary-color);'>";
    page += "<h4>1. Arduino Core for ESP32</h4>";
    page += "<p><strong>Developer:</strong> Espressif Systems | <strong>License:</strong> LGPL-2.1</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/espressif/arduino-esp32' target='_blank'>github.com/espressif/arduino-esp32</a></p>";
    page += "<p><strong>Purpose:</strong> ESP32 platform support and core libraries</p>";
    page += "</div>";
    
    // LittleFS
    page += "<div style='margin: 15px 0; padding: 10px; border-left: 3px solid var(--primary-color);'>";
    page += "<h4>2. LittleFS for ESP32</h4>";
    page += "<p><strong>Author:</strong> lorol | <strong>License:</strong> LGPL</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/lorol/LITTLEFS' target='_blank'>github.com/lorol/LITTLEFS</a></p>";
    page += "<p><strong>Purpose:</strong> Filesystem for configuration and data storage</p>";
    page += "</div>";
    
    // PubSubClient
    page += "<div style='margin: 15px 0; padding: 10px; border-left: 3px solid var(--primary-color);'>";
    page += "<h4>3. PubSubClient</h4>";
    page += "<p><strong>Author:</strong> Nick O'Leary | <strong>License:</strong> MIT License</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/knolleary/pubsubclient' target='_blank'>github.com/knolleary/pubsubclient</a></p>";
    page += "<p><strong>Purpose:</strong> MQTT client library for Arduino</p>";
    page += "</div>";
    
    // ArduinoJson
    page += "<div style='margin: 15px 0; padding: 10px; border-left: 3px solid var(--primary-color);'>";
    page += "<h4>4. ArduinoJson</h4>";
    page += "<p><strong>Author:</strong> Benoit Blanchon | <strong>License:</strong> MIT License</p>";
    page += "<p><strong>Website:</strong> <a href='https://arduinojson.org/' target='_blank'>arduinojson.org</a></p>";
    page += "<p><strong>Purpose:</strong> JSON parsing and serialization</p>";
    page += "</div>";
    
    // Adafruit SSD1306
    page += "<div style='margin: 15px 0; padding: 10px; border-left: 3px solid var(--primary-color);'>";
    page += "<h4>5. Adafruit SSD1306</h4>";
    page += "<p><strong>Author:</strong> Adafruit Industries | <strong>License:</strong> BSD License</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/adafruit/Adafruit_SSD1306' target='_blank'>github.com/adafruit/Adafruit_SSD1306</a></p>";
    page += "<p><strong>Purpose:</strong> OLED display driver for status display</p>";
    page += "</div>";
    
    // Adafruit GFX
    page += "<div style='margin: 15px 0; padding: 10px; border-left: 3px solid var(--primary-color);'>";
    page += "<h4>6. Adafruit GFX Library</h4>";
    page += "<p><strong>Author:</strong> Adafruit Industries | <strong>License:</strong> BSD License</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/adafruit/Adafruit-GFX-Library' target='_blank'>github.com/adafruit/Adafruit-GFX-Library</a></p>";
    page += "<p><strong>Purpose:</strong> Graphics library for displays</p>";
    page += "</div>";
    
    // WiFiManager
    page += "<div style='margin: 15px 0; padding: 10px; border-left: 3px solid var(--primary-color);'>";
    page += "<h4>7. WiFiManager</h4>";
    page += "<p><strong>Author:</strong> tzapu | <strong>License:</strong> MIT License</p>";
    page += "<p><strong>Repository:</strong> <a href='https://github.com/tzapu/WiFiManager' target='_blank'>github.com/tzapu/WiFiManager</a></p>";
    page += "<p><strong>Purpose:</strong> WiFi configuration portal for easy setup</p>";
    page += "</div>";
    
    page += "</div>"; // Ende Third-Party Libraries card
    
    // ========== LICENSE SUMMARY TABLE ==========
    page += "<div class='card'>";
    page += "<h3>📋 License Summary</h3>";
    page += "<table>";
    page += "<thead><tr><th>Component</th><th>License</th><th>Purpose</th></tr></thead>";
    page += "<tbody>";
    page += "<tr><td>LaCrosse2MQTT</td><td>GPL-2.0-or-later</td><td>Main project</td></tr>";
    page += "<tr><td>FHEM Protocol Implementations</td><td>GPL-2.0</td><td>Protocol reference</td></tr>";
    page += "<tr><td>ESP32 Arduino Core</td><td>LGPL-2.1</td><td>Platform support</td></tr>";
    page += "<tr><td>PubSubClient</td><td>MIT</td><td>MQTT client</td></tr>";
    page += "<tr><td>ArduinoJson</td><td>MIT</td><td>JSON parsing</td></tr>";
    page += "<tr><td>WiFiManager</td><td>MIT</td><td>WiFi configuration</td></tr>";
    page += "<tr><td>Adafruit SSD1306</td><td>BSD</td><td>OLED display driver</td></tr>";
    page += "<tr><td>Adafruit GFX</td><td>BSD</td><td>Graphics library</td></tr>";
    page += "<tr><td>LittleFS</td><td>LGPL</td><td>Filesystem</td></tr>";
    page += "</tbody>";
    page += "</table>";
    page += "</div>";
    
    // ========== HARDWARE & RF INFORMATION ==========
    page += "<div class='card'>";
    page += "<h3>🔧 Hardware & RF Protocol Information</h3>";
    page += "<p><strong>Radio Frequency:</strong> 868.3 MHz (EU ISM Band)</p>";
    page += "<p><strong>Modulation:</strong> FSK (Frequency Shift Keying)</p>";
    page += "<p><strong>Supported Hardware:</strong></p>";
    page += "<ul style='margin-top: 8px;'>";
    page += "<li>TTGO LoRa32 (ESP32 with SX1276/SX1278 LoRa module)</li>";
    page += "<li>Heltec WiFi LoRa 32</li>";
    page += "<li>Other ESP32 boards with compatible SX127x modules</li>";
    page += "</ul>";
    page += "<p><strong>Protocol Bit Rates:</strong></p>";
    page += "<ul style='margin-top: 8px;'>";
    page += "<li>LaCrosse IT+ (TX29-IT, TX27-IT): 17.241 kbps</li>";
    page += "<li>TX35-IT / TX35DTH-IT: 9.579 kbps</li>";
    page += "<li>TX38-IT: 8.842 kbps</li>";
    page += "<li>WH1080, WS1600: Various rates (auto-detected)</li>";
    page += "</ul>";
    page += "</div>";
    
    // ========== FULL LICENSE TEXTS ==========
    page += "<div class='card'>";
    page += "<h3>📜 Full License Texts</h3>";
    page += "<p>Complete license texts are available at:</p>";
    page += "<ul style='margin-top: 12px; line-height: 1.8;'>";
    page += "<li><strong>GNU GPL v2.0:</strong> <a href='https://www.gnu.org/licenses/gpl-2.0.html' target='_blank'>gnu.org/licenses/gpl-2.0.html</a></li>";
    page += "<li><strong>GNU LGPL:</strong> <a href='https://www.gnu.org/licenses/lgpl.html' target='_blank'>gnu.org/licenses/lgpl.html</a></li>";
    page += "<li><strong>MIT License:</strong> <a href='https://opensource.org/licenses/MIT' target='_blank'>opensource.org/licenses/MIT</a></li>";
    page += "<li><strong>BSD License:</strong> <a href='https://opensource.org/licenses/BSD-3-Clause' target='_blank'>opensource.org/licenses/BSD-3-Clause</a></li>";
    page += "</ul>";
    page += "</div>";
    
    // ========== ACKNOWLEDGMENTS ==========
    page += "<div class='card'>";
    page += "<h3>🙏 Acknowledgments</h3>";
    page += "<p>Special thanks to:</p>";
    page += "<ul style='margin-top: 12px; line-height: 1.8;'>";
    page += "<li><strong>FHEM Community</strong> - For comprehensive protocol documentation and reference implementations</li>";
    page += "<li><strong>Stefan Seyfried (seife)</strong> - Original LaCrosse Gateway project</li>";
    page += "<li><strong>RTL-433 Project</strong> - Extensive 433/868MHz protocol database</li>";
    page += "<li><strong>SevenWatt.com</strong> - WH1080 protocol documentation</li>";
    page += "<li><strong>Home Assistant Community</strong> - MQTT Discovery integration patterns</li>";
    page += "<li><strong>All Open Source Contributors</strong> - For making this project possible</li>";
    page += "</ul>";
    page += "</div>";
    
    // ========== FOOTER INFO ==========
    page += "<div class='card' style='background-color: rgba(3, 169, 244, 0.08); border: 1px solid var(--primary-color);'>";
    page += "<p style='margin: 0; text-align: center; font-size: 13px;'>";
    page += "<strong>Complete Documentation:</strong> ";
    page += "<a href='https://github.com/steigerbalett/lacrosse2mqtt' target='_blank'>GitHub Repository</a> | ";
    page += "<a href='https://github.com/steigerbalett/lacrosse2mqtt/blob/main/README.md' target='_blank'>README</a> | ";
    page += "<a href='https://github.com/steigerbalett/lacrosse2mqtt/blob/main/LICENSES.md' target='_blank'>Full License File</a>";
    page += "</p>";
    page += "</div>";
    
    page += "</div>"; // Ende card-full
    
    add_sysinfo_footer(page);
    server.send(200, "text/html", page);
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
    if (server.hasArg("mqtt_use_names")) {
        String on = server.arg("mqtt_use_names");
        int tmp = on.toInt();
        if (tmp != config.mqtt_use_names) {
            config_changed = true;
            config.mqtt_use_names = tmp;
            config.changed = true;
            Serial.println("MQTT use names changed to " + String(config.mqtt_use_names));
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
    
    // Prüfe ob IRGENDEINE Protokoll-Checkbox gesendet wurde
    bool proto_form_submitted = false;
    for (int i = 0; i < server.args(); i++) {
        String argName = server.argName(i);
        if (argName.startsWith("proto_")) {
            proto_form_submitted = true;
            break;
        }
    }
    
    // Wenn Protokoll-Formular abgeschickt wurde, alle Werte neu setzen
    if (proto_form_submitted) {
        Serial.println("Protocol form submitted, updating all protocol settings...");
        
        bool new_lacrosse = (server.hasArg("proto_lacrosse") && server.arg("proto_lacrosse") == "1");
        bool new_wh1080 = (server.hasArg("proto_wh1080") && server.arg("proto_wh1080") == "1");
        bool new_tx38it = (server.hasArg("proto_tx38it") && server.arg("proto_tx38it") == "1");
        bool new_tx35it = (server.hasArg("proto_tx35it") && server.arg("proto_tx35it") == "1");
        bool new_ws1600 = (server.hasArg("proto_ws1600") && server.arg("proto_ws1600") == "1");
        bool new_wt440xh = (server.hasArg("proto_wt440xh") && server.arg("proto_wt440xh") == "1");
        
        // Prüfe auf Änderungen und update
        if (new_lacrosse != config.proto_lacrosse) {
            config.proto_lacrosse = new_lacrosse;
            config_changed = true;
            config.changed = true;
            Serial.println("LaCrosse protocol changed to: " + String(config.proto_lacrosse));
        }
        if (new_wh1080 != config.proto_wh1080) {
            config.proto_wh1080 = new_wh1080;
            config_changed = true;
            config.changed = true;
            Serial.println("WH1080 protocol changed to: " + String(config.proto_wh1080));
        }
        if (new_tx38it != config.proto_tx38it) {
            config.proto_tx38it = new_tx38it;
            config_changed = true;
            config.changed = true;
            Serial.println("TX38IT protocol changed to: " + String(config.proto_tx38it));
        }
        if (new_tx35it != config.proto_tx35it) {
            config.proto_tx35it = new_tx35it;
            config_changed = true;
            config.changed = true;
            Serial.println("TX35IT protocol changed to: " + String(config.proto_tx35it));
        }
        if (new_ws1600 != config.proto_ws1600) {
            config.proto_ws1600 = new_ws1600;
            config_changed = true;
            config.changed = true;
            Serial.println("WS1600 protocol changed to: " + String(config.proto_ws1600));
        }
        if (new_wt440xh != config.proto_wt440xh) {
            config.proto_wt440xh = new_wt440xh;
            config_changed = true;
            config.changed = true;
            Serial.println("WT440XH protocol changed to: " + String(config.proto_wt440xh));
        }
    }
    
    String resp;
    add_header(resp, "LaCrosse2MQTT Configuration");
    
    resp += "<div class='card-grid'>";
    
    resp += "<div class='card'>";
    resp += "<h2>System Status</h2>";
    resp += "<p id='system-status'>";
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

     // CPU-Auslastung mit Farbe
    resp += "<p class='info-text' id='cpu-load'>CPU Load: ";
    resp += "<span style='font-weight: 500; color: ";
    if (cpu_usage < 50) {
        resp += "var(--success-color);'>"; // Grün
    } else if (cpu_usage < 80) {
        resp += "var(--warning-color);'>"; // Orange
    } else {
        resp += "var(--error-color);'>"; // Rot
    }
    resp += String(cpu_usage, 1) + "%</span>";
    
    // CPU-Balken
    resp += " <span style='display: inline-block; width: 100px; height: 8px; background: var(--divider-color); border-radius: 4px; vertical-align: middle;'>";
    resp += "<span style='display: block; width: " + String(cpu_usage, 0) + "%; height: 100%; background: ";
    if (cpu_usage < 50) {
        resp += "var(--success-color);";
    } else if (cpu_usage < 80) {
        resp += "var(--warning-color);";
    } else {
        resp += "var(--error-color);";
    }
    resp += " border-radius: 4px;'></span></span>";
    resp += "</p>";

    resp += "</p>";
    resp += "<p class='info-text' id='wifi-ssid'>SSID: " + WiFi.SSID() + "</p>";
    resp += "<p class='info-text' id='wifi-ip'>IP: " + WiFi.localIP().toString() + "</p>";
    resp += "<p class='info-text' id='system-uptime'>Uptime: " + time_string() + "</p>";
    resp += "<p class='info-text'>Loop Count: " + String(loop_count) + "</p>";
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

    resp += "<div class=\"card\">";
    resp += "<h2>MQTT Topic Settings</h2>";
    resp += "<form action=\"config.html\">";
    resp += "<div class=\"radio-group\">";
    resp += "<div class=\"radio-item\">";
    resp += "<label>";
    resp += "<input type=\"radio\" name=\"mqtt_use_names\" value=\"1\"";
    if (config.mqtt_use_names) resp += " checked";
    resp += ">";
    resp += "Use Sensor Names";
    resp += "</label>";
    resp += "<div class=\"option-description\">Publish to climate/SensorName/temp (requires sensor names to be set)</div>";
    resp += "</div>";
    resp += "<div class=\"radio-item\">";
    resp += "<label>";
    resp += "<input type=\"radio\" name=\"mqtt_use_names\" value=\"0\"";
    if (!config.mqtt_use_names) resp += " checked";
    resp += ">";
    resp += "Use Sensor IDs";
    resp += "</label>";
    resp += "<div class=\"option-description\">Publish to lacrosse/id/30/temp (default)</div>";
    resp += "</div>";
    resp += "</div>";
    resp += "<button type=\"submit\">Update MQTT Topics</button>";
    resp += "</form>";
    resp += "</div>";

    resp += "<div class='card'>";
    resp += "<h2>📡 Protocol Settings</h2>";
    resp += "<p class='info-text'>Enable or disable support for specific sensor protocols. Changes require saving configuration.</p>";
    resp += "<form action='/config.html'>";
    
    // LaCrosse IT+
    resp += "<div class='radio-group'>";
    resp += "<h3 style='margin: 8px 0; font-size: 14px; color: var(--primary-color);'>LaCrosse IT+</h3>";
    resp += "<div class='radio-item'>";
    resp += "<label>";
    resp += "<input type='checkbox' name='proto_lacrosse' value='1'";
    if (config.proto_lacrosse) resp += checked;
    resp += " onchange='this.form.submit()'>";
    resp += "Enable LaCrosse IT+ Protocol";
    resp += "</label>";
    resp += "</div>";
    resp += "<div class='option-description'>TX29-IT, TX27-IT, TX25-U, TX29DTH-IT (17.241 kbps)</div>";
    resp += "</div>";
    
    // WH1080
    resp += "<div class='radio-group'>";
    resp += "<h3 style='margin: 8px 0; font-size: 14px; color: var(--primary-color);'>WH1080 Weather Station</h3>";
    resp += "<div class='radio-item'>";
    resp += "<label>";
    resp += "<input type='checkbox' name='proto_wh1080' value='1'";
    if (config.proto_wh1080) resp += checked;
    resp += " onchange='this.form.submit()'>";
    resp += "Enable WH1080 Protocol";
    resp += "</label>";
    resp += "</div>";
    resp += "<div class='option-description'>Weather stations with wind, rain, and temperature data (10 bytes)</div>";
    resp += "</div>";
    
    // TX38IT
    resp += "<div class='radio-group'>";
    resp += "<h3 style='margin: 8px 0; font-size: 14px; color: var(--primary-color);'>TX38IT Indoor</h3>";
    resp += "<div class='radio-item'>";
    resp += "<label>";
    resp += "<input type='checkbox' name='proto_tx38it' value='1'";
    if (config.proto_tx38it) resp += checked;
    resp += " onchange='this.form.submit()'>";
    resp += "Enable TX38IT Protocol";
    resp += "</label>";
    resp += "</div>";
    resp += "<div class='option-description'>Indoor temperature sensors (8.842 kbps)</div>";
    resp += "</div>";
    
    // TX35IT
    resp += "<div class='radio-group'>";
    resp += "<h3 style='margin: 8px 0; font-size: 14px; color: var(--primary-color);'>TX35-IT/TX35DTH-IT</h3>";
    resp += "<div class='radio-item'>";
    resp += "<label>";
    resp += "<input type='checkbox' name='proto_tx35it' value='1'";
    if (config.proto_tx35it) resp += checked;
    resp += " onchange='this.form.submit()'>";
    resp += "Enable TX35-IT Protocol";
    resp += "</label>";
    resp += "</div>";
    resp += "<div class='option-description'>TX35-IT, TX35DTH-IT sensors (9.579 kbps)</div>";
    resp += "</div>";

    // WS1600
    resp += "<div class='radio-group'>";
    resp += "<h3 style='margin: 8px 0; font-size: 14px; color: var(--primary-color);'>WS1600 Weather</h3>";
    resp += "<div class='radio-item'>";
    resp += "<label>";
    resp += "<input type='checkbox' name='proto_ws1600' value='1'";
    if (config.proto_ws1600) resp += checked;
    resp += " onchange='this.form.submit()'>";
    resp += "Enable WS1600 Protocol";
    resp += "</label>";
    resp += "</div>";
    resp += "<div class='option-description'>Weather sensors (9 bytes)</div>";
    resp += "</div>";
    
    // WT440XH
    resp += "<div class='radio-group'>";
    resp += "<h3 style='margin: 8px 0; font-size: 14px; color: var(--primary-color);'>WT440XH Temp/Humidity</h3>";
    resp += "<div class='radio-item'>";
    resp += "<label>";
    resp += "<input type='checkbox' name='proto_wt440xh' value='1'";
    if (config.proto_wt440xh) resp += checked;
    resp += " onchange='this.form.submit()'>";
    resp += "Enable WT440XH Protocol";
    resp += "</label>";
    resp += "</div>";
    resp += "<div class='option-description'>Compact temperature/humidity sensors (4 bytes)</div>";
    resp += "</div>";
    
    resp += "</form>";
    resp += "</div>";

    resp += "</div>";
    
    resp += "<div class='card card-full'>";
    resp += "<div class='refresh-control'>";
    resp += "<div>";
    resp += "<strong>Automatic Data Refresh</strong><br>";
    resp += "<span style='font-size:12px;color:var(--secondary-text-color)'>System status updates every 5 seconds</span><br>";
    resp += "<span class='refresh-status' id='refresh-status' style='font-size:12px;'>⏳ Starting...</span>";
    resp += "</div>";
    resp += "<button id='auto-refresh-btn' onclick='toggleAutoRefresh()' style='background-color:var(--warning-color);min-width:120px;'>⏸️ Pause Auto-Refresh</button>";
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

// Schöne Upload-Seite
void handle_update_page() {
    String page;
    add_header(page, "Firmware Update");
    
    page += "<div class='card'>";
    page += "<h2>📦 Firmware Update</h2>";
    page += "<p class='info-text'>Upload a new firmware binary (.bin file) to update your device.</p>";
    
    page += "<div style='background-color: rgba(255, 152, 0, 0.1); border: 1px solid var(--warning-color); border-radius: 8px; padding: 16px; margin: 16px 0;'>";
    page += "<p style='margin: 0; color: var(--warning-color); font-weight: 500;'>⚠️ Warning</p>";
    page += "<p style='margin: 8px 0 0 0; font-size: 13px;'>The device will restart after the update. Make sure you have the correct firmware file.</p>";
    page += "</div>";
    
    page += "<form method='POST' action='/update' enctype='multipart/form-data' id='upload_form'>";
    page += "<div style='margin: 20px 0;'>";
    page += "<label style='display: block; margin-bottom: 8px; font-weight: 500;'>Select Firmware File (.bin)</label>";
    page += "<input type='file' name='update' accept='.bin' id='file_input' required ";
    page += "style='width: 100%; padding: 12px; border: 2px dashed var(--divider-color); border-radius: 8px; ";
    page += "background-color: var(--secondary-background-color); cursor: pointer;'>";
    page += "<p id='file_info' style='margin-top: 8px; font-size: 12px; color: var(--secondary-text-color);'></p>";
    page += "</div>";
    
    page += "<div style='margin: 20px 0;'>";
    page += "<div id='progress_container' style='display: none; margin-bottom: 16px;'>";
    page += "<div style='background-color: var(--secondary-background-color); border-radius: 8px; height: 30px; overflow: hidden; border: 1px solid var(--divider-color);'>";
    page += "<div id='progress_bar' style='height: 100%; background: linear-gradient(90deg, var(--primary-color), var(--accent-color)); width: 0%; transition: width 0.3s; display: flex; align-items: center; justify-content: center; color: white; font-weight: 500; font-size: 13px;'></div>";
    page += "</div>";
    page += "<p id='progress_text' style='text-align: center; margin-top: 8px; font-size: 13px; color: var(--primary-text-color);'></p>";
    page += "</div>";
    
    page += "<button type='submit' id='upload_button' class='action-button' style='width: 100%; padding: 14px; font-size: 15px;'>";
    page += "🚀 Upload Firmware";
    page += "</button>";
    page += "</div>";
    page += "</form>";
    
    page += "<div style='margin-top: 24px; padding-top: 16px; border-top: 1px solid var(--divider-color);'>";
    page += "<h3>Current Firmware</h3>";
    page += "<p class='info-text'>Version: " + String(LACROSSE2MQTT_VERSION) + "</p>";
    page += "<p class='info-text'>Built: " + String(__DATE__) + " " + String(__TIME__) + "</p>";
    page += "</div>";
    page += "</div>";
    
    page += "<div class='card' style='margin-top: 16px;'>";
    page += "<h3>💡 Instructions</h3>";
    page += "<ol style='margin: 8px 0; padding-left: 24px; color: var(--primary-text-color);'>";
    page += "<li style='margin: 8px 0;'>Download the latest firmware .bin file</li>";
    page += "<li style='margin: 8px 0;'>Select the file using the button above</li>";
    page += "<li style='margin: 8px 0;'>Click 'Upload Firmware' and wait for completion</li>";
    page += "<li style='margin: 8px 0;'>Device will restart automatically after update</li>";
    page += "</ol>";
    page += "<p style='margin-top: 16px;'><a href='/' class='action-button'>← Back to Main Page</a></p>";
    page += "</div>";
    
    // JavaScript für Upload-Progress und File-Info
    page += "<script>";
    page += "document.getElementById('file_input').addEventListener('change', function(e) {";
    page += "  var file = e.target.files[0];";
    page += "  if (file) {";
    page += "    var size = (file.size / 1024 / 1024).toFixed(2);";
    page += "    document.getElementById('file_info').textContent = '📄 ' + file.name + ' (' + size + ' MB)';";
    page += "  }";
    page += "});";
    
    page += "document.getElementById('upload_form').addEventListener('submit', function(e) {";
    page += "  e.preventDefault();";
    page += "  var formData = new FormData(this);";
    page += "  var xhr = new XMLHttpRequest();";
    page += "  ";
    page += "  document.getElementById('progress_container').style.display = 'block';";
    page += "  document.getElementById('upload_button').disabled = true;";
    page += "  document.getElementById('upload_button').style.opacity = '0.5';";
    page += "  document.getElementById('upload_button').textContent = '⏳ Uploading...';";
    page += "  ";
    page += "  xhr.upload.addEventListener('progress', function(e) {";
    page += "    if (e.lengthComputable) {";
    page += "      var percent = Math.round((e.loaded / e.total) * 100);";
    page += "      document.getElementById('progress_bar').style.width = percent + '%';";
    page += "      document.getElementById('progress_bar').textContent = percent + '%';";
    page += "      document.getElementById('progress_text').textContent = 'Uploading: ' + (e.loaded / 1024 / 1024).toFixed(1) + ' MB / ' + (e.total / 1024 / 1024).toFixed(1) + ' MB';";
    page += "    }";
    page += "  });";
    page += "  ";
    page += "  xhr.addEventListener('load', function() {";
    page += "    if (xhr.status === 200) {";
    page += "      document.getElementById('progress_text').innerHTML = '<span style=\"color: var(--success-color);\">✓ Upload successful! Device is restarting...</span>';";
    page += "      document.getElementById('upload_button').textContent = '✓ Success!';";
    page += "      setTimeout(function() { window.location.href = '/'; }, 15000);";
    page += "    } else {";
    page += "      document.getElementById('progress_text').innerHTML = '<span style=\"color: var(--error-color);\">✗ Upload failed: ' + xhr.statusText + '</span>';";
    page += "      document.getElementById('upload_button').disabled = false;";
    page += "      document.getElementById('upload_button').style.opacity = '1';";
    page += "      document.getElementById('upload_button').textContent = '🔄 Try Again';";
    page += "    }";
    page += "  });";
    page += "  ";
    page += "  xhr.addEventListener('error', function() {";
    page += "    document.getElementById('progress_text').innerHTML = '<span style=\"color: var(--error-color);\">✗ Network error occurred</span>';";
    page += "    document.getElementById('upload_button').disabled = false;";
    page += "    document.getElementById('upload_button').style.opacity = '1';";
    page += "    document.getElementById('upload_button').textContent = '🔄 Try Again';";
    page += "  });";
    page += "  ";
    page += "  xhr.open('POST', '/update', true);";
    page += "  xhr.send(formData);";
    page += "});";
    page += "</script>";
    
    add_sysinfo_footer(page);
    server.send(200, "text/html", page);
}

void setup_web()
{
    if (!load_idmap())
        Serial.println("setup_web ERROR: load_idmap() failed?");
    if (!load_config())
        Serial.println("setup_web ERROR: load_config() failed?");
    
    server.on("/", handle_index);
    server.on("/index.html", handle_index);
    server.on("/sensors.json", handle_sensors_json);  // NEU!
    server.on("/config.html", handle_config);
    server.on("/debug.html", handle_debug);
    server.on("/licenses.html", handle_licenses);
    server.on("/update", HTTP_GET, handle_update_page);
    
    server.onNotFound([]() {
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