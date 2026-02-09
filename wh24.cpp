#include "wh24.h"
#include "globals.h"

// Hilfsfunktion für Windrichtung
const char* GetWindDirection(int bearing) {
    const char* directions[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", 
                                "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    int index = ((bearing + 11) / 22) % 16;
    return directions[index];
}

// CRC-Prüfung für WH24
byte calculate_crc(byte *payload, int len) {
    byte crc = 0;
    for (int i = 0; i < len; i++) {
        crc += payload[i];
    }
    return crc;
}

bool WH24::TryHandleData(byte *payload, int payloadSize, Frame *frame) {
    // WH24: 17 Bytes, startet mit 0x24
    if (payloadSize != WH24_FRAME_LENGTH || payload[0] != 0x24) {
        return false;
    }
    
    // CRC prüfen (letztes Byte)
    byte calculated_crc = calculate_crc(payload, WH24_FRAME_LENGTH - 1);
    frame->crc = payload[WH24_FRAME_LENGTH - 1];
    
    if (calculated_crc != frame->crc) {
        Serial.println("WH24: CRC error");
        frame->valid = false;
        return false;
    }
    
    // ID extrahieren
    frame->ID = payload[1];
    
    // Battery Status (Bit 7 von Byte 3)
    frame->batlo = (payload[3] & 0x08) != 0;
    
    // Temperatur (Bytes 4-5, signed, 0.1°C Auflösung)
    int16_t temp_raw = (int16_t)((payload[4] << 8) | payload[5]);
    frame->temp = temp_raw / 10.0;
    
    // Luftfeuchtigkeit (Byte 6)
    frame->humi = payload[6];
    
    // Windgeschwindigkeit (Byte 8, m/s * 10)
    uint16_t wind_speed_raw = payload[8] | ((payload[3] & 0x10) << 4);
    frame->wind_speed = wind_speed_raw * 0.125 * 1.12; // WH24 Korrekturfaktor
    
    // Windböen (Byte 7, m/s)
    frame->wind_gust = payload[7] * 1.12;
    
    // Regenmenge (Bytes 9-10, 0.3mm pro tip)
    uint16_t rainfall_raw = (payload[9] << 8) | payload[10];
    frame->rain = rainfall_raw * 0.3;
    
    // Windrichtung (0-359°)
    frame->wind_bearing = payload[2] | ((payload[3] & 0x80) << 1);
    
    // UV-Index (Byte 11)
    uint16_t uv_raw = payload[11] | ((payload[12] & 0x0F) << 8);
    float uv_index = uv_raw / 10.0;
    frame->uv_index = (byte)(uv_index + 0.5);
    
    // Luftdruck (Bytes 13-14, hPa, 0.1 hPa Auflösung)
    uint16_t pressure_raw = (payload[13] << 8) | payload[14];
    frame->pressure = pressure_raw / 10.0;
    
    frame->valid = true;
    
    return true;
}

void WH24::DisplayFrame(byte *payload, int payloadSize, Frame *frame) {
    Serial.println();
    Serial.println("===== WH24 Weather Station =====");
    Serial.print("ID: ");
    Serial.println(frame->ID, HEX);
    Serial.print("Temperature: ");
    Serial.print(frame->temp, 1);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(frame->humi);
    Serial.println(" %");
    Serial.print("Pressure: ");
    Serial.print(frame->pressure, 1);
    Serial.println(" hPa");
    Serial.print("Wind Speed: ");
    Serial.print(frame->wind_speed, 1);
    Serial.println(" km/h");
    Serial.print("Wind Gust: ");
    Serial.print(frame->wind_gust, 1);
    Serial.println(" km/h");
    Serial.print("Wind Direction: ");
    Serial.print(GetWindDirection(frame->wind_bearing));
    Serial.print(" (");
    Serial.print(frame->wind_bearing);
    Serial.println("°)");
    Serial.print("Rain: ");
    Serial.print(frame->rain, 1);
    Serial.println(" mm");
    Serial.print("UV Index: ");
    Serial.println(frame->uv_index);
    Serial.print("Battery: ");
    Serial.println(frame->batlo ? "LOW" : "OK");
    Serial.print("RSSI: ");
    Serial.print(frame->rssi);
    Serial.println(" dBm");
    Serial.println("================================");
}

void WH24::DisplayRaw(unsigned long &last, const char *dev, uint8_t *data, uint8_t len, int8_t rssi, int rate) {
    unsigned long now = millis();
    if (now - last < 1000)
        return;
    last = now;
    
    Serial.print(dev);
    Serial.print(": ");
    for (int i = 0; i < len; i++) {
        if (data[i] < 0x10)
            Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.print(" RSSI: ");
    Serial.print(rssi);
    Serial.print(" Rate: ");
    Serial.println(rate);
}