#ifndef WEB_TABLE_BUILDER_H
#define WEB_TABLE_BUILDER_H

#include <Arduino.h>
#include "globals.h"

/**
 * @brief Dynamischer HTML-Tabellen-Builder für Sensordaten
 * 
 * Analysiert vorhandene Sensordaten und zeigt nur relevante Spalten an.
 * Reduziert HTML-Overhead und verbessert die Übersichtlichkeit.
 */
class WebTableBuilder {
private:
    // Welche Datentypen sind vorhanden?
    bool hasTempCh2;
    bool hasHumidity;
    bool hasWindSpeed;
    bool hasWindDir;
    bool hasWindGust;
    bool hasRain;
    bool hasPower;
    bool hasPressure;
    
    /**
     * @brief Analysiert Cache und prüft welche Datentypen vorhanden sind
     */
    void analyzeSensorData() {
        hasTempCh2 = false;
        hasHumidity = false;
        hasWindSpeed = false;
        hasWindDir = false;
        hasWindGust = false;
        hasRain = false;
        hasPower = false;
        hasPressure = false;
        
        for (int i = 0; i < SENSOR_NUM; i++) {
            // Filter: Überspringe ungültige, gelöschte oder rate=0 Sensoren
            if (fcache[i].timestamp == 0 || fcache[i].ID == 0xFF || fcache[i].rate == 0)
                continue;
            
            if (fcache[i].temp_ch2 != 0 && fcache[i].temp_ch2 > -100 && fcache[i].temp_ch2 < 100)
                hasTempCh2 = true;
            if (fcache[i].humi > 0 && fcache[i].humi <= 100)
                hasHumidity = true;
            if (fcache[i].wind_speed > 0)
                hasWindSpeed = true;
            if (fcache[i].wind_direction >= 0 && fcache[i].wind_direction <= 360)
                hasWindDir = true;
            if (fcache[i].wind_gust > 0)
                hasWindGust = true;
            if (fcache[i].rain_total > 0)
                hasRain = true;
            if (fcache[i].power > 0)
                hasPower = true;
            if (fcache[i].pressure > 0)
                hasPressure = true;
        }
    }
    
    /**
     * @brief Baut den Tabellenkopf dynamisch auf
     */
    void buildTableHeader(String& s) {
        s += "<thead><tr>";
        s += "<th>ID</th>";
        s += "<th>Type</th>";
        s += "<th>Temperature</th>";
        
        if (hasTempCh2)
            s += "<th>Temp 2</th>";
        if (hasHumidity)
            s += "<th>Humidity</th>";
        if (hasWindSpeed)
            s += "<th>Wind Speed</th>";
        if (hasWindDir)
            s += "<th>Wind Dir</th>";
        if (hasWindGust)
            s += "<th>Wind Gust</th>";
        if (hasRain)
            s += "<th>Rain</th>";
        if (hasPower)
            s += "<th>Power</th>";
        if (hasPressure)
            s += "<th>Pressure</th>";
        
        s += "<th>RSSI</th>";
        s += "<th>Name</th>";
        s += "<th>Age (ms)</th>";
        s += "<th>Battery</th>";
        s += "<th>New Batt</th>";
        s += "<th>Raw Frame Data</th>";
        s += "</tr></thead>\n";
    }
    
