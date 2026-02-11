/**
 * web_protocol_config.h
 * Datenstruktur für vereinfachte Protokoll-Konfiguration
 */

#ifndef WEB_PROTOCOL_CONFIG_H
#define WEB_PROTOCOL_CONFIG_H

#include "globals.h"
#include <WebServer.h>

struct ProtocolInfo {
    const char* name;
    const char* display_name;
    const char* description;
    bool* enabled;
    const char* datarate;
    const char* color;
};

const ProtocolInfo PROTOCOLS[] = {
    {"lacrosse", "LaCrosse IT+", "TX29-IT, TX27-IT, TX25-U sensors", &config.proto_lacrosse, "17.241 kbps", "--primary-color"},
    {"wh24", "WH24 Weather", "WH24 outdoor sensor at 868.300 MHz", &config.proto_wh24, "17.241 kbps", "--primary-color"},
    {"wh25", "WH25 Pressure", "WH25 barometric pressure sensor", &config.proto_wh25, "17.241 kbps", "--primary-color"},
    {"tx35it", "TX35-IT/TX35DTH-IT", "TX35-IT, TX35DTH-IT sensors", &config.proto_tx35it, "9.579 kbps", "--success-color"},
    {"tx38it", "TX38-IT Indoor", "TX38-IT indoor temperature sensors", &config.proto_tx38it, "8.842 kbps", "--warning-color"},
    {"wh1080", "WH1080 Weather", "Complete weather stations with wind, rain", &config.proto_wh1080, "6.618 kbps", "--info-color"},
    {"ws1600", "WS1600 Weather", "WS1600 weather sensors", &config.proto_ws1600, "6.618 kbps", "--info-color"},
    {"wt440xh", "WT440XH Temp/Hum", "Compact temperature/humidity sensors", &config.proto_wt440xh, "6.618 kbps", "--info-color"},
    {"tx22it", "TX22-IT Weather", "TX22-IT complete weather station", &config.proto_tx22it, "6.618 kbps", "--info-color"},
    {"emt7110", "EMT7110 Energy", "EMT7110 energy meter with power data", &config.proto_emt7110, "6.618 kbps", "--info-color"},
    {"w136", "W136", "W136 weather sensors at 869.820 MHz", &config.proto_w136, "4.800 kbps", "#9c27b0"}
};

const int PROTOCOL_COUNT = sizeof(PROTOCOLS) / sizeof(PROTOCOLS[0]);

void generateProtocolCheckboxes(String &resp) {
    String current_group = "";
    for (int i = 0; i < PROTOCOL_COUNT; i++) {
        const ProtocolInfo& proto = PROTOCOLS[i];
        if (current_group != proto.datarate) {
            if (!current_group.isEmpty()) resp += "</div>";
            resp += "<div style='margin:20px 0;padding:16px;background-color:rgba(3,169,244,0.05);border-radius:8px;border-left:4px solid var(" + String(proto.color) + ");'>";
            resp += "<h3 style='margin:0 0 12px 0;color:var(" + String(proto.color) + ");font-size:16px;'>⚡ " + String(proto.datarate) + "</h3>";
            current_group = proto.datarate;
        }
        resp += "<div class='radio-group'><h4 style='margin:8px 0;font-size:14px;'>" + String(proto.display_name) + "</h4>";
        resp += "<div class='radio-item'><label>";
        resp += "<input type='checkbox' name='proto_" + String(proto.name) + "' value='1'";
        if (*proto.enabled) resp += " checked";
        resp += " onchange='this.form.submit()'>Enable " + String(proto.display_name) + " Protocol</label></div>";
        resp += "<div class='option-description'>" + String(proto.description) + "</div></div>";
    }
    resp += "</div>";
}

bool processProtocolParameters(WebServer &server) {
    bool changed = false;
    bool proto_form_submitted = false;
    for (int i = 0; i < server.args(); i++) {
        String argName = server.argName(i);
        if (argName.startsWith("proto_")) {
            proto_form_submitted = true;
            break;
        }
    }
    if (!proto_form_submitted) return false;
    for (int i = 0; i < PROTOCOL_COUNT; i++) {
        const ProtocolInfo& proto = PROTOCOLS[i];
        String param_name = "proto_" + String(proto.name);
        bool new_value = (server.hasArg(param_name) && server.arg(param_name) == "1");
        if (*proto.enabled != new_value) {
            *proto.enabled = new_value;
            changed = true;
            Serial.printf("%s protocol changed to: %d\n", proto.display_name, new_value);
        }
    }
    return changed;
}

#endif