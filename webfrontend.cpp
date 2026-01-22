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
    doc["uptime"] = time_string();
    doc["mqtt_ok"] = mqtt_ok;
    doc["wifi_ok"] = (WiFi.status() == WL_CONNECTED);
    doc["cpu_usage"] = serialized(String(cpu_usage, 1));
    doc["loop_count"] = loop_count;
    
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

static void add_header(String &s, const String &title)
{
    s = "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    
    // NEU: Auto-Refresh JavaScript nur auf Index-Seite
    if (title.indexOf("Gateway") > -1) {
        s += "<script>"
             "let autoRefreshEnabled = true;"
             "let refreshInterval = 5000;"
             "let refreshTimer;"
             
             "function updateSensorData() {"
             "  if (!autoRefreshEnabled) return;"
             "  "
             "  fetch('/sensors.json')"
             "    .then(response => response.json())"
             "    .then(data => {"
             "      const tbody = document.getElementById('sensor-tbody');"
             "      if (!tbody) return;"
             "      "
             "      tbody.innerHTML = '';"
             "      "
             "      data.sensors.forEach(sensor => {"
             "        const row = tbody.insertRow();"
             "        row.innerHTML = `"
             "          <td>${sensor.id}</td>"
             "          <td>${sensor.ch}</td>"
             "          <td>${sensor.type}</td>"
             "          <td>${sensor.temp} °C</td>"
             "          <td>${sensor.humi > 0 && sensor.humi <= 100 ? sensor.humi + ' %' : '-'}</td>"
             "          <td>${sensor.rssi}</td>"
             "          <td>${sensor.name || '-'}</td>"
             "          <td>${sensor.age}</td>"
             "          <td class='${sensor.batlo ? 'batt-weak' : 'batt-ok'}'>${sensor.batlo ? 'weak' : 'ok'}</td>"
             "          <td class='${sensor.init ? 'init-new' : 'init-no'}'>${sensor.init ? 'yes' : 'no'}</td>"
             "          <td class='raw-data'>0x${sensor.raw}</td>"
             "        `;"
             "      });"
             "      "
             "      const countElem = document.getElementById('sensor-count');"
             "      if (countElem) {"
             "        if (data.count === 0) {"
             "          countElem.innerHTML = '<em>No sensors found. Waiting for data...</em>';"
             "        } else {"
             "          countElem.innerHTML = '<em>Total sensors: ' + data.count + ' | Last update: ' + new Date().toLocaleTimeString() + '</em>';"
             "        }"
             "      }"
             "      "
             "      const refreshStatus = document.getElementById('refresh-status');"
             "      if (refreshStatus) {"
             "        refreshStatus.textContent = '✓ Live (updated ' + new Date().toLocaleTimeString() + ')';"
             "        refreshStatus.style.color = 'var(--success-color)';"
             "      }"
             "    })"
             "    .catch(error => {"
             "      console.error('Error fetching sensor data:', error);"
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
             "    btn.textContent = '⏸️ Pause';"
             "    btn.style.backgroundColor = 'var(--warning-color)';"
             "    status.textContent = '⏳ Starting...';"
             "    status.style.color = 'var(--info-color)';"
             "    startAutoRefresh();"
             "    updateSensorData();"
             "  } else {"
             "    btn.textContent = '▶️ Resume';"
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

    // Rest des Headers...
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
        "}";

    // Alle bisherigen Styles hier einfügen (zu lang für diesen Kontext)...
    // Nur relevante neue Styles:
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
         "<a href='https://github.com/steigerbalett/lacrosse2mqtt'>Powered by LaCrosse2MQTT</a> | "
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
    
    // NEU: Auto-Refresh Control
    index += "<div class='refresh-control'>";
    index += "<span class='refresh-status' id='refresh-status'>⏳ Starting...</span>";
    index += "<button id='auto-refresh-btn' onclick='toggleAutoRefresh()' style='background-color: var(--warning-color);'>⏸️ Pause</button>";
    index += "</div>";
    
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
    index += "<p class='info-text'>CPU Load: " + String(cpu_usage, 1) + "%</p>";
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

// Rest der Funktionen bleiben unverändert (handle_config, handle_debug, etc.)
// ... (zu lang für Kontext)

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