    /**
     * @brief Fügt optionale Tabellenzelle hinzu (nur wenn Spalte sichtbar)
     */
    void addOptionalCell(String& s, bool visible, const String& value, const String& unit = "") {
        if (visible) {
            s += "<td>";
            if (value.length() > 0) {
                s += value;
                if (unit.length() > 0)
                    s += " " + unit;
            } else {
                s += "-";
            }
            s += "</td>";
        }
    }
    
public:
    /**
     * @brief Generiert komplette Sensortabelle als HTML
     * 
     * @param s Output String (wird erweitert)
     * @param now Aktueller Zeitstempel (millis())
     */
    void buildTable(String& s, unsigned long now) {
        // SCHRITT 1: Analysiere welche Datentypen vorhanden sind
        analyzeSensorData();
        
        // SCHRITT 2: Baue Tabelle
        s += "<h2>Current sensor data</h2>\n";
        s += "<table id='sensor-table'>\n";
        
        // Header
        buildTableHeader(s);
        
        // Body
        s += "<tbody id='sensor-tbody'>\n";
        
        int sensorCount = 0;
        for (int i = 0; i < SENSOR_NUM; i++) {
            if (fcache[i].timestamp == 0 || fcache[i].ID == 0xFF)
                continue;
            
            sensorCount++;
            
            String name = id2name[fcache[i].ID];
            if (name.length() == 0)
                name = "-";
            
            int displayID = fcache[i].ID;
            String sensorType = String(fcache[i].sensorType);
            if (sensorType.length() == 0)
                sensorType = "LaCrosse";
            
            s += "<tr>";
            
            // ID
            s += "<td>" + String(displayID) + "</td>";
            
            // Type
            s += "<td>" + sensorType + "</td>";
            
            // Temperatur 1 (immer anzeigen)
            s += "<td>" + String(fcache[i].temp, 1) + " °C</td>";
            
            // Optional: Temperatur 2
            String temp2 = "";
            if (fcache[i].temp_ch2 != 0 && fcache[i].temp_ch2 > -100 && fcache[i].temp_ch2 < 100) {
                temp2 = String(fcache[i].temp_ch2, 1);
            }
            addOptionalCell(s, hasTempCh2, temp2, "°C");
            
            // Optional: Luftfeuchtigkeit
            String humi = "";
            if (fcache[i].humi > 0 && fcache[i].humi <= 100) {
                humi = String(fcache[i].humi);
            }
            addOptionalCell(s, hasHumidity, humi, "%");
            
            // Optional: Wind Speed
            String windSpeed = "";
            if (fcache[i].wind_speed > 0) {
                windSpeed = String(fcache[i].wind_speed, 1);
            }
            addOptionalCell(s, hasWindSpeed, windSpeed, "km/h");
            
            // Optional: Wind Direction
            String windDir = "";
            if (fcache[i].wind_direction >= 0 && fcache[i].wind_direction <= 360) {
                windDir = String(fcache[i].wind_direction);
            }
            addOptionalCell(s, hasWindDir, windDir, "°");
            
            // Optional: Wind Gust
            String windGust = "";
            if (fcache[i].wind_gust > 0) {
                windGust = String(fcache[i].wind_gust);
            }
            addOptionalCell(s, hasWindGust, windGust, "km/h");
            
            // Optional: Rain
            String rain = "";
            if (fcache[i].rain_total > 0) {
                rain = String(fcache[i].rain_total, 1);
            }
            addOptionalCell(s, hasRain, rain, "mm");
            
            // Optional: Power
            String power = "";
            if (fcache[i].power > 0) {
                power = String(fcache[i].power, 1);
            }
            addOptionalCell(s, hasPower, power, "W");
            
            // Optional: Pressure
            String pressure = "";
            if (fcache[i].pressure > 0) {
                pressure = String(fcache[i].pressure, 1);
            }
            addOptionalCell(s, hasPressure, pressure, "hPa");
            
            // RSSI
            s += "<td>" + String(fcache[i].rssi) + "</td>";
            
            // Name
            s += "<td>" + name + "</td>";
            
            // Age
            unsigned long age = now - fcache[i].timestamp;
            s += "<td>" + String(age) + "</td>";
            
            // Battery
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
            
            // Raw data
            s += "<td class='raw-data'>0x";
            for (int j = 0; j < FRAME_LENGTH; j++) {
                char tmp[3];
                snprintf(tmp, 3, "%02X", fcache[i].data[j]);
                s += String(tmp);
                if (j < FRAME_LENGTH - 1)
                    s += " ";
            }
            s += "</td>";
            
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
};

#endif // WEB_TABLE_BUILDER_H
